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
#include <Fluxion/RenderCore/Renderer/GPUScene.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Scene/RenderWorld.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstring>
#include <vector>

// WHAT WAS ALREADY HIDDEN IS NOT DRAWN AGAIN.
//
// The occlusion test is the one part of the culling that cannot be
// checked from a single frame: it reads the depth pyramid of the frame
// BEFORE, so what it decides depends on what was drawn then. So this
// draws three frames -- one to fill the depth, one for the pyramid to
// have been built from it, and one to be culled by it -- and asks how
// many objects survived.
//
// The depth is CLEARED to a value rather than drawn into: a flat sheet at
// a known depth is a wall whose distance the test can state outright, and
// it makes the answer arithmetic rather than a matter of what a triangle
// happened to cover.

extern "C" void FluxionDepthPyramidPass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

const u32 kWidth = 256;
const u32 kHeight = 256;

// The wall, in the depth the frame is cleared to. Anything the test finds
// further away than this was behind it.
const f32 kWallDepth = 0.35f;

struct OcclusionVertex
{
    f32 position[3];
};

const OcclusionVertex kQuad[4] = {
    { { -0.2f, -0.2f, 0.0f } },
    { { 0.2f, -0.2f, 0.0f } },
    { { 0.2f, 0.2f, 0.0f } },
    { { -0.2f, 0.2f, 0.0f } },
};

const u16 kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

FluxionMat4 TranslationOf(f32 x, f32 y, f32 z)
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}

// Straight down -Z, no perspective, and depth mapped so that world z runs
// from 0 at the near plane to 1 at the far one -- an object at z = -0.6
// sits at 0.2, in front of the wall; one at z = 0.4 sits at 0.7, behind
// it.
FluxionMat4 Orthographic()
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = 0.5f;
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the occlusion culling was NOT checked on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

    FluxionRHITextureDesc colorDesc{};
    colorDesc.width = kWidth;
    colorDesc.height = kHeight;
    colorDesc.depth = 1;
    colorDesc.mipLevels = 1;
    colorDesc.arrayLayers = 1;
    colorDesc.sampleCount = 1;
    colorDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET;
    colorDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    colorDesc.debugName = "OcclusionCullGPU.Color";
    FluxionRHITextureHandle colorTexture = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    depthDesc.debugName = "OcclusionCullGPU.Depth";
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
    meshDesc.vertexLayout.stride = sizeof(OcclusionVertex);

    // Small bounds: a wide sphere would reach past the wall on its near
    // side and be kept, which is right and would prove nothing here.
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.05f, -0.05f, -0.05f }, FluxionVec3{ 0.05f, 0.05f, 0.05f } };
    meshDesc.debugName = "OcclusionCullGPU.Quad";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderer));
    Fluxion_Renderer_SetCullMode(renderer, FLUXION_RENDERER_CULL_ON_DEVICE);

    const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();

    // Three frames: the first has no history at all, the second has a
    // pyramid built from the first, and the third is the one whose count
    // comes back (the device's answer is a frame behind -- see
    // Fluxion_GPUScene_GetVisibleCount).
    for (u32 frame = 0; frame < 4; ++frame)
    {
        FluxionRenderViewDesc viewSettings{};
        std::memset(&viewSettings, 0, sizeof(viewSettings));
        viewSettings.viewMatrix = Fluxion_Mat4_Identity();
        viewSettings.projectionMatrix = Orthographic();
        viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
        viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
        viewSettings.renderTarget = target;
        viewSettings.layerMask = 0xFFFFFFFFu;

        FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewSettings);
        Fluxion_RenderView_UpdateFrameConstants(view);

        Fluxion_Renderer_BeginFrame(renderer, view);

        FluxionRenderWorld world{};
        TEST_CHECK(ctx, Fluxion_RenderWorld_Init(&world));

        // ONE BEHIND THE WALL, AND SEVERAL IN FRONT OF IT, spread across
        // the picture rather than all in the middle.
        //
        // The spread is the point: a test whose objects all sit at the
        // centre of the screen cannot tell a reader that looks in the
        // right place from one that looks in the wrong place -- and
        // looking in the wrong place is exactly what a picture read the
        // other way up does. What that costs is everything: measured, in
        // a scene of 784 cubes, it left one.
        struct Placement
        {
            f32 x, y, z;
            bool hidden;
        };

        const Placement placements[] = {
            { 0.0f, 0.0f, 0.4f, true },    // behind the wall
            { 0.0f, 0.0f, -0.6f, false },  // and in front of it, everywhere
            { -0.6f, -0.6f, -0.6f, false }, { 0.6f, -0.6f, -0.6f, false },
            { -0.6f, 0.6f, -0.6f, false },  { 0.6f, 0.6f, -0.6f, false },
            { 0.0f, 0.6f, -0.6f, false },   { 0.0f, -0.6f, -0.6f, false },
        };

        for (const Placement& placement : placements)
        {
            FluxionRenderObject object{};
            std::memset(&object, 0, sizeof(object));
            object.transform = TranslationOf(placement.x, placement.y, placement.z);
            object.previousTransform = object.transform;
            object.mesh = mesh;
            object.material = Fluxion::Foundation::NoHandle<FluxionMaterialHandle>();
            object.pipeline = Fluxion::Foundation::NoHandle<FluxionRenderPipelineHandle>();
            object.layerMask = 0xFFFFFFFFu;
            object.visible = true;
            Fluxion_RenderWorld_AddObject(&world, &object);
        }

        Fluxion_RHI_CommandList_Begin(cmd);
        Fluxion_Renderer_SubmitRenderWorld(renderer, &world);
        Fluxion_Renderer_UploadScene(renderer, cmd);

        // THE WALL. Cleared rather than drawn -- see the note at the top.
        FluxionRHIBarrier depthToWrite{};
        depthToWrite.texture = depthTexture;
        depthToWrite.buffer = noBuffer;
        depthToWrite.before = frame == 0 ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE;
        depthToWrite.after = FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE;
        Fluxion_RHI_CommandList_Barrier(cmd, &depthToWrite, 1);

        FluxionRHIRenderingAttachment clearDepth;
        clearDepth.view = depthTextureView;
        clearDepth.clear = true;
        clearDepth.clearColor[0] = kWallDepth;

        FluxionRHIRenderingDesc clearDesc;
        clearDesc.colorAttachments = nullptr;
        clearDesc.colorAttachmentCount = 0;
        clearDesc.depthAttachment = &clearDepth;
        clearDesc.width = kWidth;
        clearDesc.height = kHeight;
        Fluxion_RHI_CommandList_BeginRendering(cmd, &clearDesc);
        Fluxion_RHI_CommandList_EndRendering(cmd);

        // What the render graph does for this pass in a real frame.
        FluxionRHIBarrier pyramidToTarget{};
        pyramidToTarget.texture = Fluxion_Renderer_GetDepthPyramidTexture(renderer);
        pyramidToTarget.buffer = noBuffer;
        pyramidToTarget.before = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
        pyramidToTarget.after = FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET;
        Fluxion_RHI_CommandList_Barrier(cmd, &pyramidToTarget, 1);

        FluxionDepthPyramidPass_Execute(cmd, Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

        Fluxion_Renderer_EndFrame(renderer, cmd);
        Fluxion_RHI_CommandList_End(cmd);

        FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
        Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
        TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));
        Fluxion_RHI_DestroyFence(fence);

        if (frame == 3)
        {
            // The count the device worked out for the frame before this
            // one -- the first frame whose culling had a pyramid to read.
            const u32 seen = Fluxion_Renderer_GetVisibleObjectCount(renderer);
            FLUXION_LOG_INFO("RenderCoreTests", "%s: eight objects, one of them behind a wall at %.2f -> %u drawn.", backendName,
                             (f64)kWallDepth, seen);

            // SEVEN OF EIGHT, ON EVERY BACKEND: the one behind the wall
            // is gone and every one in front of it is still there. The
            // second half is the half that matters -- culling too much is
            // a picture with holes in it, and it is what this file exists
            // to catch.
            TEST_CHECK(ctx, seen == 7u);
        }

        Fluxion_RenderWorld_Shutdown(&world);
        Fluxion_RenderView_Destroy(view);
    }

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

extern "C" void Test_OcclusionCullGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the occlusion culling was NOT checked here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
