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
#include <Fluxion/Foundation/Log.h>

#include <math.h>
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

    // Always at least one, whatever the caller said -- see Create.
    FluxionMeshLevel levels[FLUXION_MESH_BUFFER_MAX_LEVELS];
    u32 levelCount;
} FluxionMeshBufferRecord;

static FluxionMeshBufferRecord s_meshBuffers[FLUXION_RENDERER_MAX_MESH_BUFFERS];

static FluxionMeshBufferRecord* Fluxion_MeshBufferInternal_Resolve(FluxionMeshBufferHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_MESH_BUFFERS) return NULL;
    FluxionMeshBufferRecord* record = &s_meshBuffers[handle.index];
    if (!record->alive || record->generation != handle.generation) return NULL;
    return record;
}

// The levels as the record keeps them: at least one, in range, and in
// order. False means the description was wrong about its own mesh, which
// is refused rather than drawn -- see FluxionMeshBufferDesc.
static bool Fluxion_MeshBufferInternal_MakeLevels(const FluxionMeshBufferDesc* desc, u32 indexCount,
                                                  FluxionMeshLevel* outLevels, u32* outCount)
{
    if (desc->levelCount == 0)
    {
        // The whole index buffer, from wherever the camera is. What
        // every mesh was before there were levels.
        outLevels[0].firstIndex = 0;
        outLevels[0].indexCount = indexCount;
        outLevels[0].minDistance = 0.0f;
        *outCount = 1;
        return true;
    }

    if (desc->levelCount > FLUXION_MESH_BUFFER_MAX_LEVELS)
    {
        FLUXION_LOG_ERROR("MeshBuffer", "a mesh described %u levels of detail and %u is the most one may have",
                          desc->levelCount, (u32)FLUXION_MESH_BUFFER_MAX_LEVELS);
        return false;
    }

    for (u32 i = 0; i < desc->levelCount; ++i)
    {
        const FluxionMeshLevel* level = &desc->levels[i];

        if (level->indexCount == 0 || (usize)level->firstIndex + level->indexCount > indexCount)
        {
            FLUXION_LOG_ERROR("MeshBuffer", "level %u of a mesh reads indices %u..%u of a buffer that holds %u", i,
                              level->firstIndex, level->firstIndex + level->indexCount, indexCount);
            return false;
        }

        outLevels[i] = *level;

        // Level zero starts where the camera is, whatever was written --
        // there is no distance at which nothing is drawn.
        if (i == 0)
        {
            outLevels[i].minDistance = 0.0f;
            continue;
        }

        if (level->minDistance <= outLevels[i - 1].minDistance)
        {
            FLUXION_LOG_ERROR("MeshBuffer", "level %u of a mesh begins at %.2f, which is not further away than level %u at %.2f", i,
                              (f64)level->minDistance, i - 1, (f64)outLevels[i - 1].minDistance);
            return false;
        }
    }

    *outCount = desc->levelCount;
    return true;
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

    const u32 describedIndices = hasIndices ? (u32)(desc->indexDataSize / (desc->use16BitIndices ? sizeof(u16) : sizeof(u32))) : 0;

    FluxionMeshLevel levels[FLUXION_MESH_BUFFER_MAX_LEVELS];
    u32 levelCount = 0;

    // Before a single buffer is made: a mesh whose levels do not fit its
    // own indices is refused here, where refusing costs nothing.
    if (!Fluxion_MeshBufferInternal_MakeLevels(desc, describedIndices, levels, &levelCount)) return invalid;

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
    memcpy(record->levels, levels, sizeof(levels));
    record->levelCount = levelCount;

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

u32 Fluxion_MeshBuffer_GetLevelCount(FluxionMeshBufferHandle mesh)
{
    const FluxionMeshBufferRecord* record = Fluxion_MeshBufferInternal_Resolve(mesh);
    return record != NULL ? record->levelCount : 0;
}

bool Fluxion_MeshBuffer_GetLevel(FluxionMeshBufferHandle mesh, u32 level, FluxionMeshLevel* outLevel)
{
    const FluxionMeshBufferRecord* record = Fluxion_MeshBufferInternal_Resolve(mesh);
    if (record == NULL || level >= record->levelCount) return false;

    if (outLevel != NULL) *outLevel = record->levels[level];
    return true;
}

u32 Fluxion_MeshBuffer_SelectLevel(FluxionMeshBufferHandle mesh, FluxionVec3 cameraPosition, const FluxionMat4* world)
{
    const FluxionMeshBufferRecord* record = Fluxion_MeshBufferInternal_Resolve(mesh);
    if (record == NULL || record->levelCount <= 1) return 0;

    // Where the mesh is and how big it is, once the transform has had
    // its say -- the same sphere the culling works out, worked out the
    // same way so that the two cannot disagree about how far away
    // something is.
    const FluxionMat4 identity = Fluxion_Mat4_Identity();
    const FluxionMat4* matrix = world != NULL ? world : &identity;

    const FluxionVec3 centre = { (record->bounds.min.x + record->bounds.max.x) * 0.5f,
                                 (record->bounds.min.y + record->bounds.max.y) * 0.5f,
                                 (record->bounds.min.z + record->bounds.max.z) * 0.5f };
    const FluxionVec3 extent = { (record->bounds.max.x - record->bounds.min.x) * 0.5f,
                                 (record->bounds.max.y - record->bounds.min.y) * 0.5f,
                                 (record->bounds.max.z - record->bounds.min.z) * 0.5f };

    const FluxionVec3 worldCentre = {
        matrix->m[0][0] * centre.x + matrix->m[0][1] * centre.y + matrix->m[0][2] * centre.z + matrix->m[0][3],
        matrix->m[1][0] * centre.x + matrix->m[1][1] * centre.y + matrix->m[1][2] * centre.z + matrix->m[1][3],
        matrix->m[2][0] * centre.x + matrix->m[2][1] * centre.y + matrix->m[2][2] * centre.z + matrix->m[2][3],
    };

    f32 longestAxis = 0.0f;
    for (u32 row = 0; row < 3; ++row)
    {
        const f32 length = sqrtf(matrix->m[row][0] * matrix->m[row][0] +
                                 matrix->m[row][1] * matrix->m[row][1] +
                                 matrix->m[row][2] * matrix->m[row][2]);
        if (length > longestAxis) longestAxis = length;
    }

    const f32 radius = sqrtf(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z) * longestAxis;

    const f32 dx = worldCentre.x - cameraPosition.x;
    const f32 dy = worldCentre.y - cameraPosition.y;
    const f32 dz = worldCentre.z - cameraPosition.z;

    f32 distance = sqrtf(dx * dx + dy * dy + dz * dz) - radius;
    if (distance < 0.0f) distance = 0.0f;

    // Walked from the far end, so the answer is the COARSEST level this
    // distance has reached rather than the first one that applies.
    for (u32 i = record->levelCount; i > 0; --i)
    {
        if (distance >= record->levels[i - 1].minDistance) return i - 1;
    }
    return 0;
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
