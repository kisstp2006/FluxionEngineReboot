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

// The frame as the device sees it: one row per object, one command per
// group of them.

#include <Fluxion/RenderCore/Renderer/GPUScene.h>

#include "RendererInternal.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <math.h>
#include <string.h>

#define FLUXION_GPU_SCENE_LOG_CATEGORY "GPUScene"

// As many scenes as there are renderers, and for the same reason: one
// per renderer is what anything here builds.
#define FLUXION_GPU_SCENE_MAX_SCENES FLUXION_RENDERER_MAX_INSTANCES

// What a frame starts with room for. It doubles from here and never
// shrinks -- a scene whose object count wobbles by one would otherwise
// rebuild its buffers twice a second for no gain, which is the same rule
// the light list already follows.
#define FLUXION_GPU_SCENE_INITIAL_OBJECTS 256

// One thing to draw, before the sort decides where its row goes.
typedef struct FluxionGPUSceneEntry
{
    FluxionMeshBufferHandle mesh;
    FluxionMaterialHandle material;
    FluxionRenderPipelineHandle pipeline;
    FluxionMat4 model; // already transposed

    // Where this object is and how far it reaches, worked out once when
    // it was added rather than once per pass that wants to know.
    FluxionVec3 boundsCentre;
    f32 boundsRadius;

    u32 layerMask;
} FluxionGPUSceneEntry;

typedef struct FluxionGPUSceneRecord
{
    bool alive;
    u32 generation;
    FluxionRHIDeviceHandle device;

    // What was added this frame, in the order it was added.
    FluxionGPUSceneEntry* entries;
    u32 entryCount;
    u32 entryCapacity;

    // The sort's answer: entry indices, grouped.
    u32* order;

    // What Upload worked out.
    FluxionGPUSceneBatch* batches;
    u32 batchCount;

    // One OBJECT bind group per batch, built with the batch and let go
    // of one frame later -- see Fluxion_GPUScene_GetBatchBindGroup for
    // why "later" and not "as soon as the draw is recorded".
    FluxionRHIBindGroupHandle* batchBindGroups;
    u32 batchBindGroupCount;

    // The rows the GPU reads, and the pair of buffers that get them
    // there: one the processor writes, one the device reads. A storage
    // buffer cannot be the CPU-visible one on every backend -- see the
    // note on the OBJECT layout in RendererInternal.h -- so this is a
    // copy, and a copy is a recorded command.
    FluxionRHIBufferHandle objectStaging;
    FluxionRHIBufferHandle objectStorage;
    u32 objectCapacity; // in rows

    // Which rows this frame draws, in draw order -- see
    // Fluxion/Object.jsl. Staged and copied exactly like the rows: a
    // shader reads it, so it is device memory.
    FluxionRHIBufferHandle visibleStaging;
    FluxionRHIBufferHandle visibleStorage;

    // Whether the rows have ever been copied across. A buffer nobody has
    // written yet is in the state it was created in, not in the one the
    // last upload left it in -- and a barrier that claims otherwise is
    // wrong on the first frame and right on every one after, which is
    // the kind of wrong that only shows up on a validation layer.
    bool objectStorageWritten;

    // The commands themselves, and one small uniform slice per batch
    // holding where that batch's rows begin. Both are read by the device
    // and written by the processor, and neither is a storage buffer, so
    // both can be host-visible and need no copy.
    FluxionRHIBufferHandle indirectBuffer;
    FluxionRHIBufferHandle batchUniformBuffer;
    u32 batchCapacity;

    // What this frame can see, and how many objects survived it.
    FluxionGPUSceneCullDesc cull;
    u32 visibleCount;
} FluxionGPUSceneRecord;

static FluxionGPUSceneRecord s_scenes[FLUXION_GPU_SCENE_MAX_SCENES];

static FluxionGPUSceneRecord* Fluxion_GPUSceneInternal_Resolve(FluxionGPUSceneHandle handle)
{
    if (handle.index >= FLUXION_GPU_SCENE_MAX_SCENES) return NULL;
    FluxionGPUSceneRecord* record = &s_scenes[handle.index];
    if (!record->alive || record->generation != handle.generation) return NULL;
    return record;
}

// --- Growth ---------------------------------------------------------------

static void Fluxion_GPUSceneInternal_DestroyObjectBuffers(FluxionGPUSceneRecord* record)
{
    if (FLUXION_HANDLE_IS_VALID(record->objectStaging)) Fluxion_RHI_DestroyBuffer(record->objectStaging);
    if (FLUXION_HANDLE_IS_VALID(record->objectStorage)) Fluxion_RHI_DestroyBuffer(record->objectStorage);
    if (FLUXION_HANDLE_IS_VALID(record->visibleStaging)) Fluxion_RHI_DestroyBuffer(record->visibleStaging);
    if (FLUXION_HANDLE_IS_VALID(record->visibleStorage)) Fluxion_RHI_DestroyBuffer(record->visibleStorage);
    record->objectStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->objectStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->visibleStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->visibleStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

static bool Fluxion_GPUSceneInternal_MakeObjectBuffers(FluxionGPUSceneRecord* record, u32 capacity)
{
    const usize size = (usize)capacity * sizeof(FluxionGPUSceneObject);

    FluxionRHIBufferDesc stagingDesc;
    memset(&stagingDesc, 0, sizeof(stagingDesc));
    stagingDesc.size = size;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = "Fluxion.GPUScene.ObjectStaging";

    FluxionRHIBufferDesc storageDesc;
    memset(&storageDesc, 0, sizeof(storageDesc));
    storageDesc.size = size;
    // TRANSFER_SRC as well as DST: what the engine wrote has to be
    // readable back, or a check of it can only ever be a check of the
    // picture it eventually produced.
    storageDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST | FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    storageDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    storageDesc.debugName = "Fluxion.GPUScene.ObjectStorage";

    // The visible list: one index per row at most, since in the worst
    // case everything is seen.
    FluxionRHIBufferDesc visibleStagingDesc = stagingDesc;
    visibleStagingDesc.size = (usize)capacity * sizeof(u32);
    visibleStagingDesc.debugName = "Fluxion.GPUScene.VisibleStaging";

    FluxionRHIBufferDesc visibleStorageDesc = storageDesc;
    visibleStorageDesc.size = (usize)capacity * sizeof(u32);
    visibleStorageDesc.debugName = "Fluxion.GPUScene.VisibleStorage";

    const FluxionRHIBufferHandle staging = Fluxion_RHI_CreateBuffer(record->device, &stagingDesc);
    const FluxionRHIBufferHandle storage = Fluxion_RHI_CreateBuffer(record->device, &storageDesc);
    const FluxionRHIBufferHandle visibleStaging = Fluxion_RHI_CreateBuffer(record->device, &visibleStagingDesc);
    const FluxionRHIBufferHandle visibleStorage = Fluxion_RHI_CreateBuffer(record->device, &visibleStorageDesc);

    if (!FLUXION_HANDLE_IS_VALID(staging) || !FLUXION_HANDLE_IS_VALID(storage) ||
        !FLUXION_HANDLE_IS_VALID(visibleStaging) || !FLUXION_HANDLE_IS_VALID(visibleStorage))
    {
        if (FLUXION_HANDLE_IS_VALID(staging)) Fluxion_RHI_DestroyBuffer(staging);
        if (FLUXION_HANDLE_IS_VALID(storage)) Fluxion_RHI_DestroyBuffer(storage);
        if (FLUXION_HANDLE_IS_VALID(visibleStaging)) Fluxion_RHI_DestroyBuffer(visibleStaging);
        if (FLUXION_HANDLE_IS_VALID(visibleStorage)) Fluxion_RHI_DestroyBuffer(visibleStorage);
        return false;
    }

    Fluxion_GPUSceneInternal_DestroyObjectBuffers(record);
    record->objectStaging = staging;
    record->objectStorage = storage;
    record->visibleStaging = visibleStaging;
    record->visibleStorage = visibleStorage;
    record->objectCapacity = capacity;
    record->objectStorageWritten = false;
    return true;
}

static bool Fluxion_GPUSceneInternal_MakeBatchBuffers(FluxionGPUSceneRecord* record, u32 capacity)
{
    FluxionRHIBufferDesc indirectDesc;
    memset(&indirectDesc, 0, sizeof(indirectDesc));
    indirectDesc.size = (usize)capacity * sizeof(FluxionRHIDrawIndexedIndirectCommand);
    indirectDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_INDIRECT;
    indirectDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    indirectDesc.debugName = "Fluxion.GPUScene.IndirectCommands";

    FluxionRHIBufferDesc uniformDesc;
    memset(&uniformDesc, 0, sizeof(uniformDesc));
    uniformDesc.size = (usize)capacity * FLUXION_GPU_SCENE_BATCH_UNIFORM_STRIDE;
    uniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    uniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    uniformDesc.debugName = "Fluxion.GPUScene.BatchUniforms";

    const FluxionRHIBufferHandle indirect = Fluxion_RHI_CreateBuffer(record->device, &indirectDesc);
    const FluxionRHIBufferHandle uniform = Fluxion_RHI_CreateBuffer(record->device, &uniformDesc);
    if (!FLUXION_HANDLE_IS_VALID(indirect) || !FLUXION_HANDLE_IS_VALID(uniform))
    {
        if (FLUXION_HANDLE_IS_VALID(indirect)) Fluxion_RHI_DestroyBuffer(indirect);
        if (FLUXION_HANDLE_IS_VALID(uniform)) Fluxion_RHI_DestroyBuffer(uniform);
        return false;
    }

    if (FLUXION_HANDLE_IS_VALID(record->indirectBuffer)) Fluxion_RHI_DestroyBuffer(record->indirectBuffer);
    if (FLUXION_HANDLE_IS_VALID(record->batchUniformBuffer)) Fluxion_RHI_DestroyBuffer(record->batchUniformBuffer);

    record->indirectBuffer = indirect;
    record->batchUniformBuffer = uniform;
    record->batchCapacity = capacity;
    return true;
}

static bool Fluxion_GPUSceneInternal_ReserveEntries(FluxionGPUSceneRecord* record, u32 needed)
{
    if (needed <= record->entryCapacity) return true;

    u32 capacity = record->entryCapacity != 0 ? record->entryCapacity : FLUXION_GPU_SCENE_INITIAL_OBJECTS;
    while (capacity < needed) capacity *= 2;

    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    FluxionGPUSceneEntry* entries =
        (FluxionGPUSceneEntry*)Fluxion_Allocator_Alloc(allocator, (usize)capacity * sizeof(FluxionGPUSceneEntry), FLUXION_DEFAULT_ALIGNMENT);
    u32* order = (u32*)Fluxion_Allocator_Alloc(allocator, (usize)capacity * sizeof(u32), FLUXION_DEFAULT_ALIGNMENT);
    FluxionGPUSceneBatch* batches =
        (FluxionGPUSceneBatch*)Fluxion_Allocator_Alloc(allocator, (usize)capacity * sizeof(FluxionGPUSceneBatch), FLUXION_DEFAULT_ALIGNMENT);
    FluxionRHIBindGroupHandle* bindGroups =
        (FluxionRHIBindGroupHandle*)Fluxion_Allocator_Alloc(allocator, (usize)capacity * sizeof(FluxionRHIBindGroupHandle), FLUXION_DEFAULT_ALIGNMENT);

    if (entries == NULL || order == NULL || batches == NULL || bindGroups == NULL)
    {
        if (entries != NULL) Fluxion_Allocator_Free(allocator, entries, (usize)capacity * sizeof(FluxionGPUSceneEntry));
        if (order != NULL) Fluxion_Allocator_Free(allocator, order, (usize)capacity * sizeof(u32));
        if (batches != NULL) Fluxion_Allocator_Free(allocator, batches, (usize)capacity * sizeof(FluxionGPUSceneBatch));
        if (bindGroups != NULL) Fluxion_Allocator_Free(allocator, bindGroups, (usize)capacity * sizeof(FluxionRHIBindGroupHandle));
        return false;
    }

    if (record->entries != NULL)
    {
        memcpy(entries, record->entries, (usize)record->entryCount * sizeof(FluxionGPUSceneEntry));
        Fluxion_Allocator_Free(allocator, record->entries, (usize)record->entryCapacity * sizeof(FluxionGPUSceneEntry));
        Fluxion_Allocator_Free(allocator, record->order, (usize)record->entryCapacity * sizeof(u32));
        Fluxion_Allocator_Free(allocator, record->batches, (usize)record->entryCapacity * sizeof(FluxionGPUSceneBatch));
        Fluxion_Allocator_Free(allocator, record->batchBindGroups, (usize)record->entryCapacity * sizeof(FluxionRHIBindGroupHandle));
    }

    record->entries = entries;
    record->order = order;

    // As many batches as there could be objects: the worst case is every
    // object differing from the one beside it, and a frame that hit it
    // with a smaller array would drop draws rather than be slow.
    record->batches = batches;
    record->batchBindGroups = bindGroups;
    record->entryCapacity = capacity;
    return true;
}

// A mesh's own box, in world space, as a sphere.
//
// Moved here from the shadow pass, which used to work this out per
// caster per frame: it belongs to the object, not to whoever is asking
// about it, and now one answer serves every pass.
//
// The sphere claims more room than the box did, which for throwing work
// away is the safe direction -- what survives may be outside, what is
// dropped certainly is.
static void Fluxion_GPUSceneInternal_WorldSphere(FluxionMeshBufferHandle mesh, const FluxionMat4* world, FluxionVec3* outCentre, f32* outRadius)
{
    outCentre->x = 0.0f;
    outCentre->y = 0.0f;
    outCentre->z = 0.0f;
    *outRadius = 0.0f;

    FluxionAABB bounds;
    if (!FluxionRendererInternal_MeshBuffer_GetBounds(mesh, &bounds)) return;

    const FluxionVec3 centre = { (bounds.min.x + bounds.max.x) * 0.5f,
                                 (bounds.min.y + bounds.max.y) * 0.5f,
                                 (bounds.min.z + bounds.max.z) * 0.5f };
    const FluxionVec3 extent = { (bounds.max.x - bounds.min.x) * 0.5f,
                                 (bounds.max.y - bounds.min.y) * 0.5f,
                                 (bounds.max.z - bounds.min.z) * 0.5f };

    outCentre->x = world->m[0][0] * centre.x + world->m[0][1] * centre.y + world->m[0][2] * centre.z + world->m[0][3];
    outCentre->y = world->m[1][0] * centre.x + world->m[1][1] * centre.y + world->m[1][2] * centre.z + world->m[1][3];
    outCentre->z = world->m[2][0] * centre.x + world->m[2][1] * centre.y + world->m[2][2] * centre.z + world->m[2][3];

    // The most the transform stretches anything, taken from its own
    // rows: a scale of two in one axis grows the sphere by two whichever
    // way the object was turned.
    f32 longestAxis = 0.0f;
    for (u32 row = 0; row < 3; ++row)
    {
        const f32 length = sqrtf(world->m[row][0] * world->m[row][0] +
                                 world->m[row][1] * world->m[row][1] +
                                 world->m[row][2] * world->m[row][2]);
        if (length > longestAxis) longestAxis = length;
    }

    *outRadius = sqrtf(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z) * longestAxis;
}

// Whether this frame draws this object at all.
//
// THREE TESTS, cheapest first: the layers it belongs to, then how far
// away it is, then whether it is in front of the eye at all. Each one is
// a whole object dropped before anything is uploaded or drawn -- which
// is the only kind of work that costs nothing.
//
// An object with no bounds (a mesh whose extents nobody gave) is always
// drawn: not knowing where something is, is not a reason to decide it is
// elsewhere.
static bool Fluxion_GPUSceneInternal_IsVisible(const FluxionGPUSceneRecord* record, const FluxionGPUSceneEntry* entry,
                                               const FluxionFrustumPlanes* frustum)
{
    if (!record->cull.enabled) return true;

    const u32 viewLayers = record->cull.layerMask != 0 ? record->cull.layerMask : 0xFFFFFFFFu;
    const u32 objectLayers = entry->layerMask != 0 ? entry->layerMask : 0xFFFFFFFFu;
    if ((viewLayers & objectLayers) == 0) return false;

    if (entry->boundsRadius <= 0.0f) return true;

    if (record->cull.cullDistance > 0.0f)
    {
        const f32 dx = entry->boundsCentre.x - record->cull.cameraPosition.x;
        const f32 dy = entry->boundsCentre.y - record->cull.cameraPosition.y;
        const f32 dz = entry->boundsCentre.z - record->cull.cameraPosition.z;

        // Measured to the NEAR SIDE of the sphere, not to its centre: a
        // large object whose centre is past the limit may still have
        // half of it in view, and dropping that is a wall vanishing
        // while you walk towards it.
        const f32 distance = sqrtf(dx * dx + dy * dy + dz * dz) - entry->boundsRadius;
        if (distance > record->cull.cullDistance) return false;
    }

    return Fluxion_Frustum_TouchesSphere(frustum, entry->boundsCentre, entry->boundsRadius);
}

// --- The API --------------------------------------------------------------

FluxionGPUSceneHandle Fluxion_GPUScene_Create(FluxionRHIDeviceHandle device)
{
    FluxionGPUSceneHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    u32 index = FLUXION_GPU_SCENE_MAX_SCENES;
    for (u32 i = 0; i < FLUXION_GPU_SCENE_MAX_SCENES; ++i)
    {
        if (!s_scenes[i].alive) { index = i; break; }
    }
    if (index == FLUXION_GPU_SCENE_MAX_SCENES) return invalid;

    FluxionGPUSceneRecord* record = &s_scenes[index];
    const u32 generation = record->generation + 1;
    memset(record, 0, sizeof(*record));
    record->device = device;
    record->generation = generation;

    // A ZEROED HANDLE IS NOT AN INVALID ONE. FLUXION_HANDLE_IS_VALID only
    // asks whether the index is the invalid sentinel, and the memset
    // above leaves every one of these looking like a valid handle
    // pointing at whatever really occupies slot zero of the buffer pool.
    // Without this, the first grow would destroy somebody else's buffers
    // -- which is not a compile error, a warning, or even a wrong
    // picture: it is a crash somewhere else entirely.
    record->objectStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->objectStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->visibleStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->visibleStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->indirectBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->batchUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (!Fluxion_GPUSceneInternal_MakeObjectBuffers(record, FLUXION_GPU_SCENE_INITIAL_OBJECTS) ||
        !Fluxion_GPUSceneInternal_MakeBatchBuffers(record, FLUXION_GPU_SCENE_INITIAL_OBJECTS) ||
        !Fluxion_GPUSceneInternal_ReserveEntries(record, FLUXION_GPU_SCENE_INITIAL_OBJECTS))
    {
        FLUXION_LOG_ERROR(FLUXION_GPU_SCENE_LOG_CATEGORY, "the frame's object buffers could not be made");
        Fluxion_GPUSceneInternal_DestroyObjectBuffers(record);
        memset(record, 0, sizeof(*record));
        record->generation = generation;
        return invalid;
    }

    record->alive = true;

    FluxionGPUSceneHandle handle = { index, generation };
    return handle;
}

void Fluxion_GPUScene_Destroy(FluxionGPUSceneHandle scene)
{
    FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL) return;

    Fluxion_GPUSceneInternal_DestroyObjectBuffers(record);
    if (FLUXION_HANDLE_IS_VALID(record->indirectBuffer)) Fluxion_RHI_DestroyBuffer(record->indirectBuffer);
    if (FLUXION_HANDLE_IS_VALID(record->batchUniformBuffer)) Fluxion_RHI_DestroyBuffer(record->batchUniformBuffer);

    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    if (record->entries != NULL)
    {
        Fluxion_Allocator_Free(allocator, record->entries, (usize)record->entryCapacity * sizeof(FluxionGPUSceneEntry));
        Fluxion_Allocator_Free(allocator, record->order, (usize)record->entryCapacity * sizeof(u32));
        Fluxion_Allocator_Free(allocator, record->batches, (usize)record->entryCapacity * sizeof(FluxionGPUSceneBatch));
        Fluxion_Allocator_Free(allocator, record->batchBindGroups, (usize)record->entryCapacity * sizeof(FluxionRHIBindGroupHandle));
    }

    const u32 generation = record->generation;
    memset(record, 0, sizeof(*record));
    record->generation = generation;

    // Zeroed is not invalid -- see Create. A slot waiting to be reused
    // must not look like it holds buffer zero.
    record->objectStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->objectStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->visibleStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->visibleStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->indirectBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->batchUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

void Fluxion_GPUScene_Begin(FluxionGPUSceneHandle scene)
{
    FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL) return;

    // LAST frame's bind groups, let go of now rather than when they were
    // recorded: by the time a frame begins, the one before it has been
    // submitted, and a descriptor freed while its command list was still
    // being built would have been handed straight to the next batch.
    for (u32 i = 0; i < record->batchBindGroupCount; ++i)
    {
        if (FLUXION_HANDLE_IS_VALID(record->batchBindGroups[i])) Fluxion_RHI_DestroyBindGroup(record->batchBindGroups[i]);
    }
    record->batchBindGroupCount = 0;

    record->entryCount = 0;
    record->batchCount = 0;
    record->visibleCount = 0;

    // A frame nobody tells about its camera draws everything. That is
    // not a fallback, it is what a test or a tool asks for.
    memset(&record->cull, 0, sizeof(record->cull));
}

void Fluxion_GPUScene_SetCulling(FluxionGPUSceneHandle scene, const FluxionGPUSceneCullDesc* cull)
{
    FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL || cull == NULL) return;
    record->cull = *cull;
}

u32 Fluxion_GPUScene_GetVisibleCount(FluxionGPUSceneHandle scene)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    return record != NULL ? record->visibleCount : 0;
}

bool Fluxion_GPUScene_Add(FluxionGPUSceneHandle scene, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                          FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform)
{
    return Fluxion_GPUScene_AddLayered(scene, mesh, material, pipeline, transform, 0xFFFFFFFFu);
}

bool Fluxion_GPUScene_AddLayered(FluxionGPUSceneHandle scene, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                                 FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform, u32 layerMask)
{
    FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL) return false;

    if (!Fluxion_GPUSceneInternal_ReserveEntries(record, record->entryCount + 1))
    {
        FLUXION_LOG_ERROR(FLUXION_GPU_SCENE_LOG_CATEGORY, "there was no room for another object this frame; it was not drawn");
        return false;
    }

    FluxionGPUSceneEntry* entry = &record->entries[record->entryCount++];
    entry->mesh = mesh;
    entry->material = material;
    entry->pipeline = pipeline;

    const FluxionMat4 world = transform != NULL ? *transform : Fluxion_Mat4_Identity();
    Fluxion_GPUSceneInternal_WorldSphere(mesh, &world, &entry->boundsCentre, &entry->boundsRadius);

    // Transposed here, at the boundary, exactly as the frame constants
    // are: callers hand over ordinary row-major matrices, and both
    // shading languages read a uniform matrix column-major. The bounds
    // above are worked out from the matrix as it arrived, because that
    // is the one the arithmetic is written for.
    entry->model = Fluxion_Mat4_Transposed(world);
    entry->layerMask = layerMask;
    return true;
}

// What decides which objects sit next to each other.
//
// Pipeline first because switching one is the most expensive thing a
// draw can ask for, then the material's bind group, then the mesh's
// buffers. The entry's own position breaks every remaining tie, which is
// what makes the order the same from one frame to the next -- an order
// that wobbled would move objects between batches for no reason anybody
// could see.
static int Fluxion_GPUSceneInternal_CompareEntries(const void* leftIndex, const void* rightIndex, void* context)
{
    const FluxionGPUSceneRecord* record = (const FluxionGPUSceneRecord*)context;
    const u32 a = *(const u32*)leftIndex;
    const u32 b = *(const u32*)rightIndex;
    const FluxionGPUSceneEntry* left = &record->entries[a];
    const FluxionGPUSceneEntry* right = &record->entries[b];

    if (left->pipeline.index != right->pipeline.index) return left->pipeline.index < right->pipeline.index ? -1 : 1;
    if (left->material.index != right->material.index) return left->material.index < right->material.index ? -1 : 1;
    if (left->mesh.index != right->mesh.index) return left->mesh.index < right->mesh.index ? -1 : 1;
    return a < b ? -1 : (a > b ? 1 : 0);
}

// A plain insertion sort over the index array.
//
// qsort would need either a global to carry the record (not thread-safe)
// or the non-portable qsort_r, whose argument order differs between the
// C library on one of these platforms and the others -- and the frames
// this engine draws are hundreds of objects, not millions, so the
// difference does not show up in a measurement.
static void Fluxion_GPUSceneInternal_Sort(FluxionGPUSceneRecord* record)
{
    for (u32 i = 0; i < record->entryCount; ++i) record->order[i] = i;

    for (u32 i = 1; i < record->entryCount; ++i)
    {
        const u32 key = record->order[i];
        u32 j = i;
        while (j > 0 && Fluxion_GPUSceneInternal_CompareEntries(&record->order[j - 1], &key, record) > 0)
        {
            record->order[j] = record->order[j - 1];
            --j;
        }
        record->order[j] = key;
    }
}

void Fluxion_GPUScene_Upload(FluxionGPUSceneHandle scene, FluxionRHICommandListHandle commandList,
                             FluxionRHIBindGroupLayoutHandle objectLayout)
{
    FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL) return;

    record->batchCount = 0;
    if (record->entryCount == 0) return;

    if (record->entryCount > record->objectCapacity)
    {
        u32 capacity = record->objectCapacity != 0 ? record->objectCapacity : FLUXION_GPU_SCENE_INITIAL_OBJECTS;
        while (capacity < record->entryCount) capacity *= 2;
        if (!Fluxion_GPUSceneInternal_MakeObjectBuffers(record, capacity)) return;
    }
    if (record->entryCount > record->batchCapacity)
    {
        u32 capacity = record->batchCapacity != 0 ? record->batchCapacity : FLUXION_GPU_SCENE_INITIAL_OBJECTS;
        while (capacity < record->entryCount) capacity *= 2;
        if (!Fluxion_GPUSceneInternal_MakeBatchBuffers(record, capacity)) return;
    }

    Fluxion_GPUSceneInternal_Sort(record);

    // The rows, in the sorted order -- which is what makes a batch a RUN
    // of rows rather than a list of them.
    FluxionGPUSceneObject* rows = (FluxionGPUSceneObject*)Fluxion_RHI_MapBuffer(record->objectStaging);
    if (rows == NULL) return;

    for (u32 i = 0; i < record->entryCount; ++i)
    {
        rows[i].model = record->entries[record->order[i]].model;
    }
    Fluxion_RHI_UnmapBuffer(record->objectStaging);

    // The groups, found by walking the sorted order and starting a new
    // one wherever the key changes.
    for (u32 i = 0; i < record->entryCount; ++i)
    {
        const FluxionGPUSceneEntry* entry = &record->entries[record->order[i]];

        bool continues = record->batchCount > 0;
        if (continues)
        {
            const FluxionGPUSceneBatch* current = &record->batches[record->batchCount - 1];
            continues = current->pipeline.index == entry->pipeline.index && current->pipeline.generation == entry->pipeline.generation &&
                        current->material.index == entry->material.index && current->material.generation == entry->material.generation &&
                        current->mesh.index == entry->mesh.index && current->mesh.generation == entry->mesh.generation;
        }

        if (continues)
        {
            FluxionGPUSceneBatch* current = &record->batches[record->batchCount - 1];
            ++current->objectCount;

            // The two spheres, as one that holds both. Not the tightest
            // sphere that could hold them -- that is an iterative
            // answer -- but one that certainly does, which is what a
            // cull needs.
            const f32 dx = entry->boundsCentre.x - current->boundsCentre.x;
            const f32 dy = entry->boundsCentre.y - current->boundsCentre.y;
            const f32 dz = entry->boundsCentre.z - current->boundsCentre.z;
            const f32 distance = sqrtf(dx * dx + dy * dy + dz * dz);

            if (distance + entry->boundsRadius > current->boundsRadius)
            {
                const f32 grown = (distance + entry->boundsRadius + current->boundsRadius) * 0.5f;
                if (distance > 0.0f)
                {
                    const f32 shift = (grown - current->boundsRadius) / distance;
                    current->boundsCentre.x += dx * shift;
                    current->boundsCentre.y += dy * shift;
                    current->boundsCentre.z += dz * shift;
                }
                current->boundsRadius = grown;
            }
            continue;
        }

        FluxionGPUSceneBatch* batch = &record->batches[record->batchCount++];
        batch->mesh = entry->mesh;
        batch->material = entry->material;
        batch->pipeline = entry->pipeline;
        batch->firstObject = i;
        batch->objectCount = 1;
        batch->boundsCentre = entry->boundsCentre;
        batch->boundsRadius = entry->boundsRadius;
    }

    // WHAT IS ACTUALLY DRAWN, decided here, one object at a time.
    //
    // The rows above hold everything; this list holds the ones this
    // frame can see, gathered so that each batch's survivors sit
    // together. A batch that lost every object keeps its rows and draws
    // none of them.
    const FluxionFrustumPlanes frustum = Fluxion_Mat4_FrustumPlanes(record->cull.viewProjection);

    u32* visible = (u32*)Fluxion_RHI_MapBuffer(record->visibleStaging);
    if (visible == NULL) return;

    record->visibleCount = 0;
    for (u32 batchIndex = 0; batchIndex < record->batchCount; ++batchIndex)
    {
        FluxionGPUSceneBatch* batch = &record->batches[batchIndex];
        batch->firstVisible = record->visibleCount;
        batch->visibleCount = 0;

        for (u32 i = 0; i < batch->objectCount; ++i)
        {
            const u32 row = batch->firstObject + i;
            if (!Fluxion_GPUSceneInternal_IsVisible(record, &record->entries[record->order[row]], &frustum)) continue;

            visible[record->visibleCount++] = row;
            ++batch->visibleCount;
        }
    }
    Fluxion_RHI_UnmapBuffer(record->visibleStaging);

    // One command per batch, and one uniform slice saying where that
    // batch's slice of the visible list begins.
    FluxionRHIDrawIndexedIndirectCommand* commands =
        (FluxionRHIDrawIndexedIndirectCommand*)Fluxion_RHI_MapBuffer(record->indirectBuffer);
    u8* uniforms = (u8*)Fluxion_RHI_MapBuffer(record->batchUniformBuffer);
    if (commands == NULL || uniforms == NULL)
    {
        if (commands != NULL) Fluxion_RHI_UnmapBuffer(record->indirectBuffer);
        if (uniforms != NULL) Fluxion_RHI_UnmapBuffer(record->batchUniformBuffer);
        record->batchCount = 0;
        return;
    }

    for (u32 i = 0; i < record->batchCount; ++i)
    {
        const FluxionGPUSceneBatch* batch = &record->batches[i];

        FluxionRHIBufferHandle vertexBuffer, indexBuffer;
        u32 vertexCount, indexCount;
        bool use16BitIndices;
        FluxionRHIVertexLayout vertexLayout;
        if (!FluxionRendererInternal_MeshBuffer_Get(batch->mesh, &vertexBuffer, &indexBuffer, &vertexCount, &indexCount, &use16BitIndices, &vertexLayout))
        {
            indexCount = 0;
        }

        commands[i].indexCount = indexCount;
        commands[i].instanceCount = batch->visibleCount;
        commands[i].firstIndex = 0;
        commands[i].vertexOffset = 0;

        // ALWAYS ZERO. A non-zero start instance means one thing to
        // HLSL's instance built-in and another to Vulkan's, and the
        // engine's answer to that disagreement is to never have one --
        // which is what makes Fluxion/Object.jsl's index arithmetic the
        // same on every backend.
        commands[i].firstInstance = 0;

        FluxionVec4 params = { (f32)batch->firstVisible, 0.0f, 0.0f, 0.0f };
        memcpy(uniforms + (usize)i * FLUXION_GPU_SCENE_BATCH_UNIFORM_STRIDE, &params, sizeof(params));
    }

    Fluxion_RHI_UnmapBuffer(record->indirectBuffer);
    Fluxion_RHI_UnmapBuffer(record->batchUniformBuffer);

    // One bind group per batch, made here rather than in each pass: two
    // passes drawing the same frame then bind the same thing, and each
    // group outlives the recording that points at it.
    if (FLUXION_HANDLE_IS_VALID(objectLayout))
    {
        for (u32 i = 0; i < record->batchCount; ++i)
        {
            FluxionRHIBindGroupEntry entries[3];
            memset(entries, 0, sizeof(entries));

            entries[0].binding = 0;
            entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
            entries[0].buffer = record->batchUniformBuffer;
            entries[0].bufferOffset = (usize)i * FLUXION_GPU_SCENE_BATCH_UNIFORM_STRIDE;
            entries[0].bufferSize = FLUXION_GPU_SCENE_BATCH_UNIFORM_STRIDE;

            entries[1].binding = 1;
            entries[1].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
            entries[1].buffer = record->objectStorage;
            entries[1].bufferOffset = 0;
            entries[1].bufferSize = 0; // the whole of it: the shader indexes rather than slices

            // What one row IS -- the same bytes Fluxion/Object.jsl
            // declares. One backend describes a buffer view by element
            // rather than by byte, and it has no other way to find out.
            entries[1].bufferElementStride = (u32)sizeof(FluxionGPUSceneObject);

            entries[2].binding = 2;
            entries[2].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
            entries[2].buffer = record->visibleStorage;
            entries[2].bufferOffset = 0;
            entries[2].bufferSize = 0;
            entries[2].bufferElementStride = (u32)sizeof(u32);

            FluxionRHIBindGroupDesc bindGroupDesc;
            bindGroupDesc.layout = objectLayout;
            bindGroupDesc.entries = entries;
            bindGroupDesc.entryCount = 3;

            record->batchBindGroups[i] = Fluxion_RHI_CreateBindGroup(record->device, &bindGroupDesc);
        }
        record->batchBindGroupCount = record->batchCount;
    }


    // And the rows across to where a shader may read them. Recorded, not
    // written: what a vertex stage reads is device memory.
    //
    // WITH BARRIERS ON BOTH SIDES, and that is not ceremony: one backend
    // promotes a buffer's state on its own and the other two do not, so
    // without these the rows are read before they arrive on exactly the
    // backends that do not -- as geometry collapsed onto the origin,
    // which reads as nothing drawn rather than as a synchronisation
    // fault.
    const FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBarrier toCopy = { noTexture, record->objectStorage,
                                 record->objectStorageWritten ? FLUXION_RHI_RESOURCE_STATE_SHADER_READ : FLUXION_RHI_RESOURCE_STATE_COMMON,
                                 FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
    Fluxion_RHI_CommandList_Barrier(commandList, &toCopy, 1);

    Fluxion_RHI_CommandList_CopyBuffer(commandList, record->objectStaging, 0, record->objectStorage, 0,
                                       (usize)record->entryCount * sizeof(FluxionGPUSceneObject));

    if (record->visibleCount > 0)
    {
        FluxionRHIBarrier visibleToCopy = { noTexture, record->visibleStorage,
                                            record->objectStorageWritten ? FLUXION_RHI_RESOURCE_STATE_SHADER_READ : FLUXION_RHI_RESOURCE_STATE_COMMON,
                                            FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
        Fluxion_RHI_CommandList_Barrier(commandList, &visibleToCopy, 1);

        Fluxion_RHI_CommandList_CopyBuffer(commandList, record->visibleStaging, 0, record->visibleStorage, 0,
                                           (usize)record->visibleCount * sizeof(u32));

        FluxionRHIBarrier visibleToRead = { noTexture, record->visibleStorage,
                                            FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
        Fluxion_RHI_CommandList_Barrier(commandList, &visibleToRead, 1);
    }

    FluxionRHIBarrier toRead = { noTexture, record->objectStorage,
                                 FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);
    record->objectStorageWritten = true;
}

u32 Fluxion_GPUScene_GetObjectCount(FluxionGPUSceneHandle scene)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    return record != NULL ? record->entryCount : 0;
}

u32 Fluxion_GPUScene_GetBatchCount(FluxionGPUSceneHandle scene)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    return record != NULL ? record->batchCount : 0;
}

const FluxionGPUSceneBatch* Fluxion_GPUScene_GetBatch(FluxionGPUSceneHandle scene, u32 batchIndex)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL || batchIndex >= record->batchCount) return NULL;
    return &record->batches[batchIndex];
}

FluxionRHIBufferHandle Fluxion_GPUScene_GetObjectBuffer(FluxionGPUSceneHandle scene)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL)
    {
        FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return invalid;
    }
    return record->objectStorage;
}

FluxionRHIBufferHandle Fluxion_GPUScene_GetVisibleBuffer(FluxionGPUSceneHandle scene)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL)
    {
        FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return invalid;
    }
    return record->visibleStorage;
}

FluxionRHIBufferHandle Fluxion_GPUScene_GetIndirectBuffer(FluxionGPUSceneHandle scene)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL)
    {
        FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return invalid;
    }
    return record->indirectBuffer;
}

FluxionRHIBindGroupHandle Fluxion_GPUScene_GetBatchBindGroup(FluxionGPUSceneHandle scene, u32 batchIndex)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL || batchIndex >= record->batchBindGroupCount)
    {
        FluxionRHIBindGroupHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return invalid;
    }
    return record->batchBindGroups[batchIndex];
}

FluxionRHIBufferHandle Fluxion_GPUScene_GetBatchUniformBuffer(FluxionGPUSceneHandle scene)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL)
    {
        FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return invalid;
    }
    return record->batchUniformBuffer;
}
