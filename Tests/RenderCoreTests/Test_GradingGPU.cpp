// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

#include "TestFramework.h"

#include <Fluxion/Foundation/Handle.hpp>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MaterialParameters.h>
#include <Fluxion/RenderCore/Renderer/MaterialShader.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/RenderCore/Renderer/TextureDefaults.h>
#include <Fluxion/RenderCore/Scene/RenderWorld.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

// A COLOURIST'S CONTROLS, MEASURED BY WHAT THEY MOVE AND WHAT THEY DO NOT.
//
// Grading is unusually easy to write in a way that looks like it works. A
// saturation control that also darkens the picture, a white balance that
// only ever brightens, a lift that raises the whole frame instead of its
// shadows -- all of them produce a picture that is visibly different from
// the one before, which is the entire evidence a screenshot offers.
//
// So every check here is TWO-SIDED. It is not enough that warming the
// picture raised the red: the blue has to have come down, or what was
// measured was an exposure control with a colourful name. It is not
// enough that dropping the saturation made the channels equal: the
// brightness has to have stayed where it was, or the control is a fade to
// black. And the lift is read WHERE THE PICTURE IS BLACK, because that is
// the half of the range it is supposed to own.

extern "C" void FluxionForwardOpaquePass_Execute(FluxionRHICommandListHandle commandList, void* userData);
extern "C" void FluxionPostProcessPass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

constexpr u32 kWidth = 64;
constexpr u32 kHeight = 64;

// The square covers part of the frame and not all of it, so that there is
// somewhere black to read as well as somewhere lit. WHICH ROWS it lands
// on is not decided here: a texture's first row is the top of the picture
// on one backend and the bottom on another, so the square is found.
constexpr u32 kColumn = 32;

// DELIBERATELY UNEQUAL, and in an order the test can name: the red is the
// most of it, the blue the least. A grey square would pass a saturation
// check that did nothing at all.
constexpr f32 kEmissiveRed = 0.60f;
constexpr f32 kEmissiveGreen = 0.25f;
constexpr f32 kEmissiveBlue = 0.10f;

// The value the curve brings out as one. With the emissive above, every
// channel lands well under half -- which is the side of the contrast
// pivot this test wants them on, and far from where a half float starts
// losing the difference between two nearby answers.
constexpr f32 kWhitePoint = 4.0f;

struct GradingVertex
{
    f32 position[3];
    f32 normal[3];
    f32 tangent[4];
    f32 uv[2];
};

// What the view is asked for, in the form the description takes: every
// number a distance from leaving the picture alone, so that the neutral
// frame below is a zeroed structure rather than a list of ones.
struct GradingSettings
{
    f32 temperature;
    f32 tint;
    f32 contrast;
    f32 saturation;
    FluxionVec3 lift;
    FluxionVec3 gamma;
    FluxionVec3 gain;
};

const char* const kMaterialSource = R"(
#include "Fluxion/Material.jsl"

SurfaceData EvaluateSurface() {
  return StandardSurface();
}
)";

// SIXTEEN BITS A CHANNEL IS NOT A FLOAT. The target holds half floats,
// and reading them as anything else gives numbers that are not merely
// wrong but plausible.
f32 HalfBitsToFloat(u16 half)
{
    const u32 sign = (u32)(half >> 15) & 1u;
    const u32 exponent = (u32)(half >> 10) & 0x1Fu;
    const u32 mantissa = (u32)half & 0x3FFu;

    u32 bits;
    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            bits = sign << 31;
        }
        else
        {
            u32 shifted = mantissa;
            i32 e = -1;
            while ((shifted & 0x400u) == 0) { shifted <<= 1; --e; }
            shifted &= 0x3FFu;
            bits = (sign << 31) | ((u32)(e + 15 + 112) << 23) | (shifted << 13);
        }
    }
    else if (exponent == 31)
    {
        bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = (sign << 31) | ((exponent + 112u) << 23) | (mantissa << 13);
    }

    f32 value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

FluxionMat4 Orthographic()
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = -0.25f;
    m.m[2][3] = 0.5f;
    return m;
}

FluxionMat4 TranslationOf(f32 x, f32 y, f32 z)
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}

const GradingVertex kQuad[4] = {
    { { -0.25f, 0.15f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 0.25f, 0.15f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { 0.25f, 0.65f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.25f, 0.65f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

const u16 kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

// One frame, graded as asked, and the picture it resolved to. The passes
// are called by hand rather than through a render graph, so the barriers
// the graph would have inserted are this function's to say.
bool RenderOnce(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, FluxionRHICommandListHandle cmd,
                FluxionRendererHandle renderer, FluxionRenderTargetHandle target, FluxionRHITextureHandle color,
                FluxionRHITextureHandle depth, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                FluxionRenderPipelineHandle pipeline, FluxionRHIBufferHandle readback, const GradingSettings& grade,
                bool first, std::vector<f32>& outPixels)
{
    FluxionRenderViewDesc viewSettings{};
    std::memset(&viewSettings, 0, sizeof(viewSettings));
    viewSettings.viewMatrix = TranslationOf(0.0f, 0.0f, -1.0f);
    viewSettings.projectionMatrix = Orthographic();
    viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
    viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
    viewSettings.renderTarget = target;
    viewSettings.layerMask = 0xFFFFFFFFu;

    // NOTHING LIGHTS THIS SCENE. The square is the only thing sending out
    // light, and it sends out exactly what the material was told to --
    // which is what lets the numbers below be numbers this test chose.
    viewSettings.ambientColor = FluxionVec3{ 0.0f, 0.0f, 0.0f };

    viewSettings.exposure = 1.0f;
    viewSettings.tonemapWhitePoint = kWhitePoint;
    viewSettings.encodeOutputToSRGB = false;

    viewSettings.gradeTemperature = grade.temperature;
    viewSettings.gradeTint = grade.tint;
    viewSettings.gradeContrast = grade.contrast;
    viewSettings.gradeSaturation = grade.saturation;
    viewSettings.gradeLift = grade.lift;
    viewSettings.gradeGamma = grade.gamma;
    viewSettings.gradeGain = grade.gain;

    FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewSettings);
    if (!FLUXION_HANDLE_IS_VALID(view)) return false;

    Fluxion_RenderView_UpdateFrameConstants(view);
    Fluxion_Renderer_BeginFrame(renderer, view);

    FluxionRenderWorld world{};
    if (!Fluxion_RenderWorld_Init(&world))
    {
        Fluxion_RenderView_Destroy(view);
        return false;
    }

    FluxionRenderObject object{};
    std::memset(&object, 0, sizeof(object));
    object.transform = Fluxion_Mat4_Identity();
    object.previousTransform = object.transform;
    object.mesh = mesh;
    object.material = material;
    object.pipeline = pipeline;
    object.layerMask = 0xFFFFFFFFu;
    object.visible = true;
    Fluxion_RenderWorld_AddObject(&world, &object);

    Fluxion_RHI_CommandList_Begin(cmd);

    // The frame's own image-based lighting, which the forward pass binds
    // whether or not this scene has an environment -- and a binding says
    // the texture is readable, so it has to have been made readable.
    Fluxion_Renderer_UpdateEnvironment(renderer, view, cmd);

    Fluxion_Renderer_SubmitRenderWorld(renderer, &world);
    Fluxion_Renderer_UploadScene(renderer, cmd);

    const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
    const FluxionRHIResourceState wasColor = first ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE;
    const FluxionRHIResourceState wasDepth = first ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE;
    const FluxionRHIResourceState wasScene = first ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_SHADER_READ;

    // Nothing casts a shadow here and the atlas is still bound: a binding
    // says the texture is readable whether the shader reads it or not.
    FluxionRHIBarrier atlasToRead = { Fluxion_RenderView_GetShadowAtlasTexture(view), noBuffer,
                                      FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(cmd, &atlasToRead, 1);

    FluxionRHIBarrier depthToWrite = { depth, noBuffer, wasDepth, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(cmd, &depthToWrite, 1);

    FluxionRHIBarrier sceneToTarget = { Fluxion_Renderer_GetSceneColorTexture(renderer), noBuffer, wasScene,
                                        FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(cmd, &sceneToTarget, 1);

    FluxionForwardOpaquePass_Execute(cmd, Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

    FluxionRHIBarrier sceneToRead = { Fluxion_Renderer_GetSceneColorTexture(renderer), noBuffer,
                                      FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(cmd, &sceneToRead, 1);

    FluxionRHIBarrier colorToTarget = { color, noBuffer, wasColor, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(cmd, &colorToTarget, 1);

    FluxionPostProcessPass_Execute(cmd, Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

    FluxionRHIBarrier colorToSource = { color, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                        FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(cmd, &colorToSource, 1);

    Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, color, 0, 0, readback, 0);
    Fluxion_Renderer_EndFrame(renderer, cmd);
    Fluxion_RHI_CommandList_End(cmd);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
    const bool finished = Fluxion_RHI_WaitForFence(fence);
    Fluxion_RHI_DestroyFence(fence);

    bool read = false;
    if (finished)
    {
        const usize rowBytes = (usize)kWidth * 4u * sizeof(u16);
        const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                      FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;

        if (const u8* mapped = (const u8*)Fluxion_RHI_MapBuffer(readback))
        {
            outPixels.assign((usize)kWidth * kHeight * 4u, 0.0f);
            for (u32 y = 0; y < kHeight; ++y)
            {
                const u8* row = mapped + (usize)y * alignedRowBytes;
                for (u32 x = 0; x < kWidth * 4u; ++x)
                {
                    u16 half = 0;
                    std::memcpy(&half, row + (usize)x * sizeof(u16), sizeof(half));
                    outPixels[(usize)y * kWidth * 4u + x] = HalfBitsToFloat(half);
                }
            }
            Fluxion_RHI_UnmapBuffer(readback);
            read = true;
        }
    }

    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RenderWorld_Shutdown(&world);
    Fluxion_RenderView_Destroy(view);
    TEST_CHECK(ctx, read);
    return read;
}

f32 Channel(const std::vector<f32>& pixels, u32 x, u32 y, u32 channel)
{
    const usize at = ((usize)y * kWidth + x) * 4u + channel;
    if (at >= pixels.size()) return 0.0f;
    return pixels[at];
}

f32 Brightness(const std::vector<f32>& pixels, u32 x, u32 y)
{
    return (Channel(pixels, x, y, 0) + Channel(pixels, x, y, 1) + Channel(pixels, x, y, 2)) / 3.0f;
}

// What the eye weighs the picture at -- the same weights the shader uses,
// because the claim being checked is that dropping the colour out of a
// picture leaves THIS unchanged.
f32 Luminance(const std::vector<f32>& pixels, u32 x, u32 y)
{
    return Channel(pixels, x, y, 0) * 0.2126f + Channel(pixels, x, y, 1) * 0.7152f + Channel(pixels, x, y, 2) * 0.0722f;
}

// A row inside the square and a row outside it, found by looking rather
// than assumed -- which end of the texture the top of the frame is at
// differs between backends.
bool FindRows(const std::vector<f32>& pixels, u32* outInside, u32* outOutside)
{
    bool foundInside = false;
    u32 first = 0;
    u32 last = 0;
    for (u32 y = 0; y < kHeight; ++y)
    {
        if (Brightness(pixels, kColumn, y) <= 0.01f) continue;
        if (!foundInside) { first = y; foundInside = true; }
        last = y;
    }
    if (!foundInside) return false;

    *outInside = (first + last) / 2u;

    // As far from the square as the picture allows, on whichever side has
    // the room -- so that a glow or a filter that spread a little way out
    // of the square is not what gets read as "black".
    *outOutside = first > (kHeight - 1u - last) ? 0u : kHeight - 1u;
    return true;
}

void CheckOnBackend(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
{
    if (!Fluxion_RHI_IsBackendAvailable(backend)) return;

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backend, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance)) return;

    FluxionRHIAdapterHandle adapters[8];
    FluxionRHIDeviceHandle device = Fluxion::Foundation::NoHandle<FluxionRHIDeviceHandle>();
    if (Fluxion_RHI_EnumerateAdapters(instance, adapters, 8) != 0)
    {
        FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
        device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    }
    if (!FLUXION_HANDLE_IS_VALID(device))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the grading was NOT measured on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

    // The target holds light rather than colour: the differences this
    // test measures are small, and an eight-bit target would round most
    // of them away before they could be read.
    const FluxionRHIFormat outputFormat = FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT;

    FluxionRHITextureDesc colorDesc{};
    colorDesc.width = kWidth;
    colorDesc.height = kHeight;
    colorDesc.depth = 1;
    colorDesc.mipLevels = 1;
    colorDesc.arrayLayers = 1;
    colorDesc.sampleCount = 1;
    colorDesc.format = outputFormat;
    colorDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC;
    colorDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    colorDesc.debugName = "GradingGPU.Color";
    FluxionRHITextureHandle color = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "GradingGPU.Depth";
    FluxionRHITextureHandle depth = Fluxion_RHI_CreateTexture(device, &depthDesc);

    FluxionRHITextureViewDesc viewDesc{};
    viewDesc.texture = color;
    viewDesc.format = colorDesc.format;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    FluxionRHITextureViewHandle colorView = Fluxion_RHI_CreateTextureView(device, &viewDesc);

    viewDesc.texture = depth;
    viewDesc.format = depthDesc.format;
    FluxionRHITextureViewHandle depthView = Fluxion_RHI_CreateTextureView(device, &viewDesc);

    FluxionRenderTargetDesc targetDesc{};
    targetDesc.colorViews[0] = colorView;
    targetDesc.colorViewCount = 1;
    targetDesc.depthView = depthView;
    FluxionRenderTargetHandle target = Fluxion_RenderTarget_Create(device, &targetDesc);

    FluxionMeshBufferDesc meshDesc{};
    std::memset(&meshDesc, 0, sizeof(meshDesc));
    meshDesc.vertexData = kQuad;
    meshDesc.vertexDataSize = sizeof(kQuad);
    meshDesc.indexData = kQuadIndices;
    meshDesc.indexDataSize = sizeof(kQuadIndices);
    meshDesc.use16BitIndices = true;
    meshDesc.vertexLayout.attributes[0].location = 0;
    meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[0].offset = offsetof(GradingVertex, position);
    meshDesc.vertexLayout.attributes[1].location = 1;
    meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[1].offset = offsetof(GradingVertex, normal);
    meshDesc.vertexLayout.attributes[2].location = 2;
    meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    meshDesc.vertexLayout.attributes[2].offset = offsetof(GradingVertex, tangent);
    meshDesc.vertexLayout.attributes[3].location = 3;
    meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    meshDesc.vertexLayout.attributes[3].offset = offsetof(GradingVertex, uv);
    meshDesc.vertexLayout.attributeCount = 4;
    meshDesc.vertexLayout.stride = sizeof(GradingVertex);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.25f, 0.15f, 0.0f }, FluxionVec3{ 0.25f, 0.65f, 0.0f } };
    meshDesc.debugName = "GradingGPU.Quad";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);

    char* vertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_FORWARD);

    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "GradingGPU.Standard";
    programDesc.vertexSource = vertexSource;
    programDesc.fragmentSource = fragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(device, &programDesc);
    Fluxion_MaterialShader_FreeSource(vertexSource);
    Fluxion_MaterialShader_FreeSource(fragmentSource);

    // The scene's format, because the chain is on: the forward pass draws
    // into the renderer's own target of light, not into the one above.
    FluxionRenderPipelineHandle pipeline = Fluxion_RenderPipeline_Create(device, program, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
                                                                        Fluxion_Renderer_GetSceneColorFormat(), depthDesc.format);

    Fluxion_TextureDefaults_Init(device, queue);

    FluxionRHISamplerDesc samplerDesc{};
    std::memset(&samplerDesc, 0, sizeof(samplerDesc));
    samplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "GradingGPU.Sampler";
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);

    FluxionMaterialHandle material = Fluxion_Material_Create(device, program);
    Fluxion_Material_SetBaseColor(material, FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
    Fluxion_Material_SetEmissive(material, FluxionVec3{ kEmissiveRed, kEmissiveGreen, kEmissiveBlue });
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_BASE_COLOR, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_METALLIC_ROUGHNESS, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_NORMAL, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_OCCLUSION, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_EMISSIVE, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_FlushDirty(material);

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    Fluxion_Renderer_SetOutputColorFormat(renderer, outputFormat);
    Fluxion_Renderer_SetPostProcessEnabled(renderer, true);

    // THE GLOW IS OFF, on purpose. A glow spreads light into the pixels
    // beside the square, which is exactly where this test reads its
    // black -- and a lift measured on a pixel the bloom had already
    // brightened would be measuring the bloom.
    Fluxion_Renderer_SetBloomEnabled(renderer, false);

    const usize rowBytes = (usize)kWidth * 4u * sizeof(u16);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                  FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
    FluxionRHIBufferDesc readbackDesc{ alignedRowBytes * kHeight, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "GradingGPU.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);

    GradingSettings neutralSettings{};
    std::memset(&neutralSettings, 0, sizeof(neutralSettings));

    GradingSettings greySettings = neutralSettings;
    greySettings.saturation = -1.0f;

    GradingSettings brighterSettings = neutralSettings;
    brighterSettings.gain = FluxionVec3{ 0.5f, 0.5f, 0.5f };

    GradingSettings warmSettings = neutralSettings;
    warmSettings.temperature = 1.0f;

    GradingSettings liftedSettings = neutralSettings;
    liftedSettings.lift = FluxionVec3{ 0.25f, 0.25f, 0.25f };

    std::vector<f32> neutral;
    std::vector<f32> grey;
    std::vector<f32> brighter;
    std::vector<f32> warm;
    std::vector<f32> lifted;

    const bool ok =
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                   neutralSettings, true, neutral) &&
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                   greySettings, false, grey) &&
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                   brighterSettings, false, brighter) &&
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                   warmSettings, false, warm) &&
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                   liftedSettings, false, lifted);

    if (ok)
    {
        u32 inside = 0;
        u32 outside = 0;
        const bool found = FindRows(neutral, &inside, &outside);
        TEST_CHECK(ctx, found);

        if (found)
        {
            const f32 neutralRed = Channel(neutral, kColumn, inside, 0);
            const f32 neutralGreen = Channel(neutral, kColumn, inside, 1);
            const f32 neutralBlue = Channel(neutral, kColumn, inside, 2);

            FLUXION_LOG_INFO("RenderCoreTests",
                             "%s: ungraded the square reads %.4f %.4f %.4f; grey reads %.4f %.4f %.4f; warm reads "
                             "%.4f %.4f %.4f; a half more gain reads %.4f; and where the picture is black, a lift of "
                             "a quarter reads %.4f against %.4f.",
                             backendName, (f64)neutralRed, (f64)neutralGreen, (f64)neutralBlue,
                             (f64)Channel(grey, kColumn, inside, 0), (f64)Channel(grey, kColumn, inside, 1),
                             (f64)Channel(grey, kColumn, inside, 2), (f64)Channel(warm, kColumn, inside, 0),
                             (f64)Channel(warm, kColumn, inside, 1), (f64)Channel(warm, kColumn, inside, 2),
                             (f64)Brightness(brighter, kColumn, inside), (f64)Brightness(lifted, kColumn, outside),
                             (f64)Brightness(neutral, kColumn, outside));

            // THE PICTURE IS THERE AND IT HAS COLOUR IN IT. Everything
            // below is a comparison against this frame, and all of it
            // would pass on a grey square or an empty one.
            TEST_CHECK(ctx, neutralRed > 0.05f);
            TEST_CHECK(ctx, neutralRed > neutralGreen && neutralGreen > neutralBlue);

            // AND THE BLACK IS BLACK, which is what makes the lift check
            // at the end mean something.
            TEST_CHECK(ctx, Brightness(neutral, kColumn, outside) < 0.01f);

            // --- saturation takes the colour out and leaves the light --
            const f32 greyRed = Channel(grey, kColumn, inside, 0);
            const f32 greyGreen = Channel(grey, kColumn, inside, 1);
            const f32 greyBlue = Channel(grey, kColumn, inside, 2);

            TEST_CHECK(ctx, std::fabs(greyRed - greyGreen) < 0.01f);
            TEST_CHECK(ctx, std::fabs(greyGreen - greyBlue) < 0.01f);

            // AND THIS IS THE HALF THAT CATCHES A FADE TO BLACK. Channels
            // that agree are easy: multiplying the whole picture by zero
            // does it. What a saturation control has to do as well is
            // leave the brightness where it was.
            const f32 neutralLuminance = Luminance(neutral, kColumn, inside);
            const f32 greyLuminance = Luminance(grey, kColumn, inside);
            TEST_CHECK(ctx, std::fabs(greyLuminance - neutralLuminance) < neutralLuminance * 0.1f);

            // --- gain moves the picture up ---------------------------
            TEST_CHECK(ctx, Brightness(brighter, kColumn, inside) > Brightness(neutral, kColumn, inside) * 1.2f);

            // --- and warming leans it, rather than raising it ---------
            //
            // BOTH DIRECTIONS, and that is the point. A white balance
            // that only ever added red would pass the first of these and
            // is not a white balance -- it is an exposure control with a
            // colourful name.
            TEST_CHECK(ctx, Channel(warm, kColumn, inside, 0) > neutralRed);
            TEST_CHECK(ctx, Channel(warm, kColumn, inside, 2) < neutralBlue);

            // --- and the lift owns the shadows ------------------------
            TEST_CHECK(ctx, Brightness(lifted, kColumn, outside) > 0.1f);

            // AND IT IS THE SHADOWS IT OWNS. A lift that raised the whole
            // frame by the same amount would be a brightness control:
            // what it adds has to matter less where the picture is
            // already bright than where it is black.
            const f32 addedToBlack = Brightness(lifted, kColumn, outside) - Brightness(neutral, kColumn, outside);
            const f32 addedToLit = Brightness(lifted, kColumn, inside) - Brightness(neutral, kColumn, inside);
            TEST_CHECK(ctx, addedToBlack > 0.0f);
            TEST_CHECK(ctx, addedToLit <= addedToBlack + 0.01f);
        }
    }

    Fluxion_Renderer_Destroy(renderer);
    Fluxion_Material_Destroy(material);
    Fluxion_RHI_DestroySampler(sampler);
    Fluxion_TextureDefaults_Shutdown();
    Fluxion_RenderPipeline_Destroy(pipeline);
    Fluxion_ShaderProgram_Destroy(program);
    Fluxion_MeshBuffer_Destroy(mesh);
    Fluxion_RenderTarget_Destroy(target);
    Fluxion_RHI_DestroyBuffer(readback);
    Fluxion_RHI_DestroyTextureView(colorView);
    Fluxion_RHI_DestroyTextureView(depthView);
    Fluxion_RHI_DestroyTexture(color);
    Fluxion_RHI_DestroyTexture(depth);
    Fluxion_RenderGraphPassRegistry_Shutdown();
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_GradingGPU_Run(TestContext* ctx)
{
    // A GRADED SURFACE NEEDS A SURFACE, and that needs a material shader
    // built here and now. Where nothing can build one, this says so
    // rather than reporting an empty picture as a failure.
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the grading was NOT measured here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
