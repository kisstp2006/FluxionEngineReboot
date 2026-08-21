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

// A CAMERA THAT SETS ITSELF, MEASURED BY WHAT IT CANCELS.
//
// The claim an automatic exposure makes is a strange one to test, because
// what it promises is that TWO DIFFERENT SCENES COME OUT THE SAME. So the
// same frame is drawn four times over: bright and dark with the camera
// fixed, and bright and dark with the camera setting itself. The first
// pair is the evidence that the two scenes really are different -- sixteen
// times apart -- and the second is the evidence that the camera closed
// that gap.
//
// Neither half means anything alone. A pass that returned a constant
// would pass the second check perfectly, and the first check is what
// catches it: the scenes have to differ before cancelling the difference
// is worth anything.
//
// And a fifth frame for the part that is easy to get wrong in the other
// direction. A camera that arrived at the right answer INSTANTLY would
// strobe every time a light moved -- so the last frame walks from a
// dark-adapted camera into the bright scene in one sixtieth of a second
// and checks that it is still mostly where it was.

extern "C" void FluxionForwardOpaquePass_Execute(FluxionRHICommandListHandle commandList, void* userData);
extern "C" void FluxionPostProcessPass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

constexpr u32 kWidth = 64;
constexpr u32 kHeight = 64;
constexpr u32 kColumn = 32;
constexpr u32 kRow = 32;

// SIXTEEN TIMES APART, and the quad covers the whole frame.
//
// Covering everything is not a convenience here, it is the measurement.
// The camera is set from the average of the picture, so a square on a
// black background would be measuring mostly the background -- and both
// scenes would then come out at the same near-black average and ask for
// the same exposure, which would look exactly like this test passing.
constexpr f32 kBright = 4.0f;
constexpr f32 kDark = 0.25f;

// One sixtieth of a second, which is the frame the easing check walks
// through. Long enough to be a real frame, far too short for a camera to
// cross sixteen times the light in.
constexpr f32 kOneFrame = 1.0f / 60.0f;

struct ExposureVertex
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

// THE WHOLE FRAME -- see the constants above for why that matters here
// and not in the tests beside this one.
const ExposureVertex kQuad[4] = {
    { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

const u16 kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

bool RenderOnce(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, FluxionRHICommandListHandle cmd,
                FluxionRendererHandle renderer, FluxionRenderTargetHandle target, FluxionRHITextureHandle color,
                FluxionRHITextureHandle depth, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                FluxionRenderPipelineHandle pipeline, FluxionRHIBufferHandle readback, f32 emissive, f32 deltaSeconds,
                bool first, std::vector<f32>& outPixels)
{
    Fluxion_Material_SetEmissive(material, FluxionVec3{ emissive, emissive, emissive });
    Fluxion_Material_FlushDirty(material);

    FluxionRenderViewDesc viewSettings{};
    std::memset(&viewSettings, 0, sizeof(viewSettings));
    viewSettings.viewMatrix = TranslationOf(0.0f, 0.0f, -1.0f);
    viewSettings.projectionMatrix = Orthographic();
    viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
    viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
    viewSettings.renderTarget = target;
    viewSettings.layerMask = 0xFFFFFFFFu;
    viewSettings.ambientColor = FluxionVec3{ 0.0f, 0.0f, 0.0f };

    // NO CURVE AT ALL, and that is what makes the numbers below readable.
    // A tone curve compresses everything towards one, which is exactly
    // the difference this test is trying to measure -- with it on, a
    // camera that did nothing whatever would look as though it had almost
    // worked.
    viewSettings.exposure = 1.0f;
    viewSettings.tonemapWhitePoint = 0.0f;
    viewSettings.encodeOutputToSRGB = false;
    viewSettings.deltaSeconds = deltaSeconds;

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
    Fluxion_Renderer_UpdateEnvironment(renderer, view, cmd);
    Fluxion_Renderer_SubmitRenderWorld(renderer, &world);
    Fluxion_Renderer_UploadScene(renderer, cmd);

    const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
    const FluxionRHIResourceState wasColor = first ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE;
    const FluxionRHIResourceState wasDepth = first ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE;
    const FluxionRHIResourceState wasScene = first ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_SHADER_READ;

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

f32 Brightness(const std::vector<f32>& pixels, u32 x, u32 y)
{
    const usize at = ((usize)y * kWidth + x) * 4u;
    if (at + 2 >= pixels.size()) return 0.0f;
    return (pixels[at] + pixels[at + 1] + pixels[at + 2]) / 3.0f;
}

// FORGETS WHERE THE CAMERA WAS, by turning the whole thing off and on
// again. What follows then measures the frame in front of it rather than
// easing towards it from a setting that belonged to a different picture
// -- which is the state a test wants when it is asking what a scene asks
// for, rather than how fast the camera gets there.
void StartAdaptingAfresh(FluxionRendererHandle renderer)
{
    Fluxion_Renderer_SetAutoExposureEnabled(renderer, false);
    Fluxion_Renderer_SetAutoExposureEnabled(renderer, true);
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the camera was NOT measured on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

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
    colorDesc.debugName = "AutoExposureGPU.Color";
    FluxionRHITextureHandle color = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "AutoExposureGPU.Depth";
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
    meshDesc.vertexLayout.attributes[0].offset = offsetof(ExposureVertex, position);
    meshDesc.vertexLayout.attributes[1].location = 1;
    meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[1].offset = offsetof(ExposureVertex, normal);
    meshDesc.vertexLayout.attributes[2].location = 2;
    meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    meshDesc.vertexLayout.attributes[2].offset = offsetof(ExposureVertex, tangent);
    meshDesc.vertexLayout.attributes[3].location = 3;
    meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    meshDesc.vertexLayout.attributes[3].offset = offsetof(ExposureVertex, uv);
    meshDesc.vertexLayout.attributeCount = 4;
    meshDesc.vertexLayout.stride = sizeof(ExposureVertex);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -1.0f, -1.0f, 0.0f }, FluxionVec3{ 1.0f, 1.0f, 0.0f } };
    meshDesc.debugName = "AutoExposureGPU.Quad";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);

    char* vertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_FORWARD);

    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "AutoExposureGPU.Standard";
    programDesc.vertexSource = vertexSource;
    programDesc.fragmentSource = fragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(device, &programDesc);
    Fluxion_MaterialShader_FreeSource(vertexSource);
    Fluxion_MaterialShader_FreeSource(fragmentSource);

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
    samplerDesc.debugName = "AutoExposureGPU.Sampler";
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);

    FluxionMaterialHandle material = Fluxion_Material_Create(device, program);
    Fluxion_Material_SetBaseColor(material, FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_BASE_COLOR, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_METALLIC_ROUGHNESS, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_NORMAL, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_OCCLUSION, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_EMISSIVE, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    Fluxion_Renderer_SetOutputColorFormat(renderer, outputFormat);
    Fluxion_Renderer_SetPostProcessEnabled(renderer, true);
    Fluxion_Renderer_SetBloomEnabled(renderer, false);

    const usize rowBytes = (usize)kWidth * 4u * sizeof(u16);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                  FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
    FluxionRHIBufferDesc readbackDesc{ alignedRowBytes * kHeight, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "AutoExposureGPU.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);

    std::vector<f32> brightFixed;
    std::vector<f32> darkFixed;
    std::vector<f32> brightAdapted;
    std::vector<f32> darkAdapted;
    std::vector<f32> brightOneFrameLater;

    // --- the camera held still ------------------------------------------
    Fluxion_Renderer_SetAutoExposureEnabled(renderer, false);
    bool ok = RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                         kBright, 0.0f, true, brightFixed) &&
              RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                         kDark, 0.0f, false, darkFixed);

    // --- and the camera setting itself, each scene from a clean start ----
    //
    // A FRAME OF NO LENGTH, which is how a caller says "do not ease". Both
    // of these are therefore the answer the scene in front of the camera
    // asks for, with nothing of the frame before it left in them.
    if (ok)
    {
        StartAdaptingAfresh(renderer);
        ok = RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                        kBright, 0.0f, false, brightAdapted);
    }
    if (ok)
    {
        StartAdaptingAfresh(renderer);
        ok = RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                        kDark, 0.0f, false, darkAdapted);
    }

    // --- and one frame of walking out of the dark ------------------------
    //
    // NO RESET BEFORE THIS ONE, and that is the whole point of it: the
    // camera is still set for the dark scene above, and a sixtieth of a
    // second is not long enough to leave it.
    if (ok)
    {
        ok = RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback,
                        kBright, kOneFrame, false, brightOneFrameLater);
    }

    if (ok)
    {
        const f32 brightHeld = Brightness(brightFixed, kColumn, kRow);
        const f32 darkHeld = Brightness(darkFixed, kColumn, kRow);
        const f32 brightSet = Brightness(brightAdapted, kColumn, kRow);
        const f32 darkSet = Brightness(darkAdapted, kColumn, kRow);
        const f32 walkedIn = Brightness(brightOneFrameLater, kColumn, kRow);

        FLUXION_LOG_INFO("RenderCoreTests",
                         "%s: held still the two scenes read %.4f and %.4f; setting itself the camera brings them to "
                         "%.4f and %.4f; and one frame after the dark scene the bright one still reads %.4f.",
                         backendName, (f64)brightHeld, (f64)darkHeld, (f64)brightSet, (f64)darkSet, (f64)walkedIn);

        // THE TWO SCENES REALLY ARE DIFFERENT. Everything below is about
        // a difference being cancelled, and all of it would pass on two
        // scenes that were the same to begin with.
        TEST_CHECK(ctx, darkHeld > 0.01f);
        TEST_CHECK(ctx, brightHeld > darkHeld * 4.0f);

        // AND THE CAMERA CANCELS IT. Not exactly -- a measurement taken
        // through a chain of averages is not an equation -- but the two
        // land within a quarter of each other where they were sixteen
        // times apart.
        TEST_CHECK(ctx, darkSet > 0.01f);
        TEST_CHECK(ctx, brightSet < darkSet * 1.25f);
        TEST_CHECK(ctx, darkSet < brightSet * 1.25f);

        // AND IT DOES NOT DO IT INSTANTLY. One frame out of the dark and
        // the picture is still far too bright, because the camera is
        // still mostly set for the room it just left. A pass that snapped
        // to the answer would put this at the same place as the line
        // above -- and would strobe every time a light moved.
        TEST_CHECK(ctx, walkedIn > brightSet * 4.0f);
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

extern "C" void Test_AutoExposureGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the camera was NOT measured here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
