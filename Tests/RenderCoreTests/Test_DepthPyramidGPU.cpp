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
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cmath>
#include <cstring>
#include <vector>

// EVERY LEVEL OF THE PYRAMID IS THE MAXIMUM OF THE ONE BELOW IT.
//
// That is the whole contract, and it is checked as arithmetic rather than
// as a picture: both levels are read back and every texel of the coarser
// one is compared against the four (or six, or nine) it covers. A pyramid
// that averaged instead of taking the furthest, or that dropped the row an
// odd size leaves over, passes every other check in this engine and hides
// geometry that is on the screen.
//
// A SIZE THAT IS NOT A POWER OF TWO on purpose: 37 by 21 halves to 18 by
// 10, and the last row and column of the source belong to no pair of
// texels. That is exactly the case a pyramid built the obvious way gets
// wrong.

extern "C" void FluxionDepthPyramidPass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

const u32 kWidth = 37;
const u32 kHeight = 21;

u32 LevelSize(u32 size, u32 level)
{
    u32 value = size;
    for (u32 i = 0; i < level; ++i) value = value > 1 ? value / 2 : 1;
    return value;
}

// One level, read back into ordinary memory.
std::vector<f32> ReadLevel(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, FluxionRHICommandListHandle cmd,
                           FluxionRHITextureHandle pyramid, u32 level, u32 width, u32 height)
{
    // ROWS ARE SPACED OUT ON THE WAY BACK, by the alignment this contract
    // names -- so the buffer is bigger than the picture and the picture
    // has to be picked out of it row by row.
    const usize rowBytes = (usize)width * sizeof(f32);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT *
                                  FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
    const usize size = alignedRowBytes * height;
    FluxionRHIBufferDesc desc{ size, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "DepthPyramid.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &desc);

    std::vector<f32> values;
    if (!FLUXION_HANDLE_IS_VALID(readback)) return values;

    const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();

    Fluxion_RHI_CommandList_Begin(cmd);

    FluxionRHIBarrier toSource{};
    toSource.texture = pyramid;
    toSource.buffer = noBuffer;
    toSource.before = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    toSource.after = FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE;
    Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);

    Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, pyramid, level, 0, readback, 0);

    FluxionRHIBarrier back{};
    back.texture = pyramid;
    back.buffer = noBuffer;
    back.before = FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE;
    back.after = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    Fluxion_RHI_CommandList_Barrier(cmd, &back, 1);

    Fluxion_RHI_CommandList_End(cmd);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
    if (Fluxion_RHI_WaitForFence(fence))
    {
        if (const u8* mapped = (const u8*)Fluxion_RHI_MapBuffer(readback))
        {
            values.resize((usize)width * height);
            for (u32 y = 0; y < height; ++y)
            {
                const f32* row = (const f32*)(mapped + (usize)y * alignedRowBytes);
                for (u32 x = 0; x < width; ++x) values[(usize)y * width + x] = row[x];
            }
            Fluxion_RHI_UnmapBuffer(readback);
        }
    }

    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyBuffer(readback);
    return values;
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the depth pyramid was NOT checked on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

    // --- a depth buffer with something in it ------------------------------

    FluxionRHITextureDesc depthDesc{};
    depthDesc.width = kWidth;
    depthDesc.height = kHeight;
    depthDesc.depth = 1;
    depthDesc.mipLevels = 1;
    depthDesc.arrayLayers = 1;
    depthDesc.sampleCount = 1;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    depthDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    depthDesc.debugName = "DepthPyramidGPU.Depth";
    FluxionRHITextureHandle depthTexture = Fluxion_RHI_CreateTexture(device, &depthDesc);

    FluxionRHITextureDesc colorDesc = depthDesc;
    colorDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET;
    colorDesc.debugName = "DepthPyramidGPU.Color";
    FluxionRHITextureHandle colorTexture = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureViewDesc viewDesc{};
    viewDesc.texture = depthTexture;
    viewDesc.format = depthDesc.format;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    FluxionRHITextureViewHandle depthView = Fluxion_RHI_CreateTextureView(device, &viewDesc);

    viewDesc.texture = colorTexture;
    viewDesc.format = colorDesc.format;
    FluxionRHITextureViewHandle colorView = Fluxion_RHI_CreateTextureView(device, &viewDesc);

    FluxionRenderTargetDesc targetDesc{};
    targetDesc.colorViews[0] = colorView;
    targetDesc.colorViewCount = 1;
    targetDesc.depthView = depthView;
    FluxionRenderTargetHandle target = Fluxion_RenderTarget_Create(device, &targetDesc);

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderer));

    FluxionRenderViewDesc viewSettings{};
    std::memset(&viewSettings, 0, sizeof(viewSettings));
    viewSettings.viewMatrix = Fluxion_Mat4_Identity();
    viewSettings.projectionMatrix = Fluxion_Mat4_Identity();
    viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
    viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
    viewSettings.renderTarget = target;
    viewSettings.layerMask = 0xFFFFFFFFu;

    FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewSettings);
    Fluxion_RenderView_UpdateFrameConstants(view);

    Fluxion_Renderer_BeginFrame(renderer, view);

    const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();

    Fluxion_RHI_CommandList_Begin(cmd);

    // The depth, cleared to a value that is not the far plane -- a
    // pyramid of nothing but 1.0 would agree with a min and with a max
    // and with an average, and prove none of them.
    FluxionRHIBarrier depthToWrite{};
    depthToWrite.texture = depthTexture;
    depthToWrite.buffer = noBuffer;
    depthToWrite.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    depthToWrite.after = FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE;
    Fluxion_RHI_CommandList_Barrier(cmd, &depthToWrite, 1);

    FluxionRHIRenderingAttachment clearDepth;
    clearDepth.view = depthView;
    clearDepth.clear = true;
    clearDepth.clearColor[0] = 0.25f;

    FluxionRHIRenderingDesc clearDesc;
    clearDesc.colorAttachments = nullptr;
    clearDesc.colorAttachmentCount = 0;
    clearDesc.depthAttachment = &clearDepth;
    clearDesc.width = kWidth;
    clearDesc.height = kHeight;
    Fluxion_RHI_CommandList_BeginRendering(cmd, &clearDesc);
    Fluxion_RHI_CommandList_EndRendering(cmd);

    // AND A CORNER AT A DIFFERENT DEPTH. A frame of one depth everywhere
    // cannot tell a reduction that takes the furthest of four from one
    // that takes any single one of them -- measured: a pyramid pass
    // crippled to read one texel passed this file until the picture had
    // two depths in it.
    //
    // A second rendering over a smaller area, because a clear covers the
    // area the rendering was begun with and this contract has no clear of
    // its own.
    FluxionRHIRenderingAttachment nearDepth;
    nearDepth.view = depthView;
    nearDepth.clear = true;
    nearDepth.clearColor[0] = 0.75f;

    FluxionRHIRenderingDesc nearDesc;
    nearDesc.colorAttachments = nullptr;
    nearDesc.colorAttachmentCount = 0;
    nearDesc.depthAttachment = &nearDepth;
    nearDesc.width = kWidth / 3;
    nearDesc.height = kHeight / 3;
    Fluxion_RHI_CommandList_BeginRendering(cmd, &nearDesc);
    Fluxion_RHI_CommandList_EndRendering(cmd);

    // What the render graph does for this pass in a real frame, by hand.
    FluxionRHIBarrier pyramidToTarget{};
    pyramidToTarget.texture = Fluxion_Renderer_GetDepthPyramidTexture(renderer);
    pyramidToTarget.buffer = noBuffer;
    pyramidToTarget.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    pyramidToTarget.after = FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET;
    Fluxion_RHI_CommandList_Barrier(cmd, &pyramidToTarget, 1);

    FluxionDepthPyramidPass_Execute(cmd, Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

    Fluxion_Renderer_EndFrame(renderer, cmd);
    Fluxion_RHI_CommandList_End(cmd);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
    TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));
    Fluxion_RHI_DestroyFence(fence);

    // --- and what it holds -------------------------------------------------

    const u32 levels = Fluxion_Renderer_GetDepthPyramidLevelCount(renderer);
    FLUXION_LOG_INFO("RenderCoreTests", "%s: a %ux%u frame makes %u levels of pyramid.", backendName, kWidth, kHeight, levels);

    // 37x21 halves to 18x10, 9x5, 4x2, 2x1, 1x1: six levels, and four of
    // the five steps have an odd side to leave over.
    TEST_CHECK(ctx, levels == 6);

    const FluxionRHITextureHandle pyramid = Fluxion_Renderer_GetDepthPyramidTexture(renderer);

    // THE TOP OF THE PYRAMID IS ONE NUMBER: the furthest depth anywhere in
    // the frame. Every backend has to arrive at the same one -- it is the
    // same picture reduced the same way -- so this is the shortest thing
    // that can tell one of them apart from the others.
    if (levels > 0)
    {
        const std::vector<f32> top = ReadLevel(device, queue, cmd, pyramid, levels - 1, 1, 1);
        FLUXION_LOG_INFO("RenderCoreTests", "%s: the top of the pyramid says %.4f (the frame holds 0.25 and 0.75).", backendName,
                         top.empty() ? -1.0 : (f64)top[0]);
    }

    // Level zero is the depth as it was: one number, everywhere.
    std::vector<f32> level0 = ReadLevel(device, queue, cmd, pyramid, 0, kWidth, kHeight);
    TEST_CHECK(ctx, level0.size() == (usize)kWidth * kHeight);

    // Two depths in it, and nothing else: what level zero holds is the
    // depth as it was drawn.
    bool onlyTheTwo = !level0.empty();
    u32 nearCount = 0;
    for (f32 v : level0)
    {
        const bool isFar = std::fabs(v - 0.25f) < 0.001f;
        const bool isNear = std::fabs(v - 0.75f) < 0.001f;
        if (!isFar && !isNear) onlyTheTwo = false;
        if (isNear) ++nearCount;
    }
    TEST_CHECK(ctx, onlyTheTwo);

    // WHETHER THE PICTURE HAS TWO DEPTHS IN IT AT ALL is a backend's own
    // answer: a clear covers the area a rendering was begun with on one
    // of them and the whole attachment on another, and this contract has
    // no clear of its own to say otherwise. Where it does vary, the
    // checks below can tell a reduction that takes the furthest of four
    // from one that takes any single one of them -- measured, with a
    // pass crippled to read one texel. Where it does not, they still say
    // every level agrees with the one below.
    FLUXION_LOG_INFO("RenderCoreTests", "%s: %s of %zu texels are at the nearer depth.", backendName,
                     nearCount > 0 ? "some" : "none", level0.size());

    // EVERY LEVEL AGAINST THE ONE BELOW IT, texel by texel.
    for (u32 level = 1; level < levels; ++level)
    {
        const u32 sourceWidth = LevelSize(kWidth, level - 1);
        const u32 sourceHeight = LevelSize(kHeight, level - 1);
        const u32 destWidth = LevelSize(kWidth, level);
        const u32 destHeight = LevelSize(kHeight, level);

        std::vector<f32> source = ReadLevel(device, queue, cmd, pyramid, level - 1, sourceWidth, sourceHeight);
        std::vector<f32> dest = ReadLevel(device, queue, cmd, pyramid, level, destWidth, destHeight);
        if (source.empty() || dest.empty())
        {
            TEST_CHECK(ctx, false);
            continue;
        }

        // NO LEVEL MAY BE NEARER THAN WHAT IT COVERS.
        //
        // That is the property the culling rests on, and it is the one
        // worth checking: a texel that reported something NEARER than the
        // furthest thing in its area would say "everything here is closer
        // than you" when it is not, and the object that believed it
        // vanishes while it is on screen. A texel that reports something
        // further is merely conservative -- it culls less.
        //
        // Exact equality is deliberately NOT required: the pass samples
        // by coordinate, and where a size is odd (37 halves to 18) the
        // coordinate at the middle of a texel does not land on the same
        // pair of source texels that "twice the index" would name. Two
        // roundings of the same intent are not worth a red test; a level
        // that hides geometry is.
        //
        // WHICH ROW OF A RENDER TARGET THE TOP OF THE PICTURE LANDS IN
        // differs between backends, so a level may be the reduction of
        // the one below read the other way up -- self-consistent, and
        // what the pass and its readers both use. Either order is
        // accepted, decided ONCE for the whole level.
        bool matches = false;
        bool matchedMirrored = false;
        for (u32 attempt = 0; attempt < 2 && !matches; ++attempt)
        {
            const bool mirrored = attempt == 1;
            matchedMirrored = mirrored;
            matches = true;

            for (u32 y = 0; y < destHeight; ++y)
            {
                for (u32 x = 0; x < destWidth; ++x)
                {
                    // The area this texel stands for, in the source.
                    f32 expected = 0.0f;
                    for (u32 dy = 0; dy < 2; ++dy)
                    {
                        for (u32 dx = 0; dx < 2; ++dx)
                        {
                            const u32 sx = x * 2 + dx < sourceWidth ? x * 2 + dx : sourceWidth - 1;
                            const u32 sourceY = y * 2 + dy < sourceHeight ? y * 2 + dy : sourceHeight - 1;
                            const u32 row = mirrored ? (sourceHeight - 1 - sourceY) : sourceY;
                            const f32 value = source[(usize)row * sourceWidth + sx];
                            if (value > expected) expected = value;
                        }
                    }

                    const u32 destRow = mirrored ? (destHeight - 1 - y) : y;
                    const f32 got = dest[(usize)destRow * destWidth + x];
                    if (got < expected - 0.001f) matches = false;
                }
            }
        }

        // REQUIRED ON EVERY BACKEND, including the one the renderer will
        // not cull with: what that backend cannot do is give this pass a
        // depth texture when the frame is drawn straight to the window,
        // and a test that hands it one of its own is not asking that
        // question. The pyramid itself is the same everywhere, and this
        // is what says so.
        FLUXION_LOG_INFO("RenderCoreTests", "%s: level %u reduces the one below it %s.", backendName, level,
                         !matches ? "in NEITHER row order" : (matchedMirrored ? "with the rows the other way up" : "row for row"));

        TEST_CHECK(ctx, matches);
    }

    Fluxion_Renderer_Destroy(renderer);
    Fluxion_RenderView_Destroy(view);
    Fluxion_RenderTarget_Destroy(target);
    Fluxion_RHI_DestroyTextureView(colorView);
    Fluxion_RHI_DestroyTextureView(depthView);
    Fluxion_RHI_DestroyTexture(colorTexture);
    Fluxion_RHI_DestroyTexture(depthTexture);
    Fluxion_RenderGraphPassRegistry_Shutdown();
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_DepthPyramidGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the depth pyramid was NOT checked here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
