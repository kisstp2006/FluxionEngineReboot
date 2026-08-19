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
#include <Fluxion/RenderCore/Scene/RenderWorld.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cmath>
#include <cstring>
#include <vector>

// WHERE A PIXEL WAS, READ BACK.
//
// A motion vector is the one number in this engine that is about two
// frames at once, so it cannot be checked by looking at one: the same
// object is drawn twice, moved between the two, and what the second frame
// wrote is compared against where the first frame put it.
//
// THE SIGN AND THE SCALE ARE THE POINT. A vector that pointed the other
// way, or that was twice as long as it should be, would still look like a
// plausible picture in every other test -- and would send everything
// downstream (reprojection, accumulation) to the wrong pixel.

extern "C" void FluxionMotionVectorPass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

struct MotionVertex
{
    f32 position[3];
};

// A quad across the middle of the screen, in the plane z = 0.
const MotionVertex kQuad[4] = {
    { { -0.5f, -0.5f, 0.0f } },
    { { 0.5f, -0.5f, 0.0f } },
    { { 0.5f, 0.5f, 0.0f } },
    { { -0.5f, 0.5f, 0.0f } },
};

const u16 kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

const u32 kWidth = 64;
const u32 kHeight = 64;

FluxionMat4 TranslationOf(f32 x, f32 y, f32 z)
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}

// Straight down -Z from the origin, with no perspective at all: an
// orthographic view keeps the arithmetic between world units and screen
// units something the test can state outright.
FluxionMat4 Orthographic()
{
    FluxionMat4 m = Fluxion_Mat4_Identity();

    // Two world units across the screen in each direction, and depth
    // mapped so that the quad at z = -1 sits in the middle of the range.
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = -0.25f;
    m.m[2][3] = 0.5f;
    return m;
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the motion vectors were NOT read back on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

    // --- what the frame is drawn into ------------------------------------

    FluxionRHITextureDesc colorDesc{};
    colorDesc.width = kWidth;
    colorDesc.height = kHeight;
    colorDesc.depth = 1;
    colorDesc.mipLevels = 1;
    colorDesc.arrayLayers = 1;
    colorDesc.sampleCount = 1;
    colorDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    colorDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    colorDesc.debugName = "MotionVectorGPU.Color";
    FluxionRHITextureHandle colorTexture = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "MotionVectorGPU.Depth";
    FluxionRHITextureHandle depthTexture = Fluxion_RHI_CreateTexture(device, &depthDesc);

    FluxionRHITextureViewDesc viewDesc{};
    viewDesc.texture = colorTexture;
    viewDesc.format = colorDesc.format;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    FluxionRHITextureViewHandle colorView = Fluxion_RHI_CreateTextureView(device, &viewDesc);

    viewDesc.texture = depthTexture;
    viewDesc.format = depthDesc.format;
    FluxionRHITextureViewHandle depthTextureView = Fluxion_RHI_CreateTextureView(device, &viewDesc);

    FluxionRenderTargetDesc targetDesc{};
    targetDesc.colorViews[0] = colorView;
    targetDesc.colorViewCount = 1;
    targetDesc.depthView = depthTextureView;
    FluxionRenderTargetHandle target = Fluxion_RenderTarget_Create(device, &targetDesc);

    // --- the mesh, and a renderer to draw it with -------------------------

    FluxionMeshBufferDesc meshDesc{};
    std::memset(&meshDesc, 0, sizeof(meshDesc));
    meshDesc.vertexData = kQuad;
    meshDesc.vertexDataSize = sizeof(kQuad);
    meshDesc.indexData = kQuadIndices;
    meshDesc.indexDataSize = sizeof(kQuadIndices);
    meshDesc.use16BitIndices = true;
    meshDesc.vertexLayout.attributes[0].location = 0;
    meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[0].offset = 0;
    meshDesc.vertexLayout.attributeCount = 1;
    meshDesc.vertexLayout.stride = sizeof(MotionVertex);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.5f, -0.5f, 0.0f }, FluxionVec3{ 0.5f, 0.5f, 0.0f } };
    meshDesc.debugName = "MotionVectorGPU.Quad";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderer));

    const usize readbackSize = (usize)kWidth * kHeight * 2 * sizeof(f32);
    FluxionRHIBufferDesc readbackDesc{ readbackSize, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU,
                                       "MotionVectorGPU.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);

    // --- two frames, with the quad moved between them ---------------------
    //
    // The FIRST frame has no history, so what it writes is not the answer;
    // it is what gives the second frame a past.
    const f32 kShift = 0.25f;
    bool historyAfterFirst = true;

    for (u32 frame = 0; frame < 2; ++frame)
    {
        FluxionRenderViewDesc viewSettings{};
        std::memset(&viewSettings, 0, sizeof(viewSettings));
        viewSettings.viewMatrix = TranslationOf(0.0f, 0.0f, -1.0f);
        viewSettings.projectionMatrix = Orthographic();
        viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
        viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
        viewSettings.renderTarget = target;
        viewSettings.layerMask = 0xFFFFFFFFu;

        FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewSettings);
        Fluxion_RenderView_UpdateFrameConstants(view);

        Fluxion_Renderer_BeginFrame(renderer, view);
        if (frame == 0) historyAfterFirst = Fluxion_Renderer_IsHistoryValid(renderer);

        // MOVED ALONG X BETWEEN THE TWO FRAMES, and the previous
        // placement is where it was in the frame before -- which for the
        // first frame is where it is.
        const FluxionMat4 previous = TranslationOf(0.0f, 0.0f, 0.0f);
        const FluxionMat4 current = frame == 0 ? previous : TranslationOf(kShift, 0.0f, 0.0f);

        FluxionRenderWorld world{};
        TEST_CHECK(ctx, Fluxion_RenderWorld_Init(&world));

        FluxionRenderObject object{};
        std::memset(&object, 0, sizeof(object));
        object.transform = current;
        object.previousTransform = previous;
        object.mesh = mesh;
        object.material = Fluxion::Foundation::NoHandle<FluxionMaterialHandle>();
        object.pipeline = Fluxion::Foundation::NoHandle<FluxionRenderPipelineHandle>();
        object.layerMask = 0xFFFFFFFFu;
        object.visible = true;
        Fluxion_RenderWorld_AddObject(&world, &object);

        Fluxion_RHI_CommandList_Begin(cmd);
        Fluxion_Renderer_SubmitRenderWorld(renderer, &world);
        Fluxion_Renderer_UploadScene(renderer, cmd);

        // The depth this frame's motion pass tests against. Nothing else
        // draws here, so it is cleared and left empty -- a pass that only
        // TESTS depth needs a depth buffer that says "nothing is in front
        // of anything", and a rendering with no draws in it is how this
        // contract spells a clear.
        FluxionRHIRenderingAttachment clearDepth;
        clearDepth.view = depthTextureView;
        clearDepth.clear = true;
        clearDepth.clearColor[0] = 1.0f;

        // And the depth, which starts in no state at all as well.
        const FluxionRHIBufferHandle noBufferForDepth = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
        FluxionRHIBarrier depthToWrite = { depthTexture, noBufferForDepth,
                                           frame == 0 ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE,
                                           FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &depthToWrite, 1);

        FluxionRHIRenderingDesc clearDesc;
        clearDesc.colorAttachments = nullptr;
        clearDesc.colorAttachmentCount = 0;
        clearDesc.depthAttachment = &clearDepth;
        clearDesc.width = kWidth;
        clearDesc.height = kHeight;
        Fluxion_RHI_CommandList_BeginRendering(cmd, &clearDesc);
        Fluxion_RHI_CommandList_EndRendering(cmd);

        // What the render graph does for this pass in a real frame, done
        // by hand here: the target has to be attachable before anything
        // attaches it, and on the first frame it is in no state at all.
        const FluxionRHIBufferHandle noBufferForLayout = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
        FluxionRHIBarrier toTarget = { Fluxion_Renderer_GetMotionVectorTexture(renderer), noBufferForLayout,
                                       frame == 0 ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                       FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &toTarget, 1);

        FluxionMotionVectorPass_Execute(cmd, Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

        // And back, so this side can read what the device wrote.
        const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
        FluxionRHIBarrier toSource = { Fluxion_Renderer_GetMotionVectorTexture(renderer), noBuffer,
                                       FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);
        Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, Fluxion_Renderer_GetMotionVectorTexture(renderer), 0, 0, readback, 0);

        FluxionRHIBarrier backAgain = { Fluxion_Renderer_GetMotionVectorTexture(renderer), noBuffer,
                                        FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &backAgain, 1);

        Fluxion_Renderer_EndFrame(renderer, cmd);
        Fluxion_RHI_CommandList_End(cmd);

        FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
        Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
        TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));
        Fluxion_RHI_DestroyFence(fence);

        Fluxion_RenderWorld_Shutdown(&world);
        Fluxion_RenderView_Destroy(view);
    }

    // A first frame has nothing behind it, and says so.
    TEST_CHECK(ctx, !historyAfterFirst);
    TEST_CHECK(ctx, Fluxion_Renderer_IsHistoryValid(renderer));

    // --- what the second frame wrote --------------------------------------
    //
    // The quad moved a quarter of a world unit along +X, and this view maps
    // one world unit to one clip unit, so on the screen it moved a quarter
    // of the way from the middle to the edge. What a motion vector says is
    // where the pixel WAS, in the coordinates a texture is read by -- half
    // of a clip distance -- so the answer is -0.125 in x and nothing in y.
    const f32 kExpectedX = -kShift * 0.5f;

    if (const f32* pixels = (const f32*)Fluxion_RHI_MapBuffer(readback))
    {
        // The centre of the screen, which the quad covers in both frames.
        const usize centre = ((usize)(kHeight / 2) * kWidth + (kWidth / 2)) * 2;
        const f32 x = pixels[centre];
        const f32 y = pixels[centre + 1];

        FLUXION_LOG_INFO("RenderCoreTests", "%s: the centre pixel moved by (%.4f, %.4f); expected (%.4f, 0.0000).", backendName, (f64)x,
                         (f64)y, (f64)kExpectedX);

        // A TENTH OF THE ANSWER as the tolerance, not a hair: what this is
        // checking is the direction and the scale, and a vector that was
        // the wrong way round or twice too long is what would break
        // everything downstream. Half-precision arithmetic in the middle
        // of a triangle does not land on a fifth decimal.
        const f32 tolerance = 0.1f * (kExpectedX < 0.0f ? -kExpectedX : kExpectedX);
        TEST_CHECK(ctx, std::fabs(x - kExpectedX) < tolerance);
        TEST_CHECK(ctx, std::fabs(y) < tolerance);

        // A corner, which the quad covers in neither frame: nothing was
        // drawn there, and nothing drawn is no motion rather than whatever
        // the buffer held.
        TEST_CHECK(ctx, pixels[0] == 0.0f && pixels[1] == 0.0f);

        Fluxion_RHI_UnmapBuffer(readback);
    }
    else
    {
        TEST_CHECK(ctx, false);
    }

    Fluxion_RHI_DestroyBuffer(readback);
    Fluxion_Renderer_Destroy(renderer);
    Fluxion_MeshBuffer_Destroy(mesh);
    Fluxion_RenderTarget_Destroy(target);
    Fluxion_RHI_DestroyTextureView(depthTextureView);
    Fluxion_RHI_DestroyTextureView(colorView);
    Fluxion_RHI_DestroyTexture(depthTexture);
    Fluxion_RHI_DestroyTexture(colorTexture);
    Fluxion_RenderGraphPassRegistry_Shutdown();
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_MotionVectorGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the motion vectors were NOT checked here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
