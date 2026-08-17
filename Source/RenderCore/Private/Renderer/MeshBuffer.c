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

#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>

#include "RendererInternal.h"

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

typedef struct FluxionMeshBufferRecord
{
    bool alive;
    u32 generation;

    FluxionRHIBufferHandle vertexBuffer;
    FluxionRHIBufferHandle indexBuffer; // invalid if this mesh has no index data
    u32 vertexCount;
    u32 indexCount;
    usize vertexBytes;
    usize indexBytes;
    bool use16BitIndices;
    FluxionRHIVertexLayout vertexLayout;
    FluxionAABB bounds;
} FluxionMeshBufferRecord;

static FluxionMeshBufferRecord s_meshBuffers[FLUXION_RENDERER_MAX_MESH_BUFFERS];

static FluxionMeshBufferRecord* Fluxion_MeshBufferInternal_Resolve(FluxionMeshBufferHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_MESH_BUFFERS) return NULL;
    FluxionMeshBufferRecord* record = &s_meshBuffers[handle.index];
    if (!record->alive || record->generation != handle.generation) return NULL;
    return record;
}

FluxionMeshBufferHandle Fluxion_MeshBuffer_Create(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, const FluxionMeshBufferDesc* desc)
{
    FluxionMeshBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(desc != NULL && desc->vertexData != NULL && desc->vertexDataSize > 0);

    u32 index = FLUXION_RENDERER_MAX_MESH_BUFFERS;
    for (u32 i = 0; i < FLUXION_RENDERER_MAX_MESH_BUFFERS; ++i)
    {
        if (!s_meshBuffers[i].alive) { index = i; break; }
    }
    if (index == FLUXION_RENDERER_MAX_MESH_BUFFERS) return invalid;

    bool hasIndices = desc->indexData != NULL && desc->indexDataSize > 0;
    usize stagingSize = desc->vertexDataSize + (hasIndices ? desc->indexDataSize : 0);

    FluxionRHIBufferDesc stagingDesc;
    stagingDesc.size = stagingSize;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = "Fluxion.MeshBuffer.Staging";
    FluxionRHIBufferHandle stagingBuffer = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
    if (FLUXION_HANDLE_IS_VALID(stagingBuffer)) FluxionRendererInternal_RecordGpuAlloc(true, stagingDesc.size);

    u8* mapped = (u8*)Fluxion_RHI_MapBuffer(stagingBuffer);
    memcpy(mapped, desc->vertexData, desc->vertexDataSize);
    if (hasIndices) memcpy(mapped + desc->vertexDataSize, desc->indexData, desc->indexDataSize);
    Fluxion_RHI_UnmapBuffer(stagingBuffer);

    FluxionRHIBufferDesc vertexBufferDesc;
    vertexBufferDesc.size = desc->vertexDataSize;
    vertexBufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
    vertexBufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    vertexBufferDesc.debugName = desc->debugName;
    FluxionRHIBufferHandle vertexBuffer = Fluxion_RHI_CreateBuffer(device, &vertexBufferDesc);
    if (FLUXION_HANDLE_IS_VALID(vertexBuffer)) FluxionRendererInternal_RecordGpuAlloc(false, vertexBufferDesc.size);

    FluxionRHIBufferHandle indexBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (hasIndices)
    {
        FluxionRHIBufferDesc indexBufferDesc;
        indexBufferDesc.size = desc->indexDataSize;
        indexBufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_INDEX_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
        indexBufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
        indexBufferDesc.debugName = desc->debugName;
        indexBuffer = Fluxion_RHI_CreateBuffer(device, &indexBufferDesc);
        if (FLUXION_HANDLE_IS_VALID(indexBuffer)) FluxionRendererInternal_RecordGpuAlloc(false, indexBufferDesc.size);
    }

    FluxionRHICommandListHandle uploadCommandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(uploadCommandList);

    // Into COPY_DESTINATION before the copy, explicitly. The backends in
    // use happen to tolerate copying into a freshly created buffer (a
    // buffer has no layout in one API and promotes from COMMON in
    // another), so this never failed visibly -- but the post-upload
    // barrier below then declared COPY_DESTINATION as its before-state
    // without anything ever having put the buffer there, and a declared
    // state that never happened is exactly the kind of lie that stays
    // invisible until a stricter backend or tool calls it out.
    FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBarrier preCopyBarriers[2];
    u32 preCopyCount = 0;
    preCopyBarriers[preCopyCount].texture = noTexture;
    preCopyBarriers[preCopyCount].buffer = vertexBuffer;
    preCopyBarriers[preCopyCount].before = FLUXION_RHI_RESOURCE_STATE_COMMON;
    preCopyBarriers[preCopyCount].after = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    ++preCopyCount;
    if (hasIndices)
    {
        preCopyBarriers[preCopyCount].texture = noTexture;
        preCopyBarriers[preCopyCount].buffer = indexBuffer;
        preCopyBarriers[preCopyCount].before = FLUXION_RHI_RESOURCE_STATE_COMMON;
        preCopyBarriers[preCopyCount].after = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
        ++preCopyCount;
    }
    Fluxion_RHI_CommandList_Barrier(uploadCommandList, preCopyBarriers, preCopyCount);

    Fluxion_RHI_CommandList_CopyBuffer(uploadCommandList, stagingBuffer, 0, vertexBuffer, 0, desc->vertexDataSize);
    if (hasIndices) Fluxion_RHI_CommandList_CopyBuffer(uploadCommandList, stagingBuffer, desc->vertexDataSize, indexBuffer, 0, desc->indexDataSize);

    FluxionRHIBarrier postUploadBarriers[2];
    u32 barrierCount = 0;
    postUploadBarriers[barrierCount].texture = noTexture;
    postUploadBarriers[barrierCount].buffer = vertexBuffer;
    postUploadBarriers[barrierCount].before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    postUploadBarriers[barrierCount].after = FLUXION_RHI_RESOURCE_STATE_VERTEX_BUFFER;
    ++barrierCount;
    if (hasIndices)
    {
        postUploadBarriers[barrierCount].texture = noTexture;
        postUploadBarriers[barrierCount].buffer = indexBuffer;
        postUploadBarriers[barrierCount].before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
        postUploadBarriers[barrierCount].after = FLUXION_RHI_RESOURCE_STATE_INDEX_BUFFER;
        ++barrierCount;
    }
    Fluxion_RHI_CommandList_Barrier(uploadCommandList, postUploadBarriers, barrierCount);
    Fluxion_RHI_CommandList_End(uploadCommandList);

    FluxionRHIFenceHandle uploadFence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &uploadCommandList, 1, uploadFence);
    Fluxion_RHI_WaitForFence(uploadFence);

    Fluxion_RHI_DestroyFence(uploadFence);
    Fluxion_RHI_DestroyCommandList(uploadCommandList);
    Fluxion_RHI_DestroyBuffer(stagingBuffer);
    FluxionRendererInternal_RecordGpuFree(true, stagingDesc.size);
    Fluxion_RHI_Device_CollectGarbage(device);

    FluxionMeshBufferRecord* record = &s_meshBuffers[index];
    u32 generation = record->generation;
    memset(record, 0, sizeof(*record));
    record->alive = true;
    record->generation = generation;
    record->vertexBuffer = vertexBuffer;
    record->indexBuffer = indexBuffer;
    record->vertexCount = desc->vertexLayout.stride > 0 ? (u32)(desc->vertexDataSize / desc->vertexLayout.stride) : 0;
    record->vertexBytes = desc->vertexDataSize;
    record->indexBytes = hasIndices ? desc->indexDataSize : 0;
    record->indexCount = hasIndices ? (u32)(desc->indexDataSize / (desc->use16BitIndices ? sizeof(u16) : sizeof(u32))) : 0;
    record->use16BitIndices = desc->use16BitIndices;
    record->vertexLayout = desc->vertexLayout;
    record->bounds = desc->bounds;

    FluxionMeshBufferHandle handle = { index, generation };
    return handle;
}

void Fluxion_MeshBuffer_Destroy(FluxionMeshBufferHandle mesh)
{
    FluxionMeshBufferRecord* record = Fluxion_MeshBufferInternal_Resolve(mesh);
    if (record == NULL)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion_MeshBuffer_Destroy called with an invalid or already-destroyed handle");
        return;
    }

    Fluxion_RHI_DestroyBuffer(record->vertexBuffer);
    FluxionRendererInternal_RecordGpuFree(false, record->vertexBytes);
    if (FLUXION_HANDLE_IS_VALID(record->indexBuffer))
    {
        Fluxion_RHI_DestroyBuffer(record->indexBuffer);
        FluxionRendererInternal_RecordGpuFree(false, record->indexBytes);
    }

    record->alive = false;
    ++record->generation;
}

bool FluxionRendererInternal_MeshBuffer_Get(FluxionMeshBufferHandle mesh, FluxionRHIBufferHandle* outVertexBuffer, FluxionRHIBufferHandle* outIndexBuffer, u32* outVertexCount, u32* outIndexCount, bool* outUse16BitIndices, FluxionRHIVertexLayout* outVertexLayout)
{
    FluxionMeshBufferRecord* record = Fluxion_MeshBufferInternal_Resolve(mesh);
    if (record == NULL) return false;

    if (outVertexBuffer != NULL) *outVertexBuffer = record->vertexBuffer;
    if (outIndexBuffer != NULL) *outIndexBuffer = record->indexBuffer;
    if (outVertexCount != NULL) *outVertexCount = record->vertexCount;
    if (outIndexCount != NULL) *outIndexCount = record->indexCount;
    if (outUse16BitIndices != NULL) *outUse16BitIndices = record->use16BitIndices;
    if (outVertexLayout != NULL) *outVertexLayout = record->vertexLayout;
    return true;
}

bool FluxionRendererInternal_MeshBuffer_GetBounds(FluxionMeshBufferHandle mesh, FluxionAABB* outBounds)
{
    // Its own accessor rather than an eighth output on the one above:
    // what wants the bounds -- deciding whether to draw at all -- asks
    // before it wants any of the rest, and a caller that only needs to
    // know where something is should not have to name six buffers.
    const FluxionMeshBufferRecord* record = Fluxion_MeshBufferInternal_Resolve(mesh);
    if (record == NULL) return false;

    if (outBounds != NULL) *outBounds = record->bounds;
    return true;
}
