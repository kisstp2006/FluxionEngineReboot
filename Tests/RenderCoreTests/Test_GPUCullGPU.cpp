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
#include <Fluxion/RenderCore/Renderer/GPUScene.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// THE TWO WAYS OF DECIDING WHAT IS SEEN, MEASURED AGAINST EACH OTHER.
//
// Every other check of the culling asks whether an answer is the one this
// file expects. This one asks something a single path cannot be asked at
// all: the same scene, the same camera, and the same batches, worked out
// once on the processor and once on the device -- and the two lists of
// survivors have to be the same list.
//
// That is what makes the pipeline setting a POLICY rather than a second
// renderer. If the two ever drift, a project that switched the setting
// would get a different picture from the same data, and neither of them
// would look wrong on its own.
//
// The order within a batch's slice is NOT compared. The device's threads
// claim their slots in whatever order they are scheduled, so what is
// compared is the SET -- both lists sorted, then held side by side.

namespace
{

struct CullVertex
{
    f32 position[3];
};

const CullVertex kTriangle[3] = {
    { { -0.5f, -0.5f, 0.0f } },
    { { 0.5f, -0.5f, 0.0f } },
    { { 0.0f, 0.5f, 0.0f } },
};

const u16 kTriangleIndices[3] = { 0, 1, 2 };

// The camera this frame looks through: at the origin, down -Z, the same
// convention the scene's own camera uses.
FluxionMat4 Perspective(f32 fovYRadians, f32 aspect, f32 nearPlane, f32 farPlane)
{
    FluxionMat4 m;
    std::memset(&m, 0, sizeof(m));

    const f32 f = 1.0f / std::tan(fovYRadians * 0.5f);
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = farPlane / (nearPlane - farPlane);
    m.m[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    m.m[3][2] = -1.0f;
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

// Where the objects are, and what makes each of them interesting.
//
// Deliberately spread across all three tests and BOTH sides of each:
// in front and behind, near and far, in the view mask and out of it.
// A scene where everything survives would compare two full lists and
// notice nothing.
struct CullPlacement
{
    f32 x, y, z;
    u32 layerMask;
};

const u32 kViewLayers = 0x1u;
const u32 kOtherLayers = 0x2u;
const f32 kCullDistance = 30.0f;

const CullPlacement kPlacements[] = {
    // Straight ahead, near: seen.
    { 0.0f, 0.0f, -5.0f, kViewLayers },
    { 1.5f, 0.0f, -6.0f, kViewLayers },
    { -1.5f, 0.0f, -7.0f, kViewLayers },
    { 0.0f, 1.5f, -8.0f, kViewLayers },

    // Ahead but past the distance limit -- nothing to do with the
    // frustum, which still holds them.
    { 0.0f, 0.0f, -44.0f, kViewLayers },
    { 2.0f, 0.0f, -47.0f, kViewLayers },

    // Behind the eye.
    { 0.0f, 0.0f, 6.0f, kViewLayers },
    { 3.0f, 0.0f, 11.0f, kViewLayers },

    // Far off to the sides at a near depth: outside the frustum without
    // being far away.
    { -22.0f, 0.0f, -6.0f, kViewLayers },
    { 22.0f, 0.0f, -6.0f, kViewLayers },
    { 0.0f, 24.0f, -6.0f, kViewLayers },

    // In view, near, and in a layer this view does not draw.
    { 0.5f, 0.0f, -9.0f, kOtherLayers },
    { -0.5f, 0.5f, -10.0f, kOtherLayers },

    // And more of the ordinary case, so the surviving list is long
    // enough that a wrong ORDER would not accidentally still match.
    { 2.5f, -1.0f, -12.0f, kViewLayers },
    { -2.5f, 1.0f, -13.0f, kViewLayers },
    { 4.0f, 0.0f, -16.0f, kViewLayers },
    { -4.0f, 0.0f, -17.0f, kViewLayers },
    { 0.0f, -3.0f, -19.0f, kViewLayers },
    { 3.0f, 3.0f, -21.0f, kViewLayers },
    { -3.0f, -3.0f, -23.0f, kViewLayers },
};

constexpr u32 kPlacementCount = (u32)(sizeof(kPlacements) / sizeof(kPlacements[0]));

FluxionMeshBufferHandle MakeMesh(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, const char* name)
{
    FluxionMeshBufferDesc desc{};
    desc.vertexData = kTriangle;
    desc.vertexDataSize = sizeof(kTriangle);
    desc.indexData = kTriangleIndices;
    desc.indexDataSize = sizeof(kTriangleIndices);
    desc.use16BitIndices = true;
    desc.vertexLayout.attributes[0].location = 0;
    desc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    desc.vertexLayout.attributes[0].offset = 0;
    desc.vertexLayout.attributeCount = 1;
    desc.vertexLayout.stride = sizeof(CullVertex);

    // A metre across, whatever the triangle above happens to be: the
    // bounds are what the culling reads, and a radius of zero would mean
    // "no bounds", which is always drawn.
    desc.bounds = FluxionAABB{ FluxionVec3{ -0.5f, -0.5f, -0.5f }, FluxionVec3{ 0.5f, 0.5f, 0.5f } };
    desc.debugName = name;

    return Fluxion_MeshBuffer_Create(device, queue, &desc);
}

// One run of the frame: adds every object, culls it the way asked, and
// brings back both the visible list and the instance counts the draws
// would use.
struct CullRun
{
    std::vector<u32> visible;
    std::vector<u32> instanceCounts;
    std::vector<u32> firstVisible;
    u32 batchCount = 0;
};

bool RunOnce(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, FluxionRHICommandListHandle cmd,
             FluxionGPUSceneHandle scene, FluxionMeshBufferHandle meshA, FluxionMeshBufferHandle meshB,
             FluxionGPUSceneCullMode mode, CullRun* out)
{
    const FluxionMaterialHandle material{ 1, 1 };
    const FluxionRenderPipelineHandle pipeline{ 1, 1 };

    Fluxion_GPUScene_Begin(scene);

    for (u32 i = 0; i < kPlacementCount; ++i)
    {
        const CullPlacement& placement = kPlacements[i];
        const FluxionMat4 transform = TranslationOf(placement.x, placement.y, placement.z);

        // Alternating meshes, so there are two batches and each has a
        // slice of the visible list of its own -- the arrangement the
        // device's atomic append has to get right.
        Fluxion_GPUScene_AddLayered(scene, (i % 2 == 0) ? meshA : meshB, material, pipeline, &transform, placement.layerMask);
    }

    FluxionGPUSceneCullDesc cull;
    std::memset(&cull, 0, sizeof(cull));
    cull.enabled = true;
    cull.mode = mode;
    cull.viewProjection = Perspective(1.0f, 1.0f, 0.1f, 200.0f);
    cull.cameraPosition = FluxionVec3{ 0.0f, 0.0f, 0.0f };
    cull.cullDistance = kCullDistance;
    cull.layerMask = kViewLayers;
    Fluxion_GPUScene_SetCulling(scene, &cull);

    const usize visibleBytes = (usize)kPlacementCount * sizeof(u32);
    const usize commandBytes = (usize)kPlacementCount * sizeof(FluxionRHIDrawIndexedIndirectCommand);

    FluxionRHIBufferDesc readbackDesc{ visibleBytes + commandBytes, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "GPUCull.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(readback));
    if (!FLUXION_HANDLE_IS_VALID(readback)) return false;

    Fluxion_RHI_CommandList_Begin(cmd);
    Fluxion_GPUScene_Upload(scene, cmd, Fluxion::Foundation::NoHandle<FluxionRHIBindGroupLayoutHandle>());

    const FluxionRHITextureHandle noTexture = Fluxion::Foundation::NoHandle<FluxionRHITextureHandle>();

    // Both buffers are left by Upload in the state the frame draws from,
    // so both are borrowed for a copy and handed straight back.
    FluxionRHIBarrier toSource[2] = {
        { noTexture, Fluxion_GPUScene_GetVisibleBuffer(scene), FLUXION_RHI_RESOURCE_STATE_SHADER_READ, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE },
        { noTexture, Fluxion_GPUScene_GetIndirectBuffer(scene), FLUXION_RHI_RESOURCE_STATE_COMMON, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE },
    };
    Fluxion_RHI_CommandList_Barrier(cmd, toSource, 2);

    Fluxion_RHI_CommandList_CopyBuffer(cmd, Fluxion_GPUScene_GetVisibleBuffer(scene), 0, readback, 0, visibleBytes);
    Fluxion_RHI_CommandList_CopyBuffer(cmd, Fluxion_GPUScene_GetIndirectBuffer(scene), 0, readback, visibleBytes, commandBytes);

    FluxionRHIBarrier backAgain[2] = {
        { noTexture, Fluxion_GPUScene_GetVisibleBuffer(scene), FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, FLUXION_RHI_RESOURCE_STATE_SHADER_READ },
        { noTexture, Fluxion_GPUScene_GetIndirectBuffer(scene), FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, FLUXION_RHI_RESOURCE_STATE_COMMON },
    };
    Fluxion_RHI_CommandList_Barrier(cmd, backAgain, 2);

    Fluxion_RHI_CommandList_End(cmd);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
    const bool completed = Fluxion_RHI_WaitForFence(fence);
    TEST_CHECK(ctx, completed);

    bool ok = false;
    if (completed)
    {
        if (const u8* bytes = (const u8*)Fluxion_RHI_MapBuffer(readback))
        {
            out->batchCount = Fluxion_GPUScene_GetBatchCount(scene);
            out->visible.assign((const u32*)bytes, (const u32*)bytes + kPlacementCount);

            const FluxionRHIDrawIndexedIndirectCommand* commands = (const FluxionRHIDrawIndexedIndirectCommand*)(bytes + visibleBytes);
            out->instanceCounts.clear();
            out->firstVisible.clear();
            for (u32 i = 0; i < out->batchCount; ++i)
            {
                out->instanceCounts.push_back(commands[i].instanceCount);

                const FluxionGPUSceneBatch* batch = Fluxion_GPUScene_GetBatch(scene, i);
                out->firstVisible.push_back(batch != nullptr ? batch->firstVisible : 0);
            }

            Fluxion_RHI_UnmapBuffer(readback);
            ok = true;
        }
    }

    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyBuffer(readback);
    return ok;
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the two culling paths were NOT compared on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    FluxionMeshBufferHandle meshA = MakeMesh(device, queue, "GPUCull.MeshA");
    FluxionMeshBufferHandle meshB = MakeMesh(device, queue, "GPUCull.MeshB");
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(meshA) && FLUXION_HANDLE_IS_VALID(meshB));

    FluxionGPUSceneHandle scene = Fluxion_GPUScene_Create(device);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(scene));

    CullRun onHost;
    CullRun onDevice;
    const bool hostRan = RunOnce(ctx, device, queue, cmd, scene, meshA, meshB, FLUXION_GPU_SCENE_CULL_CPU, &onHost);
    const bool deviceRan = RunOnce(ctx, device, queue, cmd, scene, meshA, meshB, FLUXION_GPU_SCENE_CULL_GPU, &onDevice);

    if (hostRan && deviceRan)
    {
        // The same scene, so the same batches -- if this differs, the
        // comparison below is comparing two different frames.
        TEST_CHECK(ctx, onHost.batchCount == onDevice.batchCount);
        TEST_CHECK(ctx, onHost.batchCount == 2);
        TEST_CHECK(ctx, onHost.firstVisible == onDevice.firstVisible);

        u32 hostSeen = 0;
        u32 deviceSeen = 0;
        for (u32 i = 0; i < onHost.batchCount && i < onDevice.batchCount; ++i)
        {
            hostSeen += onHost.instanceCounts[i];
            deviceSeen += onDevice.instanceCounts[i];
        }

        FLUXION_LOG_INFO("RenderCoreTests", "%s: %u objects -> the processor saw %u, the device saw %u.", backendName, kPlacementCount,
                         hostSeen, deviceSeen);

        // Something was thrown away and something was kept. Two paths
        // that agreed on "everything" or on "nothing" would agree
        // without either of them having done anything.
        TEST_CHECK(ctx, hostSeen > 0 && hostSeen < kPlacementCount);

        for (u32 i = 0; i < onHost.batchCount && i < onDevice.batchCount; ++i)
        {
            TEST_CHECK(ctx, onHost.instanceCounts[i] == onDevice.instanceCounts[i]);
            if (onHost.instanceCounts[i] != onDevice.instanceCounts[i]) continue;

            const u32 first = onHost.firstVisible[i];
            const u32 count = onHost.instanceCounts[i];
            if (first + count > kPlacementCount)
            {
                TEST_CHECK(ctx, false);
                continue;
            }

            // Sorted, because the device's threads claim their slots in
            // whatever order they run -- see the note at the top.
            std::vector<u32> hostSlice(onHost.visible.begin() + first, onHost.visible.begin() + first + count);
            std::vector<u32> deviceSlice(onDevice.visible.begin() + first, onDevice.visible.begin() + first + count);
            std::sort(hostSlice.begin(), hostSlice.end());
            std::sort(deviceSlice.begin(), deviceSlice.end());

            TEST_CHECK(ctx, hostSlice == deviceSlice);

            // And each survivor belongs to the batch that claimed it: a
            // list that agreed but pointed outside the batch's own rows
            // would draw somebody else's objects with this batch's mesh.
            const FluxionGPUSceneBatch* batch = Fluxion_GPUScene_GetBatch(scene, i);
            if (batch != nullptr)
            {
                for (u32 row : deviceSlice)
                {
                    TEST_CHECK(ctx, row >= batch->firstObject && row < batch->firstObject + batch->objectCount);
                }
            }

            // No slot claimed twice. The atomic is the only thing that
            // makes this true, and a plain read-then-write would fail
            // here rather than somewhere in the picture.
            TEST_CHECK(ctx, std::adjacent_find(deviceSlice.begin(), deviceSlice.end()) == deviceSlice.end());
        }
    }

    Fluxion_GPUScene_Destroy(scene);
    Fluxion_MeshBuffer_Destroy(meshB);
    Fluxion_MeshBuffer_Destroy(meshA);
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_GPUCullGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the culling paths were NOT compared here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
