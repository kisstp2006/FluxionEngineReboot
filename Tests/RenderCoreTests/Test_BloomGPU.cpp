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

// LIGHT THAT SPREADS INTO WHAT IS BESIDE IT, MEASURED WHERE IT LANDS.
//
// A glow is only worth anything if it goes SOMEWHERE ELSE. A pass that
// brightened the bright thing and nothing around it would look almost
// right in a screenshot and would be useless -- so what this reads back
// is a pixel the bright square does not cover, and what it asks is
// whether that pixel changed.
//
// Three frames of the same scene, and the differences between them are
// the whole test:
//   nothing added   -- the picture as it would be without any of this;
//   the glow added  -- the same picture, and a pixel beside the square
//                      that is now brighter;
//   the threshold raised above what the square emits -- the glow finds
//                      nothing to spread, and the picture is the first
//                      one again.
//
// The third is what makes the second mean something: a chain that added a
// constant, or that leaked the picture into itself, would pass the second
// check and fail this one.

extern "C" void FluxionForwardOpaquePass_Execute(FluxionRHICommandListHandle commandList, void* userData);
extern "C" void FluxionPostProcessPass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

constexpr u32 kWidth = 64;
constexpr u32 kHeight = 64;

// The square is a quarter of the frame wide and sits off the middle
// vertically. WHICH ROW that lands on is not something this test decides:
// a texture's first row is the top of the picture on one backend and the
// bottom on another, so the square is FOUND rather than assumed, and
// everything after that is measured relative to where it actually is.
constexpr u32 kColumn = 32;

// How bright the square is, in the units everything before the camera is
// in. Far above the threshold the first two frames use, and far below the
// one the third uses.
constexpr f32 kEmissive = 40.0f;
constexpr f32 kLowThreshold = 4.0f;
constexpr f32 kHighThreshold = 400.0f;

struct BloomVertex
{
    f32 position[3];
    f32 normal[3];
    f32 tangent[4];
    f32 uv[2];
};

const char* const kMaterialSource = R"(
#include "Fluxion/Material.jsl"

SurfaceData EvaluateSurface() {
  return StandardSurface();
}
)";

// SIXTEEN BITS A CHANNEL IS NOT A FLOAT. The target holds half floats,
// and reading them as anything else gives numbers that are not merely
// wrong but plausible -- measured, before this was here: a picture that
// read back as zeros everywhere while the pass that wrote it was drawing
// correctly.
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

// A quarter of the screen wide, and DELIBERATELY OFF THE MIDDLE.
//
// A square in the centre is its own mirror image, and a chain that turned
// the glow upside down would put it back exactly where it belonged --
// which is how a whole backend's worth of that fault went unnoticed until
// somebody looked at a picture and saw a floor in the sky. Off-centre,
// the glow has a side it belongs on.
const BloomVertex kQuad[4] = {
    { { -0.25f, 0.15f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 0.25f, 0.15f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { 0.25f, 0.65f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.25f, 0.65f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

const u16 kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

// One frame, and the picture it resolved to.
//
// The passes are called by hand rather than through a render graph, the
// way the other GPU tests here do it -- which means the barriers the
// graph would have inserted are this function's to say.
bool RenderOnce(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, FluxionRHICommandListHandle cmd,
                FluxionRendererHandle renderer, FluxionRenderTargetHandle target, FluxionRHITextureHandle color,
                FluxionRHITextureHandle depth, FluxionMeshBufferHandle mesh,
                FluxionMaterialHandle material, FluxionRenderPipelineHandle pipeline, FluxionRHIBufferHandle readback,
                f32 threshold, f32 intensity, bool first, std::vector<f32>& outPixels)
{
    FluxionRenderViewDesc viewSettings{};
    std::memset(&viewSettings, 0, sizeof(viewSettings));
    viewSettings.viewMatrix = TranslationOf(0.0f, 0.0f, -1.0f);
    viewSettings.projectionMatrix = Orthographic();
    viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
    viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
    viewSettings.renderTarget = target;
    viewSettings.layerMask = 0xFFFFFFFFu;

    // LIT FROM EVERYWHERE, so the square's brightness is a number this
    // test chose rather than one a light's placement decides.
    viewSettings.ambientColor = FluxionVec3{ kEmissive, kEmissive, kEmissive };

    // No camera at all, so that what is read back is the light itself:
    // an exposure of one and no curve leave the numbers alone.
    viewSettings.exposure = 1.0f;
    viewSettings.tonemapWhitePoint = 0.0f;
    viewSettings.encodeOutputToSRGB = false;

    viewSettings.bloomThreshold = threshold;
    viewSettings.bloomKnee = threshold * 0.1f;
    viewSettings.bloomIntensity = intensity;

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

    // NOTHING CASTS A SHADOW HERE, and the atlas is still bound: a
    // binding says the texture is readable whether the shader reads it
    // or not, so it has to be made readable.
    //
    // EVERY FRAME, because the view is made fresh every frame and its
    // atlas with it -- what came before belonged to a different texture.
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
        // Rows come back spaced out by the alignment this contract names,
        // so the picture has to be picked out of the buffer row by row.
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

f32 Brightness(const std::vector<f32>& pixels, u32 x, u32 y)
{
    const usize at = ((usize)y * kWidth + x) * 4u;
    if (at + 2 >= pixels.size()) return 0.0f;
    return (pixels[at] + pixels[at + 1] + pixels[at + 2]) / 3.0f;
}

// The rows the square covers, down one column of the picture. Found by
// looking, because which end of the texture the top of the frame is at
// differs between backends -- and a test that assumed one of them would
// pass on that backend and be meaningless on the other.
bool FindSquareRows(const std::vector<f32>& pixels, u32* outFirst, u32* outLast)
{
    bool found = false;
    for (u32 y = 0; y < kHeight; ++y)
    {
        if (Brightness(pixels, kColumn, y) <= kEmissive * 0.5f) continue;
        if (!found) { *outFirst = y; found = true; }
        *outLast = y;
    }
    return found;
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the glow was NOT measured on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

    // WHAT THE RESOLVE WRITES, and it holds light rather than colour: the
    // point of this test is how much brighter a pixel got, and an
    // eight-bit target would clip that to one.
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
    colorDesc.debugName = "BloomGPU.Color";
    FluxionRHITextureHandle color = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "BloomGPU.Depth";
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
    meshDesc.vertexLayout.attributes[0].offset = offsetof(BloomVertex, position);
    meshDesc.vertexLayout.attributes[1].location = 1;
    meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[1].offset = offsetof(BloomVertex, normal);
    meshDesc.vertexLayout.attributes[2].location = 2;
    meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    meshDesc.vertexLayout.attributes[2].offset = offsetof(BloomVertex, tangent);
    meshDesc.vertexLayout.attributes[3].location = 3;
    meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    meshDesc.vertexLayout.attributes[3].offset = offsetof(BloomVertex, uv);
    meshDesc.vertexLayout.attributeCount = 4;
    meshDesc.vertexLayout.stride = sizeof(BloomVertex);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.25f, 0.15f, 0.0f }, FluxionVec3{ 0.25f, 0.65f, 0.0f } };
    meshDesc.debugName = "BloomGPU.Quad";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);

    char* vertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_FORWARD);

    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "BloomGPU.Standard";
    programDesc.vertexSource = vertexSource;
    programDesc.fragmentSource = fragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(device, &programDesc);
    Fluxion_MaterialShader_FreeSource(vertexSource);
    Fluxion_MaterialShader_FreeSource(fragmentSource);

    // THE SCENE'S FORMAT, because the chain is on: the forward pass draws
    // into the renderer's own target of light, not into the one above.
    FluxionRenderPipelineHandle pipeline = Fluxion_RenderPipeline_Create(device, program, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
                                                                        Fluxion_Renderer_GetSceneColorFormat(), depthDesc.format);

    // A surface needs every map it names bound whether or not it reads
    // them, so the standard ones stand in.
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
    samplerDesc.debugName = "BloomGPU.Sampler";
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);

    FluxionMaterialHandle material = Fluxion_Material_Create(device, program);

    // BLACK AND EMISSIVE: nothing lights this scene, so what the square
    // sends out is entirely its own -- which is what makes the number the
    // test reads back a number it chose.
    Fluxion_Material_SetBaseColor(material, FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
    Fluxion_Material_SetEmissive(material, FluxionVec3{ kEmissive, kEmissive, kEmissive });
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_BASE_COLOR, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_METALLIC_ROUGHNESS, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_NORMAL, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_OCCLUSION, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_EMISSIVE, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_FlushDirty(material);

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    Fluxion_Renderer_SetOutputColorFormat(renderer, outputFormat);
    Fluxion_Renderer_SetPostProcessEnabled(renderer, true);
    Fluxion_Renderer_SetBloomEnabled(renderer, true);

    const usize rowBytes = (usize)kWidth * 4u * sizeof(u16);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                  FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
    FluxionRHIBufferDesc readbackDesc{ alignedRowBytes * kHeight, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "BloomGPU.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);

    std::vector<f32> withoutGlow;
    std::vector<f32> withGlow;
    std::vector<f32> aboveThreshold;

    const bool ok =
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                   kLowThreshold, 0.0f, true, withoutGlow) &&
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                   kLowThreshold, 1.0f, false, withGlow) &&
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                   kHighThreshold, 1.0f, false, aboveThreshold);

    if (ok)
    {
        u32 squareFirst = 0;
        u32 squareLast = 0;
        const bool foundSquare = FindSquareRows(withoutGlow, &squareFirst, &squareLast);
        TEST_CHECK(ctx, foundSquare);

        if (foundSquare)
        {
            // A ROW JUST PAST THE SQUARE, and the row where the square
            // WOULD be if the chain had turned the picture over.
            //
            // A glow reaches both sides -- it is wider than the square
            // that made it -- so "the other side is dark" is not the
            // question. The question is whether it is strongest NEAR the
            // thing that emitted it. A chain that mirrored somewhere puts
            // the peak at the reflection instead, which is what a floor
            // appearing in the sky looks like from a pixel's point of
            // view.
            const u32 gap = 3;
            const i32 nearRow = (i32)squareLast + (i32)gap;
            const i32 mirroredRow = (i32)kHeight - 1 - (i32)((squareFirst + squareLast) / 2u);

            TEST_CHECK(ctx, nearRow < (i32)kHeight && mirroredRow >= 0);

            if (nearRow < (i32)kHeight && mirroredRow >= 0)
            {
                const f32 insideWithout = Brightness(withoutGlow, kColumn, (squareFirst + squareLast) / 2u);
                const f32 nearWithout = Brightness(withoutGlow, kColumn, (u32)nearRow);
                const f32 nearWith = Brightness(withGlow, kColumn, (u32)nearRow);
                const f32 nearAbove = Brightness(aboveThreshold, kColumn, (u32)nearRow);
                const f32 mirroredWith = Brightness(withGlow, kColumn, (u32)mirroredRow);

                FLUXION_LOG_INFO("RenderCoreTests",
                                 "%s: the square holds %.2f over rows %u..%u; three rows past it reads %.4f with no glow, "
                                 "%.4f with it, and %.4f once the threshold is above the square. Where its reflection "
                                 "would be: %.4f.",
                                 backendName, (f64)insideWithout, squareFirst, squareLast, (f64)nearWithout, (f64)nearWith,
                                 (f64)nearAbove, (f64)mirroredWith);

                // THE SQUARE IS THERE AT ALL. Everything below is about
                // pixels beside it, and all of it would pass on an empty
                // picture.
                TEST_CHECK(ctx, insideWithout > kEmissive * 0.5f);

                // AND NOTHING IS BESIDE IT UNTIL THE GLOW PUTS IT THERE.
                TEST_CHECK(ctx, nearWithout < 0.01f);

                // WHICH IS THE WHOLE POINT: light landing where the thing
                // that emitted it is not.
                TEST_CHECK(ctx, nearWith > nearWithout + 0.01f);

                // AND IT IS STRONGEST NEXT TO WHAT MADE IT. Where the
                // square's reflection would be, the glow must be weaker --
                // a chain that turned the picture over somewhere puts the
                // peak there instead, which is a fault that costs a whole
                // backend its picture and that a square in the MIDDLE of
                // the frame cannot see at all, because the middle is its
                // own reflection.
                TEST_CHECK(ctx, nearWith > mirroredWith * 1.5f);

                // AND IT IS THE SQUARE'S LIGHT, not a constant this pass
                // adds: raised above what the square emits, the threshold
                // leaves the picture as it was.
                TEST_CHECK(ctx, nearAbove < nearWithout + 0.01f);
            }
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

extern "C" void Test_BloomGPU_Run(TestContext* ctx)
{
    // A SURFACE THAT GLOWS NEEDS A SURFACE, and that needs a material
    // shader built here and now. Where nothing can build one, this says
    // so rather than reporting an empty picture as a failure -- the same
    // answer the lighting checks give on the same machines.
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the glow was NOT measured here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
