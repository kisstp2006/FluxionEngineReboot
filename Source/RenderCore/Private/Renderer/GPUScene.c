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

    // And one more per batch, differing in a single binding: the list of
    // rows it reads. A pass that draws what the CAMERA can see binds the
    // group above; a pass that draws everything a batch HOLDS -- the
    // shadow pass, whose casters are not the camera's business -- binds
    // this one.
    FluxionRHIBindGroupHandle* allRowsBindGroups;

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

    // The list that is no list at all: row zero at index zero, row one
    // at index one, all the way up. What it is FOR is that the shading
    // language reads every row through the visible list, so a pass that
    // wants all of them still needs a list -- and one that says "as they
    // are" costs a buffer written once instead of a second shader path.
    FluxionRHIBufferHandle everyRowStaging;
    FluxionRHIBufferHandle everyRowStorage;
    bool everyRowUploaded;

    // The commands themselves, and one small uniform slice per batch
    // holding where that batch's rows begin. Both are read by the device
    // and written by the processor, and neither is a storage buffer, so
    // both can be host-visible and need no copy.
    FluxionRHIBufferHandle indirectStaging;
    FluxionRHIBufferHandle indirectBuffer;
    FluxionRHIBufferHandle batchUniformBuffer;
    u32 batchCapacity;

    // The commands on their way BACK, for the one thing the processor
    // cannot work out on the device's path: how many objects survived.
    // Read at the start of a later frame, never at the end of the one
    // that wrote it -- a map that waited for the device would be a stall
    // in the middle of the frame, in exchange for a number nothing draws
    // with.
    FluxionRHIBufferHandle commandReadback;
    u32 readbackBatchCount;
    bool readbackPending;
    u32 deviceVisibleCount;

    // Whether the commands have ever been copied across -- the same
    // question, and for the same reason, as objectStorageWritten.
    bool indirectWritten;

    // What this frame can see, and how many objects survived it.
    FluxionGPUSceneCullDesc cull;
    u32 visibleCount;

    // Whether the last upload left the counting to the device -- what a
    // pass asks before it believes a batch's visibleCount.
    bool countsOnDevice;

    // --- the device's own way of deciding that ------------------------
    //
    // Built the first time a frame asks for it, and not before: a
    // program that never culls on the device never compiles this.
    FluxionShaderProgramHandle cullProgram;
    FluxionRHIBindGroupLayoutHandle cullLayout;
    FluxionRHIPipelineHandle cullPipeline;
    bool cullPipelineFailed;

    // What the compute reads about each object, and where each batch's
    // slice begins. Written by the processor, read by the device --
    // staged and copied like everything else it reads.
    FluxionRHIBufferHandle cullSphereStaging;
    FluxionRHIBufferHandle cullSphereStorage;
    FluxionRHIBufferHandle cullBatchStaging;
    FluxionRHIBufferHandle cullBatchStorage;
    FluxionRHIBufferHandle cullLayerStaging;
    FluxionRHIBufferHandle cullLayerStorage;
    FluxionRHIBufferHandle cullBatchFirstStaging;
    FluxionRHIBufferHandle cullBatchFirstStorage;
    FluxionRHIBufferHandle cullUniform;

    // The bind group the cull pass runs with. ONE, kept, rather than one
    // a frame: every binding in it is the same buffer every frame -- what
    // changes is what those buffers hold. Rebuilding it each frame was
    // measured to exhaust the descriptor pool on one backend within a
    // second, and a frame whose bind group could not be made is a frame
    // that culls nothing and draws nothing.
    FluxionRHIBindGroupHandle cullBindGroup;

    // Whether the failure below has already been said once. Said at all,
    // because a frame that cannot bind its cull pass draws nothing and
    // has nothing else to point at; said once, because a pool that is
    // full this frame is full the next one too.
    bool cullBindGroupFailed;

    u32 cullObjectCapacity;
    u32 cullBatchCapacity;
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

// Let go of the cull pass's bind group, because a buffer it points at is
// about to be a different buffer.
static void Fluxion_GPUSceneInternal_ForgetCullBindGroup(FluxionGPUSceneRecord* record)
{
    if (FLUXION_HANDLE_IS_VALID(record->cullBindGroup)) Fluxion_RHI_DestroyBindGroup(record->cullBindGroup);
    record->cullBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

static void Fluxion_GPUSceneInternal_DestroyObjectBuffers(FluxionGPUSceneRecord* record)
{
    if (FLUXION_HANDLE_IS_VALID(record->objectStaging)) Fluxion_RHI_DestroyBuffer(record->objectStaging);
    if (FLUXION_HANDLE_IS_VALID(record->objectStorage)) Fluxion_RHI_DestroyBuffer(record->objectStorage);
    if (FLUXION_HANDLE_IS_VALID(record->visibleStaging)) Fluxion_RHI_DestroyBuffer(record->visibleStaging);
    if (FLUXION_HANDLE_IS_VALID(record->visibleStorage)) Fluxion_RHI_DestroyBuffer(record->visibleStorage);
    if (FLUXION_HANDLE_IS_VALID(record->everyRowStaging)) Fluxion_RHI_DestroyBuffer(record->everyRowStaging);
    if (FLUXION_HANDLE_IS_VALID(record->everyRowStorage)) Fluxion_RHI_DestroyBuffer(record->everyRowStorage);
    record->everyRowStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->everyRowStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
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

    FluxionRHIBufferDesc everyRowStagingDesc = visibleStagingDesc;
    everyRowStagingDesc.debugName = "Fluxion.GPUScene.EveryRowStaging";

    FluxionRHIBufferDesc everyRowStorageDesc = visibleStorageDesc;
    everyRowStorageDesc.debugName = "Fluxion.GPUScene.EveryRowStorage";

    const FluxionRHIBufferHandle everyRowStaging = Fluxion_RHI_CreateBuffer(record->device, &everyRowStagingDesc);
    const FluxionRHIBufferHandle everyRowStorage = Fluxion_RHI_CreateBuffer(record->device, &everyRowStorageDesc);

    if (!FLUXION_HANDLE_IS_VALID(staging) || !FLUXION_HANDLE_IS_VALID(storage) ||
        !FLUXION_HANDLE_IS_VALID(visibleStaging) || !FLUXION_HANDLE_IS_VALID(visibleStorage) ||
        !FLUXION_HANDLE_IS_VALID(everyRowStaging) || !FLUXION_HANDLE_IS_VALID(everyRowStorage))
    {
        if (FLUXION_HANDLE_IS_VALID(staging)) Fluxion_RHI_DestroyBuffer(staging);
        if (FLUXION_HANDLE_IS_VALID(storage)) Fluxion_RHI_DestroyBuffer(storage);
        if (FLUXION_HANDLE_IS_VALID(visibleStaging)) Fluxion_RHI_DestroyBuffer(visibleStaging);
        if (FLUXION_HANDLE_IS_VALID(visibleStorage)) Fluxion_RHI_DestroyBuffer(visibleStorage);
        if (FLUXION_HANDLE_IS_VALID(everyRowStaging)) Fluxion_RHI_DestroyBuffer(everyRowStaging);
        if (FLUXION_HANDLE_IS_VALID(everyRowStorage)) Fluxion_RHI_DestroyBuffer(everyRowStorage);
        return false;
    }

    Fluxion_GPUSceneInternal_DestroyObjectBuffers(record);
    Fluxion_GPUSceneInternal_ForgetCullBindGroup(record);
    record->objectStaging = staging;
    record->objectStorage = storage;
    record->visibleStaging = visibleStaging;
    record->visibleStorage = visibleStorage;
    record->everyRowStaging = everyRowStaging;
    record->everyRowStorage = everyRowStorage;
    record->objectCapacity = capacity;
    record->objectStorageWritten = false;

    // Written once, here, because it never changes: index n holds n.
    // Copied across by the next upload -- a copy is a recorded command,
    // and there is no command list in scope at this point.
    record->everyRowUploaded = false;

    u32* everyRow = (u32*)Fluxion_RHI_MapBuffer(record->everyRowStaging);
    if (everyRow != NULL)
    {
        for (u32 i = 0; i < capacity; ++i) everyRow[i] = i;
        Fluxion_RHI_UnmapBuffer(record->everyRowStaging);
    }
    return true;
}

static bool Fluxion_GPUSceneInternal_MakeBatchBuffers(FluxionGPUSceneRecord* record, u32 capacity)
{
    // DEVICE MEMORY, with a staged copy -- not because the processor
    // cannot write these, but because the culling compute pass has to be
    // able to: a buffer a shader writes cannot live in the memory the
    // processor maps, on the backend that decides that.
    //
    // So the commands are always written the same way: the processor
    // fills in what it knows (which indices, how many), the copy takes
    // them across, and whoever decides visibility fills in the instance
    // count.
    FluxionRHIBufferDesc indirectStagingDesc;
    memset(&indirectStagingDesc, 0, sizeof(indirectStagingDesc));
    indirectStagingDesc.size = (usize)capacity * sizeof(FluxionRHIDrawIndexedIndirectCommand);
    indirectStagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    indirectStagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    indirectStagingDesc.debugName = "Fluxion.GPUScene.IndirectStaging";

    FluxionRHIBufferDesc indirectDesc;
    memset(&indirectDesc, 0, sizeof(indirectDesc));
    indirectDesc.size = (usize)capacity * sizeof(FluxionRHIDrawIndexedIndirectCommand);
    indirectDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_INDIRECT | FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER |
                              FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST | FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    indirectDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    indirectDesc.debugName = "Fluxion.GPUScene.IndirectCommands";

    FluxionRHIBufferDesc readbackDesc;
    memset(&readbackDesc, 0, sizeof(readbackDesc));
    readbackDesc.size = (usize)capacity * sizeof(FluxionRHIDrawIndexedIndirectCommand);
    readbackDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
    readbackDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU;
    readbackDesc.debugName = "Fluxion.GPUScene.IndirectReadback";

    FluxionRHIBufferDesc uniformDesc;
    memset(&uniformDesc, 0, sizeof(uniformDesc));
    uniformDesc.size = (usize)capacity * FLUXION_GPU_SCENE_BATCH_UNIFORM_STRIDE;
    uniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    uniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    uniformDesc.debugName = "Fluxion.GPUScene.BatchUniforms";

    const FluxionRHIBufferHandle indirectStaging = Fluxion_RHI_CreateBuffer(record->device, &indirectStagingDesc);
    const FluxionRHIBufferHandle indirect = Fluxion_RHI_CreateBuffer(record->device, &indirectDesc);
    const FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(record->device, &readbackDesc);
    const FluxionRHIBufferHandle uniform = Fluxion_RHI_CreateBuffer(record->device, &uniformDesc);
    if (!FLUXION_HANDLE_IS_VALID(indirectStaging) || !FLUXION_HANDLE_IS_VALID(indirect) || !FLUXION_HANDLE_IS_VALID(readback) ||
        !FLUXION_HANDLE_IS_VALID(uniform))
    {
        if (FLUXION_HANDLE_IS_VALID(indirectStaging)) Fluxion_RHI_DestroyBuffer(indirectStaging);
        if (FLUXION_HANDLE_IS_VALID(indirect)) Fluxion_RHI_DestroyBuffer(indirect);
        if (FLUXION_HANDLE_IS_VALID(readback)) Fluxion_RHI_DestroyBuffer(readback);
        if (FLUXION_HANDLE_IS_VALID(uniform)) Fluxion_RHI_DestroyBuffer(uniform);
        return false;
    }

    if (FLUXION_HANDLE_IS_VALID(record->indirectStaging)) Fluxion_RHI_DestroyBuffer(record->indirectStaging);
    if (FLUXION_HANDLE_IS_VALID(record->indirectBuffer)) Fluxion_RHI_DestroyBuffer(record->indirectBuffer);
    if (FLUXION_HANDLE_IS_VALID(record->commandReadback)) Fluxion_RHI_DestroyBuffer(record->commandReadback);
    if (FLUXION_HANDLE_IS_VALID(record->batchUniformBuffer)) Fluxion_RHI_DestroyBuffer(record->batchUniformBuffer);
    Fluxion_GPUSceneInternal_ForgetCullBindGroup(record);

    record->indirectStaging = indirectStaging;
    record->indirectBuffer = indirect;
    record->commandReadback = readback;
    record->batchUniformBuffer = uniform;
    record->batchCapacity = capacity;
    record->indirectWritten = false;

    // The old buffer's contents went with it.
    record->readbackPending = false;
    record->readbackBatchCount = 0;
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

    FluxionRHIBindGroupHandle* allRowsGroups =
        (FluxionRHIBindGroupHandle*)Fluxion_Allocator_Alloc(allocator, (usize)capacity * sizeof(FluxionRHIBindGroupHandle), FLUXION_DEFAULT_ALIGNMENT);

    if (entries == NULL || order == NULL || batches == NULL || bindGroups == NULL || allRowsGroups == NULL)
    {
        if (entries != NULL) Fluxion_Allocator_Free(allocator, entries, (usize)capacity * sizeof(FluxionGPUSceneEntry));
        if (order != NULL) Fluxion_Allocator_Free(allocator, order, (usize)capacity * sizeof(u32));
        if (batches != NULL) Fluxion_Allocator_Free(allocator, batches, (usize)capacity * sizeof(FluxionGPUSceneBatch));
        if (bindGroups != NULL) Fluxion_Allocator_Free(allocator, bindGroups, (usize)capacity * sizeof(FluxionRHIBindGroupHandle));
        if (allRowsGroups != NULL) Fluxion_Allocator_Free(allocator, allRowsGroups, (usize)capacity * sizeof(FluxionRHIBindGroupHandle));
        return false;
    }

    if (record->entries != NULL)
    {
        memcpy(entries, record->entries, (usize)record->entryCount * sizeof(FluxionGPUSceneEntry));
        Fluxion_Allocator_Free(allocator, record->entries, (usize)record->entryCapacity * sizeof(FluxionGPUSceneEntry));
        Fluxion_Allocator_Free(allocator, record->order, (usize)record->entryCapacity * sizeof(u32));
        Fluxion_Allocator_Free(allocator, record->batches, (usize)record->entryCapacity * sizeof(FluxionGPUSceneBatch));
        Fluxion_Allocator_Free(allocator, record->batchBindGroups, (usize)record->entryCapacity * sizeof(FluxionRHIBindGroupHandle));
        Fluxion_Allocator_Free(allocator, record->allRowsBindGroups, (usize)record->entryCapacity * sizeof(FluxionRHIBindGroupHandle));
    }

    record->entries = entries;
    record->order = order;

    // As many batches as there could be objects: the worst case is every
    // object differing from the one beside it, and a frame that hit it
    // with a smaller array would drop draws rather than be slow.
    record->batches = batches;
    record->batchBindGroups = bindGroups;
    record->allRowsBindGroups = allRowsGroups;

    // A ZEROED HANDLE IS NOT AN INVALID ONE -- see the same note in
    // Create. These are freed by index range rather than one by one, and
    // a slot nobody filled must not look like it holds group zero.
    for (u32 i = 0; i < capacity; ++i)
    {
        record->batchBindGroups[i] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        record->allRowsBindGroups[i] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

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

// --- Deciding it on the device --------------------------------------------

// What the cull compute reads, in the shape Fluxion/Pass/GPUCull.jsl
// declares -- six frustum planes, the eye, and how many objects there
// are. Two statements of one layout, and the shader's is the one a
// driver reads.
typedef struct FluxionGPUSceneCullUniform
{
    FluxionVec4 planes[6];
    FluxionVec4 eye;    // xyz where it is, w how far it sees
    FluxionVec4 counts; // x how many objects
} FluxionGPUSceneCullUniform;

// The bindings the pass declares, in the order the shader compiler hands
// them out: the group's uniform block first, then its storage buffers in
// the order they are declared.
static FluxionRHIBindGroupLayoutDesc Fluxion_GPUSceneInternal_MakeCullLayoutDesc(void)
{
    FluxionRHIBindGroupLayoutDesc desc = { 0 };

    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    for (u32 i = 1; i <= 6; ++i)
    {
        desc.entries[i].binding = i;
        desc.entries[i].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
        desc.entries[i].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;
    }

    desc.entryCount = 7;
    desc.debugName = "Fluxion.GPUScene.CullBindGroupLayout";
    return desc;
}

static bool Fluxion_GPUSceneInternal_EnsureCullPipeline(FluxionGPUSceneRecord* record)
{
    if (FLUXION_HANDLE_IS_VALID(record->cullPipeline)) return true;
    if (record->cullPipelineFailed) return false;

    // The whole shader is one line: everything it does is in the
    // library, where it can be read as a shader rather than as a string.
    static const char* const kCullSource = "#include \"Fluxion/Pass/GPUCull.jsl\"\n";

    FluxionShaderProgramDesc programDesc;
    memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "Fluxion.GPUScene.Cull";
    programDesc.computeSource = kCullSource;

    record->cullProgram = Fluxion_ShaderProgram_Create(record->device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(record->cullProgram))
    {
        // Said once, not once a frame: a shader that would not build
        // will not build again, and repeating it buries whatever said
        // it first. The frame still draws -- on the processor's answer.
        FLUXION_LOG_ERROR(FLUXION_GPU_SCENE_LOG_CATEGORY,
                          "the culling shader could not be built; this frame decides what it can see on the processor instead");
        record->cullPipelineFailed = true;
        return false;
    }

    FluxionRHIBindGroupLayoutDesc layoutDesc = Fluxion_GPUSceneInternal_MakeCullLayoutDesc();
    record->cullLayout = Fluxion_RHI_CreateBindGroupLayout(record->device, &layoutDesc);

    FluxionRHIComputePipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.computeShader = FluxionRendererInternal_ShaderProgram_GetComputeShader(record->cullProgram);
    pipelineDesc.bindGroupLayouts[0] = record->cullLayout;
    pipelineDesc.bindGroupLayoutCount = 1;
    pipelineDesc.debugName = "Fluxion.GPUScene.CullPipeline";

    record->cullPipeline = Fluxion_RHI_CreateComputePipeline(record->device, &pipelineDesc);
    if (!FLUXION_HANDLE_IS_VALID(record->cullPipeline))
    {
        record->cullPipelineFailed = true;
        return false;
    }

    return true;
}

static void Fluxion_GPUSceneInternal_DestroyCullBuffers(FluxionGPUSceneRecord* record)
{
    FluxionRHIBufferHandle* buffers[] = {
        &record->cullSphereStaging, &record->cullSphereStorage,
        &record->cullBatchStaging, &record->cullBatchStorage,
        &record->cullLayerStaging, &record->cullLayerStorage,
        &record->cullBatchFirstStaging, &record->cullBatchFirstStorage,
    };

    for (usize i = 0; i < sizeof(buffers) / sizeof(buffers[0]); ++i)
    {
        if (FLUXION_HANDLE_IS_VALID(*buffers[i])) Fluxion_RHI_DestroyBuffer(*buffers[i]);
        *buffers[i] = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
}

// One pair of buffers, staged and device-side, for one of the lists the
// cull reads.
static bool Fluxion_GPUSceneInternal_MakeCullPair(FluxionGPUSceneRecord* record, usize size, const char* name,
                                                  FluxionRHIBufferHandle* outStaging, FluxionRHIBufferHandle* outStorage)
{
    FluxionRHIBufferDesc stagingDesc;
    memset(&stagingDesc, 0, sizeof(stagingDesc));
    stagingDesc.size = size;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = name;

    FluxionRHIBufferDesc storageDesc;
    memset(&storageDesc, 0, sizeof(storageDesc));
    storageDesc.size = size;
    storageDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
    storageDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    storageDesc.debugName = name;

    *outStaging = Fluxion_RHI_CreateBuffer(record->device, &stagingDesc);
    *outStorage = Fluxion_RHI_CreateBuffer(record->device, &storageDesc);
    return FLUXION_HANDLE_IS_VALID(*outStaging) && FLUXION_HANDLE_IS_VALID(*outStorage);
}

static bool Fluxion_GPUSceneInternal_EnsureCullBuffers(FluxionGPUSceneRecord* record, u32 objectCapacity, u32 batchCapacity)
{
    if (objectCapacity <= record->cullObjectCapacity && batchCapacity <= record->cullBatchCapacity) return true;

    Fluxion_GPUSceneInternal_DestroyCullBuffers(record);
    Fluxion_GPUSceneInternal_ForgetCullBindGroup(record);

    const bool made =
        Fluxion_GPUSceneInternal_MakeCullPair(record, (usize)objectCapacity * sizeof(FluxionVec4), "Fluxion.GPUScene.CullSpheres",
                                              &record->cullSphereStaging, &record->cullSphereStorage) &&
        Fluxion_GPUSceneInternal_MakeCullPair(record, (usize)objectCapacity * sizeof(u32), "Fluxion.GPUScene.CullBatches",
                                              &record->cullBatchStaging, &record->cullBatchStorage) &&
        Fluxion_GPUSceneInternal_MakeCullPair(record, (usize)objectCapacity * sizeof(u32), "Fluxion.GPUScene.CullLayerPass",
                                              &record->cullLayerStaging, &record->cullLayerStorage) &&
        Fluxion_GPUSceneInternal_MakeCullPair(record, (usize)batchCapacity * sizeof(u32), "Fluxion.GPUScene.CullBatchFirst",
                                              &record->cullBatchFirstStaging, &record->cullBatchFirstStorage);

    if (!made)
    {
        Fluxion_GPUSceneInternal_DestroyCullBuffers(record);
        return false;
    }

    if (!FLUXION_HANDLE_IS_VALID(record->cullUniform))
    {
        FluxionRHIBufferDesc uniformDesc;
        memset(&uniformDesc, 0, sizeof(uniformDesc));
        uniformDesc.size = sizeof(FluxionGPUSceneCullUniform);
        uniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
        uniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
        uniformDesc.debugName = "Fluxion.GPUScene.CullUniform";
        record->cullUniform = Fluxion_RHI_CreateBuffer(record->device, &uniformDesc);
        if (!FLUXION_HANDLE_IS_VALID(record->cullUniform)) return false;
    }

    record->cullObjectCapacity = objectCapacity;
    record->cullBatchCapacity = batchCapacity;
    return true;
}

// How many objects one group of the cull shader covers. The shader's own
// group size is the compiler's default; this has to agree with it, and
// the check below is what keeps them agreeing.
#define FLUXION_GPU_SCENE_CULL_GROUP_SIZE 64

// The copies across, the dispatch, and the barriers that make what it
// wrote readable by the draws that follow.
static void Fluxion_GPUSceneInternal_RecordCull(FluxionGPUSceneRecord* record, FluxionRHICommandListHandle commandList)
{
    const FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // What the compute reads, across first. The commands are already on
    // their way (the caller copied them, with the instance counts at
    // zero) and the visible list is written entirely by the shader, so
    // neither is copied here.
    struct { FluxionRHIBufferHandle staging; FluxionRHIBufferHandle storage; usize size; } copies[] = {
        { record->cullSphereStaging, record->cullSphereStorage, (usize)record->entryCount * sizeof(FluxionVec4) },
        { record->cullBatchStaging, record->cullBatchStorage, (usize)record->entryCount * sizeof(u32) },
        { record->cullLayerStaging, record->cullLayerStorage, (usize)record->entryCount * sizeof(u32) },
        { record->cullBatchFirstStaging, record->cullBatchFirstStorage, (usize)record->batchCount * sizeof(u32) },
    };

    for (usize i = 0; i < sizeof(copies) / sizeof(copies[0]); ++i)
    {
        if (copies[i].size == 0) continue;

        FluxionRHIBarrier toCopy = { noTexture, copies[i].storage, FLUXION_RHI_RESOURCE_STATE_COMMON,
                                     FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
        Fluxion_RHI_CommandList_Barrier(commandList, &toCopy, 1);

        Fluxion_RHI_CommandList_CopyBuffer(commandList, copies[i].staging, 0, copies[i].storage, 0, copies[i].size);

        FluxionRHIBarrier toRead = { noTexture, copies[i].storage, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION,
                                     FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
        Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);
    }

    // The two the shader WRITES: the commands it counts into, and the
    // list it fills.
    FluxionRHIBarrier toWrite[2] = {
        { noTexture, record->indirectBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE },
        { noTexture, record->visibleStorage,
          record->objectStorageWritten ? FLUXION_RHI_RESOURCE_STATE_SHADER_READ : FLUXION_RHI_RESOURCE_STATE_COMMON,
          FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE },
    };
    Fluxion_RHI_CommandList_Barrier(commandList, toWrite, 2);

    if (!FLUXION_HANDLE_IS_VALID(record->cullBindGroup))
    {
        FluxionRHIBindGroupEntry entries[7];
        memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = record->cullUniform;
        entries[0].bufferSize = sizeof(FluxionGPUSceneCullUniform);

        const struct { FluxionRHIBufferHandle buffer; u32 stride; } storages[6] = {
            { record->cullSphereStorage, (u32)sizeof(FluxionVec4) },
            { record->cullBatchStorage, (u32)sizeof(u32) },
            { record->cullLayerStorage, (u32)sizeof(u32) },
            { record->cullBatchFirstStorage, (u32)sizeof(u32) },
            { record->visibleStorage, (u32)sizeof(u32) },
            { record->indirectBuffer, (u32)sizeof(FluxionRHIDrawIndexedIndirectCommand) },
        };

        for (u32 i = 0; i < 6; ++i)
        {
            entries[i + 1].binding = i + 1;
            entries[i + 1].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
            entries[i + 1].buffer = storages[i].buffer;
            entries[i + 1].bufferElementStride = storages[i].stride;
        }

        FluxionRHIBindGroupDesc bindGroupDesc;
        bindGroupDesc.layout = record->cullLayout;
        bindGroupDesc.entries = entries;
        bindGroupDesc.entryCount = 7;

        record->cullBindGroup = Fluxion_RHI_CreateBindGroup(record->device, &bindGroupDesc);
        if (FLUXION_HANDLE_IS_VALID(record->cullBindGroup)) record->cullBindGroupFailed = false;
    }

    if (!FLUXION_HANDLE_IS_VALID(record->cullBindGroup))
    {
        if (!record->cullBindGroupFailed)
        {
            FLUXION_LOG_ERROR(FLUXION_GPU_SCENE_LOG_CATEGORY,
                              "the cull pass could not be bound; this frame drew nothing, and the next will try again");
            record->cullBindGroupFailed = true;
        }

        // The two buffers above were told they are about to be written.
        // Nothing is going to write them, so they are put back the way
        // the rest of the frame expects to find them -- a state left
        // half changed is a validation error every frame after this one.
        FluxionRHIBarrier undo[2] = {
            { noTexture, record->indirectBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE, FLUXION_RHI_RESOURCE_STATE_COMMON },
            { noTexture, record->visibleStorage, FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE, FLUXION_RHI_RESOURCE_STATE_SHADER_READ },
        };
        Fluxion_RHI_CommandList_Barrier(commandList, undo, 2);
        return;
    }

    Fluxion_RHI_CommandList_SetPipeline(commandList, record->cullPipeline);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, record->cullBindGroup);

    const u32 groups = (record->entryCount + FLUXION_GPU_SCENE_CULL_GROUP_SIZE - 1) / FLUXION_GPU_SCENE_CULL_GROUP_SIZE;
    Fluxion_RHI_CommandList_Dispatch(commandList, groups, 1, 1);

    // And back to being read: the list by the vertex stage, the commands
    // by the draws themselves. COMMON for the commands because this
    // contract has no state of its own for "a draw reads its arguments
    // from here", and COMMON is what every backend maps to something
    // that covers it.
    FluxionRHIBarrier toRead[2] = {
        { noTexture, record->visibleStorage, FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE, FLUXION_RHI_RESOURCE_STATE_SHADER_READ },
        { noTexture, record->indirectBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE, FLUXION_RHI_RESOURCE_STATE_COMMON },
    };
    Fluxion_RHI_CommandList_Barrier(commandList, toRead, 2);

    // And a copy of what it counted, for the caller to read at the start
    // of a later frame -- see commandReadback.
    if (FLUXION_HANDLE_IS_VALID(record->commandReadback) && record->batchCount > 0)
    {
        FluxionRHIBarrier toSource = { noTexture, record->indirectBuffer, FLUXION_RHI_RESOURCE_STATE_COMMON,
                                       FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE };
        Fluxion_RHI_CommandList_Barrier(commandList, &toSource, 1);

        Fluxion_RHI_CommandList_CopyBuffer(commandList, record->indirectBuffer, 0, record->commandReadback, 0,
                                           (usize)record->batchCount * sizeof(FluxionRHIDrawIndexedIndirectCommand));

        FluxionRHIBarrier backToDraw = { noTexture, record->indirectBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE,
                                         FLUXION_RHI_RESOURCE_STATE_COMMON };
        Fluxion_RHI_CommandList_Barrier(commandList, &backToDraw, 1);

        record->readbackBatchCount = record->batchCount;
        record->readbackPending = true;
    }

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
    record->everyRowStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->everyRowStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->indirectStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->indirectBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->commandReadback = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->batchUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullSphereStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullSphereStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBatchStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBatchStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullLayerStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullLayerStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBatchFirstStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBatchFirstStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullUniform = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

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
    Fluxion_GPUSceneInternal_DestroyCullBuffers(record);
    Fluxion_GPUSceneInternal_ForgetCullBindGroup(record);
    if (FLUXION_HANDLE_IS_VALID(record->cullUniform)) Fluxion_RHI_DestroyBuffer(record->cullUniform);
    if (FLUXION_HANDLE_IS_VALID(record->cullPipeline)) Fluxion_RHI_DestroyPipeline(record->cullPipeline);
    if (FLUXION_HANDLE_IS_VALID(record->cullLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->cullLayout);
    if (FLUXION_HANDLE_IS_VALID(record->cullProgram)) Fluxion_ShaderProgram_Destroy(record->cullProgram);
    if (FLUXION_HANDLE_IS_VALID(record->indirectStaging)) Fluxion_RHI_DestroyBuffer(record->indirectStaging);
    if (FLUXION_HANDLE_IS_VALID(record->indirectBuffer)) Fluxion_RHI_DestroyBuffer(record->indirectBuffer);
    if (FLUXION_HANDLE_IS_VALID(record->commandReadback)) Fluxion_RHI_DestroyBuffer(record->commandReadback);
    if (FLUXION_HANDLE_IS_VALID(record->batchUniformBuffer)) Fluxion_RHI_DestroyBuffer(record->batchUniformBuffer);

    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    if (record->entries != NULL)
    {
        Fluxion_Allocator_Free(allocator, record->entries, (usize)record->entryCapacity * sizeof(FluxionGPUSceneEntry));
        Fluxion_Allocator_Free(allocator, record->order, (usize)record->entryCapacity * sizeof(u32));
        Fluxion_Allocator_Free(allocator, record->batches, (usize)record->entryCapacity * sizeof(FluxionGPUSceneBatch));
        Fluxion_Allocator_Free(allocator, record->batchBindGroups, (usize)record->entryCapacity * sizeof(FluxionRHIBindGroupHandle));
        Fluxion_Allocator_Free(allocator, record->allRowsBindGroups, (usize)record->entryCapacity * sizeof(FluxionRHIBindGroupHandle));
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
    record->everyRowStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->everyRowStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->indirectStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->indirectBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->commandReadback = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->batchUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullSphereStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullSphereStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBatchStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBatchStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullLayerStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullLayerStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBatchFirstStaging = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBatchFirstStorage = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullUniform = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    record->cullBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
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
        if (FLUXION_HANDLE_IS_VALID(record->allRowsBindGroups[i])) Fluxion_RHI_DestroyBindGroup(record->allRowsBindGroups[i]);
        record->allRowsBindGroups[i] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    record->batchBindGroupCount = 0;

    // And LAST frame's counts, read now for the same reason: the frame
    // that wrote them has been submitted, and a caller that waits for a
    // frame before starting the next has waited for it. What comes of a
    // caller that does not is a count one frame staler still -- a report
    // that is behind, rather than a frame that is drawn wrong.
    if (record->readbackPending)
    {
        const FluxionRHIDrawIndexedIndirectCommand* commands =
            (const FluxionRHIDrawIndexedIndirectCommand*)Fluxion_RHI_MapBuffer(record->commandReadback);
        if (commands != NULL)
        {
            u32 seen = 0;
            for (u32 i = 0; i < record->readbackBatchCount; ++i) seen += commands[i].instanceCount;
            record->deviceVisibleCount = seen;
            Fluxion_RHI_UnmapBuffer(record->commandReadback);
        }
        record->readbackPending = false;
    }

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

    // WHAT IS ACTUALLY DRAWN.
    //
    // The rows above hold everything; the visible list holds the ones
    // this frame can see. EACH BATCH GETS ITS OWN SLICE, as long as the
    // batch itself and starting where its rows start -- so a batch's
    // survivors can be gathered without knowing anything about the
    // batches beside it, which is what lets the device do it too.
    const FluxionFrustumPlanes frustum = Fluxion_Mat4_FrustumPlanes(record->cull.viewProjection);
    const bool onDevice = record->cull.enabled && record->cull.mode == FLUXION_GPU_SCENE_CULL_GPU &&
                          Fluxion_GPUSceneInternal_EnsureCullPipeline(record) &&
                          Fluxion_GPUSceneInternal_EnsureCullBuffers(record, record->objectCapacity, record->batchCapacity);

    record->countsOnDevice = onDevice;

    for (u32 batchIndex = 0; batchIndex < record->batchCount; ++batchIndex)
    {
        record->batches[batchIndex].firstVisible = record->batches[batchIndex].firstObject;
        record->batches[batchIndex].visibleCount = 0;
    }

    record->visibleCount = 0;

    if (!onDevice)
    {
        u32* visible = (u32*)Fluxion_RHI_MapBuffer(record->visibleStaging);
        if (visible == NULL) return;

        for (u32 batchIndex = 0; batchIndex < record->batchCount; ++batchIndex)
        {
            FluxionGPUSceneBatch* batch = &record->batches[batchIndex];

            for (u32 i = 0; i < batch->objectCount; ++i)
            {
                const u32 row = batch->firstObject + i;
                if (!Fluxion_GPUSceneInternal_IsVisible(record, &record->entries[record->order[row]], &frustum)) continue;

                visible[batch->firstVisible + batch->visibleCount] = row;
                ++batch->visibleCount;
                ++record->visibleCount;
            }
        }
        Fluxion_RHI_UnmapBuffer(record->visibleStaging);
    }
    else
    {
        // The same three answers, written down for the device to read.
        // The layer test is answered HERE either way: the shading
        // language has no bitwise operators yet, so what crosses is a
        // yes or a no rather than a mask -- see Fluxion/Pass/GPUCull.jsl.
        FluxionVec4* spheres = (FluxionVec4*)Fluxion_RHI_MapBuffer(record->cullSphereStaging);
        u32* batchOf = (u32*)Fluxion_RHI_MapBuffer(record->cullBatchStaging);
        u32* layerPass = (u32*)Fluxion_RHI_MapBuffer(record->cullLayerStaging);
        u32* batchFirst = (u32*)Fluxion_RHI_MapBuffer(record->cullBatchFirstStaging);

        if (spheres == NULL || batchOf == NULL || layerPass == NULL || batchFirst == NULL) return;

        const u32 viewLayers = record->cull.layerMask != 0 ? record->cull.layerMask : 0xFFFFFFFFu;

        for (u32 batchIndex = 0; batchIndex < record->batchCount; ++batchIndex)
        {
            const FluxionGPUSceneBatch* batch = &record->batches[batchIndex];
            batchFirst[batchIndex] = batch->firstVisible;

            for (u32 i = 0; i < batch->objectCount; ++i)
            {
                const u32 row = batch->firstObject + i;
                const FluxionGPUSceneEntry* entry = &record->entries[record->order[row]];

                spheres[row].x = entry->boundsCentre.x;
                spheres[row].y = entry->boundsCentre.y;
                spheres[row].z = entry->boundsCentre.z;
                spheres[row].w = entry->boundsRadius;

                batchOf[row] = batchIndex;

                const u32 objectLayers = entry->layerMask != 0 ? entry->layerMask : 0xFFFFFFFFu;
                layerPass[row] = (viewLayers & objectLayers) != 0 ? 1u : 0u;
            }
        }

        Fluxion_RHI_UnmapBuffer(record->cullSphereStaging);
        Fluxion_RHI_UnmapBuffer(record->cullBatchStaging);
        Fluxion_RHI_UnmapBuffer(record->cullLayerStaging);
        Fluxion_RHI_UnmapBuffer(record->cullBatchFirstStaging);

        FluxionGPUSceneCullUniform uniform;
        memset(&uniform, 0, sizeof(uniform));
        for (u32 i = 0; i < 6; ++i) uniform.planes[i] = frustum.planes[i];
        uniform.eye.x = record->cull.cameraPosition.x;
        uniform.eye.y = record->cull.cameraPosition.y;
        uniform.eye.z = record->cull.cameraPosition.z;
        uniform.eye.w = record->cull.cullDistance;
        uniform.counts.x = (f32)record->entryCount;

        void* mappedUniform = Fluxion_RHI_MapBuffer(record->cullUniform);
        if (mappedUniform == NULL) return;
        memcpy(mappedUniform, &uniform, sizeof(uniform));
        Fluxion_RHI_UnmapBuffer(record->cullUniform);

        // How many survived is not known on this side any more -- the
        // number lives in a buffer the device wrote, and comes back one
        // frame later. Until the first one has, it is zero: a count
        // nobody has yet, rather than a wrong one.
        record->visibleCount = record->deviceVisibleCount;
    }

    // One command per batch, and one uniform slice saying where that
    // batch's slice of the visible list begins.
    FluxionRHIDrawIndexedIndirectCommand* commands =
        (FluxionRHIDrawIndexedIndirectCommand*)Fluxion_RHI_MapBuffer(record->indirectStaging);
    u8* uniforms = (u8*)Fluxion_RHI_MapBuffer(record->batchUniformBuffer);
    if (commands == NULL || uniforms == NULL)
    {
        if (commands != NULL) Fluxion_RHI_UnmapBuffer(record->indirectStaging);
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

        // On the device's path this starts at zero and the compute
        // counts up to what it finds; on the processor's it is already
        // the answer.
        commands[i].instanceCount = onDevice ? 0u : batch->visibleCount;
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

    Fluxion_RHI_UnmapBuffer(record->indirectStaging);
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

            // The same group again, reading the rows as they lie. One
            // binding different, and the uniform slice is shared: a
            // batch's slice begins where its rows begin, so "the nth of
            // what I can see" and "my nth row" are the same arithmetic.
            entries[2].buffer = record->everyRowStorage;
            record->allRowsBindGroups[i] = Fluxion_RHI_CreateBindGroup(record->device, &bindGroupDesc);
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

    // Once per buffer rather than once per frame: what it holds does not
    // depend on the frame.
    if (!record->everyRowUploaded)
    {
        FluxionRHIBarrier everyRowToCopy = { noTexture, record->everyRowStorage, FLUXION_RHI_RESOURCE_STATE_COMMON,
                                             FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
        Fluxion_RHI_CommandList_Barrier(commandList, &everyRowToCopy, 1);

        Fluxion_RHI_CommandList_CopyBuffer(commandList, record->everyRowStaging, 0, record->everyRowStorage, 0,
                                           (usize)record->objectCapacity * sizeof(u32));

        FluxionRHIBarrier everyRowToRead = { noTexture, record->everyRowStorage, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION,
                                             FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
        Fluxion_RHI_CommandList_Barrier(commandList, &everyRowToRead, 1);

        record->everyRowUploaded = true;
    }

    if (!onDevice && record->visibleCount > 0)
    {
        FluxionRHIBarrier visibleToCopy = { noTexture, record->visibleStorage,
                                            record->objectStorageWritten ? FLUXION_RHI_RESOURCE_STATE_SHADER_READ : FLUXION_RHI_RESOURCE_STATE_COMMON,
                                            FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
        Fluxion_RHI_CommandList_Barrier(commandList, &visibleToCopy, 1);

        Fluxion_RHI_CommandList_CopyBuffer(commandList, record->visibleStaging, 0, record->visibleStorage, 0,
                                           (usize)record->objectCapacity * sizeof(u32) < (usize)record->entryCount * sizeof(u32)
                                               ? (usize)record->objectCapacity * sizeof(u32)
                                               : (usize)record->entryCount * sizeof(u32));

        FluxionRHIBarrier visibleToRead = { noTexture, record->visibleStorage,
                                            FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
        Fluxion_RHI_CommandList_Barrier(commandList, &visibleToRead, 1);
    }

    FluxionRHIBarrier toRead = { noTexture, record->objectStorage,
                                 FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);

    // The commands, across. Both paths write them the same way -- what
    // differs is only whether the instance counts in them are already
    // the answer.
    FluxionRHIBarrier commandsToCopy = { noTexture, record->indirectBuffer,
                                         record->indirectWritten ? FLUXION_RHI_RESOURCE_STATE_COMMON : FLUXION_RHI_RESOURCE_STATE_COMMON,
                                         FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
    Fluxion_RHI_CommandList_Barrier(commandList, &commandsToCopy, 1);

    Fluxion_RHI_CommandList_CopyBuffer(commandList, record->indirectStaging, 0, record->indirectBuffer, 0,
                                       (usize)record->batchCount * sizeof(FluxionRHIDrawIndexedIndirectCommand));

    if (onDevice)
    {
        Fluxion_GPUSceneInternal_RecordCull(record, commandList);
    }
    else
    {
        // COMMON rather than a state of its own: this contract has no
        // "indirect argument" state, and COMMON is the one every backend
        // maps to something that covers a draw reading its own arguments
        // -- on the strictest of them, memory reads at any stage.
        FluxionRHIBarrier commandsToDraw = { noTexture, record->indirectBuffer,
                                             FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_COMMON };
        Fluxion_RHI_CommandList_Barrier(commandList, &commandsToDraw, 1);
    }

    record->objectStorageWritten = true;
    record->indirectWritten = true;
}

bool Fluxion_GPUScene_CountsOnDevice(FluxionGPUSceneHandle scene)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL) return false;
    return record->countsOnDevice;
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

FluxionRHIBindGroupHandle Fluxion_GPUScene_GetBatchAllRowsBindGroup(FluxionGPUSceneHandle scene, u32 batchIndex)
{
    const FluxionGPUSceneRecord* record = Fluxion_GPUSceneInternal_Resolve(scene);
    if (record == NULL || batchIndex >= record->batchBindGroupCount)
    {
        FluxionRHIBindGroupHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return invalid;
    }
    return record->allRowsBindGroups[batchIndex];
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
