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
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/GPUScene.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>

#include <cstring>

// What the frame looks like from the device's side, checked as data
// rather than as a picture.
//
// Two questions, and they fail differently: does the grouping put the
// right objects together (no device needed), and do the rows it wrote
// actually arrive in device memory (which needs one, and is the half a
// wrong picture never names).

namespace
{

FluxionMat4 Translation(f32 x)
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][3] = x;
    return m;
}

// Two meshes and two materials' worth of handles, without a device
// behind them: the grouping reads handles, it does not resolve them.
FluxionMeshBufferHandle FakeMesh(u32 index) { return FluxionMeshBufferHandle{ index, 1 }; }
FluxionMaterialHandle FakeMaterial(u32 index) { return FluxionMaterialHandle{ index, 1 }; }
FluxionRenderPipelineHandle FakePipeline(u32 index) { return FluxionRenderPipelineHandle{ index, 1 }; }

void TheGroupingPutsLikeWithLike(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHICommandListHandle commandList)
{
    FluxionGPUSceneHandle scene = Fluxion_GPUScene_Create(device);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(scene));

    // Added deliberately interleaved: A B A B. If the grouping only
    // noticed runs that were already together, this would come out as
    // four batches rather than two.
    Fluxion_GPUScene_Begin(scene);
    const FluxionMat4 first = Translation(1.0f);
    const FluxionMat4 second = Translation(2.0f);
    Fluxion_GPUScene_Add(scene, FakeMesh(1), FakeMaterial(1), FakePipeline(1), &first);
    Fluxion_GPUScene_Add(scene, FakeMesh(2), FakeMaterial(2), FakePipeline(1), &second);
    Fluxion_GPUScene_Add(scene, FakeMesh(1), FakeMaterial(1), FakePipeline(1), &first);
    Fluxion_GPUScene_Add(scene, FakeMesh(2), FakeMaterial(2), FakePipeline(1), &second);

    Fluxion_RHI_CommandList_Begin(commandList);
    Fluxion_GPUScene_Upload(scene, commandList, Fluxion::Foundation::NoHandle<FluxionRHIBindGroupLayoutHandle>());
    Fluxion_RHI_CommandList_End(commandList);

    TEST_CHECK(ctx, Fluxion_GPUScene_GetObjectCount(scene) == 4);
    TEST_CHECK(ctx, Fluxion_GPUScene_GetBatchCount(scene) == 2);

    const FluxionGPUSceneBatch* a = Fluxion_GPUScene_GetBatch(scene, 0);
    const FluxionGPUSceneBatch* b = Fluxion_GPUScene_GetBatch(scene, 1);
    TEST_CHECK(ctx, a != nullptr && b != nullptr);
    if (a == nullptr || b == nullptr) return;

    // Each batch's rows are a RUN, and the runs cover everything exactly
    // once -- which is the whole property one instanced draw relies on.
    TEST_CHECK(ctx, a->objectCount == 2 && b->objectCount == 2);
    TEST_CHECK(ctx, a->firstObject == 0);
    TEST_CHECK(ctx, b->firstObject == 2);

    // A frame with nothing in it has no batches, rather than one empty
    // one that would still be bound and drawn.
    Fluxion_GPUScene_Begin(scene);
    Fluxion_RHI_CommandList_Begin(commandList);
    Fluxion_GPUScene_Upload(scene, commandList, Fluxion::Foundation::NoHandle<FluxionRHIBindGroupLayoutHandle>());
    Fluxion_RHI_CommandList_End(commandList);
    TEST_CHECK(ctx, Fluxion_GPUScene_GetBatchCount(scene) == 0);

    Fluxion_GPUScene_Destroy(scene);
}

// The half a picture cannot answer: what the processor wrote is what
// device memory holds by the time anything draws.
void TheRowsReachTheDevice(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the object rows were NOT checked on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    FluxionGPUSceneHandle scene = Fluxion_GPUScene_Create(device);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(scene));

    const FluxionMat4 placed = Translation(3.0f);
    Fluxion_GPUScene_Begin(scene);
    Fluxion_GPUScene_Add(scene, FakeMesh(1), FakeMaterial(1), FakePipeline(1), &placed);

    FluxionRHIBufferDesc readbackDesc{ sizeof(FluxionGPUSceneObject), FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "GPUScene.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(readback));

    Fluxion_RHI_CommandList_Begin(cmd);
    Fluxion_GPUScene_Upload(scene, cmd, Fluxion::Foundation::NoHandle<FluxionRHIBindGroupLayoutHandle>());

    const FluxionRHITextureHandle noTexture = Fluxion::Foundation::NoHandle<FluxionRHITextureHandle>();
    FluxionRHIBarrier toSource = { noTexture, Fluxion_GPUScene_GetObjectBuffer(scene),
                                   FLUXION_RHI_RESOURCE_STATE_SHADER_READ, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);
    Fluxion_RHI_CommandList_CopyBuffer(cmd, Fluxion_GPUScene_GetObjectBuffer(scene), 0, readback, 0, sizeof(FluxionGPUSceneObject));
    Fluxion_RHI_CommandList_End(cmd);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
    TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));

    if (const FluxionGPUSceneObject* row = (const FluxionGPUSceneObject*)Fluxion_RHI_MapBuffer(readback))
    {
        // TRANSPOSED, because that is what the shading languages read --
        // so the translation the caller put in the last COLUMN comes back
        // in the last ROW.
        FLUXION_LOG_INFO("RenderCoreTests", "%s: the object row reads [%.2f %.2f %.2f %.2f] on its last row.", backendName,
                         (f64)row->model.m[3][0], (f64)row->model.m[3][1], (f64)row->model.m[3][2], (f64)row->model.m[3][3]);

        TEST_CHECK(ctx, row->model.m[0][0] == 1.0f && row->model.m[1][1] == 1.0f && row->model.m[2][2] == 1.0f);
        TEST_CHECK(ctx, row->model.m[3][0] == 3.0f);
        TEST_CHECK(ctx, row->model.m[3][3] == 1.0f);

        Fluxion_RHI_UnmapBuffer(readback);
    }
    else
    {
        TEST_CHECK(ctx, false);
    }

    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyBuffer(readback);
    Fluxion_GPUScene_Destroy(scene);
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

// TWO LEVELS OF ONE MESH ARE TWO BATCHES.
//
// Not an optimisation that failed to happen: one indirect command
// carries one range of indices, so two objects of the same mesh drawn
// from different ranges cannot share a command. What this checks is that
// the split happens AND that each command names its own level's range --
// a split that produced two batches both drawing level zero would look
// right in every count and draw the wrong geometry.
void TwoLevelsAreTwoBatches(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
{
    if (!Fluxion_RHI_IsBackendAvailable(backend)) return;

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the levels of detail were NOT batched on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    const f32 vertices[9] = { 0.0f, 0.5f, 0.0f, 0.5f, -0.5f, 0.0f, -0.5f, -0.5f, 0.0f };
    const u16 indices[6] = { 0, 1, 2, 2, 1, 0 };

    FluxionMeshBufferDesc meshDesc;
    std::memset(&meshDesc, 0, sizeof(meshDesc));
    meshDesc.vertexData = vertices;
    meshDesc.vertexDataSize = sizeof(vertices);
    meshDesc.indexData = indices;
    meshDesc.indexDataSize = sizeof(indices);
    meshDesc.use16BitIndices = true;
    meshDesc.vertexLayout.attributes[0].location = 0;
    meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[0].offset = 0;
    meshDesc.vertexLayout.attributeCount = 1;
    meshDesc.vertexLayout.stride = 3 * sizeof(f32);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.5f, -0.5f, 0.0f }, FluxionVec3{ 0.5f, 0.5f, 0.0f } };

    // Six indices, halved: the near level draws all of them, the far one
    // draws the last three.
    meshDesc.levels[0] = FluxionMeshLevel{ 0, 6, 0.0f };
    meshDesc.levels[1] = FluxionMeshLevel{ 3, 3, 20.0f };
    meshDesc.levelCount = 2;
    meshDesc.debugName = "GPUScene.TwoLevels";

    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));

    FluxionGPUSceneHandle scene = Fluxion_GPUScene_Create(device);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(scene));

    const FluxionMaterialHandle material{ 1, 1 };
    const FluxionRenderPipelineHandle pipeline{ 1, 1 };
    const FluxionMat4 near = Translation(1.0f);
    const FluxionMat4 far = Translation(40.0f);

    // Interleaved, and the same mesh and material throughout -- so the
    // only thing that can put them in different batches is the level.
    Fluxion_GPUScene_Begin(scene);
    Fluxion_GPUScene_AddDetailed(scene, mesh, material, pipeline, &near, 0xFFFFFFFFu, 0);
    Fluxion_GPUScene_AddDetailed(scene, mesh, material, pipeline, &far, 0xFFFFFFFFu, 1);
    Fluxion_GPUScene_AddDetailed(scene, mesh, material, pipeline, &near, 0xFFFFFFFFu, 0);

    // And one asking for a level this mesh does not have: drawn at its
    // coarsest rather than dropped, which is the batch above.
    Fluxion_GPUScene_AddDetailed(scene, mesh, material, pipeline, &far, 0xFFFFFFFFu, 7);

    const usize commandBytes = 4 * sizeof(FluxionRHIDrawIndexedIndirectCommand);
    FluxionRHIBufferDesc readbackDesc{ commandBytes, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU,
                                       "GPUScene.LevelReadback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);

    Fluxion_RHI_CommandList_Begin(cmd);
    Fluxion_GPUScene_Upload(scene, cmd, Fluxion::Foundation::NoHandle<FluxionRHIBindGroupLayoutHandle>());

    const FluxionRHITextureHandle noTexture = Fluxion::Foundation::NoHandle<FluxionRHITextureHandle>();
    FluxionRHIBarrier toSource = { noTexture, Fluxion_GPUScene_GetIndirectBuffer(scene), FLUXION_RHI_RESOURCE_STATE_COMMON,
                                   FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);
    Fluxion_RHI_CommandList_CopyBuffer(cmd, Fluxion_GPUScene_GetIndirectBuffer(scene), 0, readback, 0, commandBytes);
    Fluxion_RHI_CommandList_End(cmd);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
    TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));

    // Two batches of one mesh: level zero holds two objects, level one
    // holds the other two.
    TEST_CHECK(ctx, Fluxion_GPUScene_GetObjectCount(scene) == 4);
    TEST_CHECK(ctx, Fluxion_GPUScene_GetBatchCount(scene) == 2);

    const FluxionGPUSceneBatch* fine = Fluxion_GPUScene_GetBatch(scene, 0);
    const FluxionGPUSceneBatch* coarse = Fluxion_GPUScene_GetBatch(scene, 1);
    if (fine != nullptr && coarse != nullptr)
    {
        TEST_CHECK(ctx, fine->lodIndex == 0 && coarse->lodIndex == 1);
        TEST_CHECK(ctx, fine->objectCount == 2 && coarse->objectCount == 2);
    }

    // The BYTES are only worth reading where a copy really happened: the
    // Null backend records commands and moves nothing, so what comes
    // back from it is whatever the buffer held. What it does answer is
    // the batching above, which is arithmetic and needs no device.
    const bool copyHappened = backend != FLUXION_RHI_BACKEND_NULL;

    if (const FluxionRHIDrawIndexedIndirectCommand* commands =
            copyHappened ? (const FluxionRHIDrawIndexedIndirectCommand*)Fluxion_RHI_MapBuffer(readback) : NULL)
    {
        FLUXION_LOG_INFO("RenderCoreTests", "%s: level 0 draws %u indices from %u, level 1 draws %u from %u.", backendName,
                         commands[0].indexCount, commands[0].firstIndex, commands[1].indexCount, commands[1].firstIndex);

        // EACH COMMAND NAMES ITS OWN LEVEL'S RANGE. This is the whole
        // of the LOD as the device sees it.
        TEST_CHECK(ctx, commands[0].indexCount == 6 && commands[0].firstIndex == 0);
        TEST_CHECK(ctx, commands[1].indexCount == 3 && commands[1].firstIndex == 3);

        Fluxion_RHI_UnmapBuffer(readback);
    }
    else
    {
        TEST_CHECK(ctx, !copyHappened);
    }

    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyBuffer(readback);
    Fluxion_GPUScene_Destroy(scene);
    Fluxion_MeshBuffer_Destroy(mesh);
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_GPUScene_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(instance, adapters, 1);
    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    TheGroupingPutsLikeWithLike(ctx, device, commandList);

    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);

    // The levels are batched on the Null backend as well as the real
    // ones: what decides them is arithmetic, and what a device adds is
    // only whether the commands arrive.
    TwoLevelsAreTwoBatches(ctx, FLUXION_RHI_BACKEND_NULL, "Null");
    TwoLevelsAreTwoBatches(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    TwoLevelsAreTwoBatches(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    TwoLevelsAreTwoBatches(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");

    TheRowsReachTheDevice(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    TheRowsReachTheDevice(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    TheRowsReachTheDevice(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
