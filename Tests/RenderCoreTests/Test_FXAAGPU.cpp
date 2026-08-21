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

// A STAIRCASE SMOOTHED, WITHOUT THE PICTURE MOVING OR BLURRING.
//
// The same triangle twice: once straight to the screen, once through the
// pass that smooths it. Three things have to be true of the second, and
// only the first of them is what anybody thinks of as testing FXAA.
//
//   IT SMOOTHS. A rasterised edge is pixels that are entirely inside or
//   entirely outside -- there is nothing in between. Afterwards there
//   are values in between, and counting them is the measurement.
//
//   IT DOES NOT BLUR EVERYTHING. A pass that simply averaged the picture
//   would produce far MORE in-between pixels and would pass the first
//   check by a mile. So a pixel deep inside the triangle and one far
//   outside it have to come back exactly as they went in.
//
//   AND IT DOES NOT TURN THE PICTURE OVER. This is the one that looks
//   like paranoia and is not. On one of these three backends every pass
//   that writes the screen has to correct for which way its rows run, and
//   inserting a pass changes WHICH pass that is -- so the correction has
//   to move with it. Getting that wrong costs a whole backend its
//   picture, and nothing in the first two checks would notice: an
//   upside-down triangle has exactly as many smoothed edge pixels as a
//   right-way-up one.

extern "C" void FluxionForwardOpaquePass_Execute(FluxionRHICommandListHandle commandList, void* userData);
extern "C" void FluxionPostProcessPass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

constexpr u32 kWidth = 64;
constexpr u32 kHeight = 64;

// What the triangle sends out. One, so that with no camera and no curve
// the picture that comes back is ones inside and zeroes outside -- and
// anything else in it was put there by the pass being measured.
constexpr f32 kEmissive = 1.0f;

// What counts as neither inside nor outside. Well clear of both, so that
// a pixel only lands in this band because something blended it.
constexpr f32 kBetweenLow = 0.05f;
constexpr f32 kBetweenHigh = 0.95f;

struct FXAAVertex
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

// A TRIANGLE, AND A LOPSIDED ONE.
//
// A triangle because its long edge is neither a row nor a column, which
// is the only kind of edge a rasteriser leaves a staircase on -- an
// axis-aligned edge is already as smooth as it will ever be, and a test
// built on one would measure nothing.
//
// Lopsided because of the third check: it has to be possible to tell this
// shape from itself upside down, and anything symmetric about the middle
// row is not.
const FXAAVertex kTriangle[3] = {
    { { -0.9f, -0.9f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 0.9f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { -0.9f, 0.9f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

const u16 kTriangleIndices[3] = { 0, 1, 2 };

bool RenderOnce(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, FluxionRHICommandListHandle cmd,
                FluxionRendererHandle renderer, FluxionRenderTargetHandle target, FluxionRHITextureHandle color,
                FluxionRHITextureHandle depth, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                FluxionRenderPipelineHandle pipeline, FluxionRHIBufferHandle readback, bool smoothing, bool first,
                std::vector<f32>& outPixels)
{
    Fluxion_Renderer_SetFXAAEnabled(renderer, smoothing);

    FluxionRenderViewDesc viewSettings{};
    std::memset(&viewSettings, 0, sizeof(viewSettings));
    viewSettings.viewMatrix = TranslationOf(0.0f, 0.0f, -1.0f);
    viewSettings.projectionMatrix = Orthographic();
    viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
    viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
    viewSettings.renderTarget = target;
    viewSettings.layerMask = 0xFFFFFFFFu;
    viewSettings.ambientColor = FluxionVec3{ 0.0f, 0.0f, 0.0f };

    // NO CAMERA AND NO CURVE, so the picture is ones and zeroes and every
    // value between them was put there by the pass being measured.
    viewSettings.exposure = 1.0f;
    viewSettings.tonemapWhitePoint = 0.0f;
    viewSettings.encodeOutputToSRGB = false;

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

u32 CountBetween(const std::vector<f32>& pixels)
{
    u32 count = 0;
    for (u32 y = 0; y < kHeight; ++y)
    {
        for (u32 x = 0; x < kWidth; ++x)
        {
            const f32 value = Brightness(pixels, x, y);
            if (value > kBetweenLow && value < kBetweenHigh) ++count;
        }
    }
    return count;
}

// WHICH ROW THE PICTURE SITS ON, weighted by how bright each row is.
// One number that moves a long way if the picture is turned over and
// hardly at all if it is merely smoothed.
f32 BrightRow(const std::vector<f32>& pixels)
{
    f32 weighted = 0.0f;
    f32 total = 0.0f;
    for (u32 y = 0; y < kHeight; ++y)
    {
        for (u32 x = 0; x < kWidth; ++x)
        {
            const f32 value = Brightness(pixels, x, y);
            weighted += value * (f32)y;
            total += value;
        }
    }
    return total > 0.0f ? weighted / total : 0.0f;
}

// A pixel well inside the triangle and one well outside it, found by
// looking: which end of the texture the top of the frame is at differs
// between backends, and both of these have to be somewhere the triangle
// actually is.
bool FindDeepPixels(const std::vector<f32>& pixels, u32* outInsideX, u32* outInsideY, u32* outOutsideX, u32* outOutsideY)
{
    bool foundInside = false;
    bool foundOutside = false;

    // Two texels of margin in every direction, so that neither is near
    // enough to an edge for the smoothing to have any business there.
    const u32 margin = 2;
    for (u32 y = margin; y + margin < kHeight; ++y)
    {
        for (u32 x = margin; x + margin < kWidth; ++x)
        {
            bool allLit = true;
            bool allDark = true;
            for (u32 dy = 0; dy <= margin * 2u; ++dy)
            {
                for (u32 dx = 0; dx <= margin * 2u; ++dx)
                {
                    const f32 value = Brightness(pixels, x + dx - margin, y + dy - margin);
                    if (value < kBetweenHigh) allLit = false;
                    if (value > kBetweenLow) allDark = false;
                }
            }

            if (allLit && !foundInside)
            {
                *outInsideX = x;
                *outInsideY = y;
                foundInside = true;
            }
            if (allDark && !foundOutside)
            {
                *outOutsideX = x;
                *outOutsideY = y;
                foundOutside = true;
            }
        }
    }

    return foundInside && foundOutside;
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the smoothing was NOT measured on it.", backendName);
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
    colorDesc.debugName = "FXAAGPU.Color";
    FluxionRHITextureHandle color = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "FXAAGPU.Depth";
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
    meshDesc.vertexData = kTriangle;
    meshDesc.vertexDataSize = sizeof(kTriangle);
    meshDesc.indexData = kTriangleIndices;
    meshDesc.indexDataSize = sizeof(kTriangleIndices);
    meshDesc.use16BitIndices = true;
    meshDesc.vertexLayout.attributes[0].location = 0;
    meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[0].offset = offsetof(FXAAVertex, position);
    meshDesc.vertexLayout.attributes[1].location = 1;
    meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[1].offset = offsetof(FXAAVertex, normal);
    meshDesc.vertexLayout.attributes[2].location = 2;
    meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    meshDesc.vertexLayout.attributes[2].offset = offsetof(FXAAVertex, tangent);
    meshDesc.vertexLayout.attributes[3].location = 3;
    meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    meshDesc.vertexLayout.attributes[3].offset = offsetof(FXAAVertex, uv);
    meshDesc.vertexLayout.attributeCount = 4;
    meshDesc.vertexLayout.stride = sizeof(FXAAVertex);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.9f, -0.9f, 0.0f }, FluxionVec3{ 0.9f, 0.9f, 0.0f } };
    meshDesc.debugName = "FXAAGPU.Triangle";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);

    char* vertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_FORWARD);

    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "FXAAGPU.Standard";
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
    samplerDesc.debugName = "FXAAGPU.Sampler";
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);

    FluxionMaterialHandle material = Fluxion_Material_Create(device, program);
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
    Fluxion_Renderer_SetBloomEnabled(renderer, false);

    const usize rowBytes = (usize)kWidth * 4u * sizeof(u16);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                  FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
    FluxionRHIBufferDesc readbackDesc{ alignedRowBytes * kHeight, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "FXAAGPU.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);

    std::vector<f32> stepped;
    std::vector<f32> smoothed;

    const bool ok =
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback, false, true, stepped) &&
        RenderOnce(ctx, device, queue, cmd, renderer, target, color, depth, mesh, material, pipeline, readback, true, false, smoothed);

    if (ok)
    {
        const u32 steppedBetween = CountBetween(stepped);
        const u32 smoothedBetween = CountBetween(smoothed);
        const f32 steppedRow = BrightRow(stepped);
        const f32 smoothedRow = BrightRow(smoothed);

        u32 insideX = 0;
        u32 insideY = 0;
        u32 outsideX = 0;
        u32 outsideY = 0;
        const bool foundDeep = FindDeepPixels(stepped, &insideX, &insideY, &outsideX, &outsideY);

        FLUXION_LOG_INFO("RenderCoreTests",
                         "%s: the rasterised edge has %u pixels between black and white, the smoothed one %u; the "
                         "picture's weight sits on row %.2f before and %.2f after; deep inside it reads %.4f against "
                         "%.4f, and deep outside %.4f against %.4f.",
                         backendName, steppedBetween, smoothedBetween, (f64)steppedRow, (f64)smoothedRow,
                         (f64)Brightness(stepped, insideX, insideY), (f64)Brightness(smoothed, insideX, insideY),
                         (f64)Brightness(stepped, outsideX, outsideY), (f64)Brightness(smoothed, outsideX, outsideY));

        TEST_CHECK(ctx, foundDeep);

        // THERE IS A HARD EDGE TO BEGIN WITH. A rasteriser answers yes or
        // no, so almost nothing is in between -- and if that were not
        // true, the count below would be measuring something else.
        TEST_CHECK(ctx, steppedBetween < 16u);

        // AND AFTERWARDS THERE IS NOT. The long edge runs most of the way
        // across a sixty-four pixel frame, so a smoothed one puts values
        // in between along the whole of it.
        TEST_CHECK(ctx, smoothedBetween > 24u);

        // AND THE PICTURE DID NOT MOVE. Turning it over would shift this
        // by about ten rows on a frame this size -- see the triangle for
        // why it is lopsided.
        TEST_CHECK(ctx, std::fabs(smoothedRow - steppedRow) < 2.0f);

        if (foundDeep)
        {
            // AND NOTHING AWAY FROM AN EDGE WAS TOUCHED. This is what
            // separates smoothing from blurring, and a pass that averaged
            // the whole picture would fail here and nowhere else.
            TEST_CHECK(ctx, Brightness(smoothed, insideX, insideY) > kBetweenHigh);
            TEST_CHECK(ctx, Brightness(smoothed, outsideX, outsideY) < kBetweenLow);
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

extern "C" void Test_FXAAGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the smoothing was NOT measured here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
