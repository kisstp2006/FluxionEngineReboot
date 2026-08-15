#include <Fluxion/Assets/Assets.h>

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Atomic.h>
#include <Fluxion/Foundation/Containers/HashMap.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Log.h>

#include <string.h>

#define FLUXION_ASSETS_LOG_CATEGORY "Assets"

typedef struct FluxionAssetSlot
{
    FluxionUUID id;
    FluxionAssetTypeId type;

    // Bumped every time the slot is let go, so a handle kept past a
    // release names nothing rather than naming whatever moved in.
    u32 generation;
    bool inUse;

    i32 referenceCount;

    // The only field two threads touch: the worker writes it at the end
    // of a load, the owning thread reads it whenever it asks.
    FluxionAtomicI32 state;

    void* object;

    // The load reads through this rather than looking the asset up again.
    // A worker that consulted the registries would be reading tables the
    // owning thread is free to change while it runs -- and a type
    // unregistered mid-load would take its own load function with it.
    char* cookedPath;
    usize cookedPathCapacity;
    FluxionAssetLoadFn load;
    FluxionAssetFinalizeFn finalize;
    FluxionAssetUnloadFn unload;
    void* userData;

    FluxionJobHandle loadJob;
} FluxionAssetSlot;

static FluxionAllocator* s_allocator = NULL;
static FluxionAssetSlot s_slots[FLUXION_ASSETS_MAX_LOADED];
static FluxionHashMap s_byId; // FluxionUUID -> u32 slot index
static u32 s_loadedCount = 0;
static bool s_initialized = false;

void Fluxion_Assets_Init(FluxionAllocator* allocator)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_Assets_Init called twice without a Shutdown in between");

    s_allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    memset(s_slots, 0, sizeof(s_slots));
    Fluxion_HashMap_Init(&s_byId, s_allocator, sizeof(FluxionUUID), sizeof(u32), Fluxion_HashBytes64, Fluxion_BytesEqual);
    s_loadedCount = 0;
    s_initialized = true;
}

static void Fluxion_Assets_FinishSlot(FluxionAssetSlot* slot);
static void Fluxion_Assets_ReleaseSlot(FluxionAssetSlot* slot);

void Fluxion_Assets_Shutdown(void)
{
    if (!s_initialized) return;

    for (u32 i = 0; i < FLUXION_ASSETS_MAX_LOADED; ++i)
    {
        if (s_slots[i].inUse) Fluxion_Assets_ReleaseSlot(&s_slots[i]);
    }

    Fluxion_HashMap_Destroy(&s_byId);
    memset(s_slots, 0, sizeof(s_slots));
    s_loadedCount = 0;
    s_allocator = NULL;
    s_initialized = false;
}

bool Fluxion_Assets_IsInitialized(void)
{
    return s_initialized;
}

static FluxionAssetSlot* Fluxion_Assets_Resolve(FluxionAssetHandle handle)
{
    if (!s_initialized) return NULL;
    if (!FLUXION_HANDLE_IS_VALID(handle) || handle.index >= FLUXION_ASSETS_MAX_LOADED) return NULL;

    FluxionAssetSlot* slot = &s_slots[handle.index];
    if (!slot->inUse || slot->generation != handle.generation) return NULL;
    return slot;
}

// ---------------------------------------------------------------------
// The load itself.
// ---------------------------------------------------------------------

static void Fluxion_Assets_RunLoad(void* data)
{
    FluxionAssetSlot* slot = *(FluxionAssetSlot**)data;

    usize size = 0;
    u8* bytes = Fluxion_Vfs_ReadAll(slot->cookedPath, &size);
    if (!bytes)
    {
        Fluxion_AtomicI32_Store(&slot->state, (i32)FLUXION_ASSET_STATE_FAILED);
        return;
    }

    void* object = NULL;
    const bool ok = slot->load(bytes, size, &object, slot->userData);
    Fluxion_Vfs_FreeBuffer(bytes, size);

    if (!ok)
    {
        Fluxion_AtomicI32_Store(&slot->state, (i32)FLUXION_ASSET_STATE_FAILED);
        return;
    }

    slot->object = object;

    // A type with no device-side step is finished here. Saying so rather
    // than parking it in cpu-ready is what keeps the uploading state
    // meaningful: only things that really do upload pass through it.
    Fluxion_AtomicI32_Store(&slot->state,
                            slot->finalize ? (i32)FLUXION_ASSET_STATE_CPU_READY : (i32)FLUXION_ASSET_STATE_READY);
}

// Carries one slot as far as this thread can take it.
static void Fluxion_Assets_FinishSlot(FluxionAssetSlot* slot)
{
    if ((FluxionAssetState)Fluxion_AtomicI32_Load(&slot->state) != FLUXION_ASSET_STATE_CPU_READY) return;

    Fluxion_AtomicI32_Store(&slot->state, (i32)FLUXION_ASSET_STATE_UPLOADING);

    const bool ok = slot->finalize(slot->object, slot->userData);
    Fluxion_AtomicI32_Store(&slot->state, ok ? (i32)FLUXION_ASSET_STATE_READY : (i32)FLUXION_ASSET_STATE_FAILED);
}

static void Fluxion_Assets_ReleaseSlot(FluxionAssetSlot* slot)
{
    // Whatever is still being decoded belongs to this asset, and there
    // would be nowhere for it to land once the slot is gone.
    if (FLUXION_HANDLE_IS_VALID(slot->loadJob)) Fluxion_JobSystem_Wait(slot->loadJob);

    if (slot->object && slot->unload) slot->unload(slot->object, slot->userData);

    if (slot->cookedPath) Fluxion_Allocator_Free(s_allocator, slot->cookedPath, slot->cookedPathCapacity);

    Fluxion_HashMap_Remove(&s_byId, &slot->id);

    const u32 generation = slot->generation;
    memset(slot, 0, sizeof(*slot));

    // Not the memset's zero: a handle held past this must stop naming the
    // slot, and it would go on naming it if the generation went back to
    // where it started.
    slot->generation = generation + 1;

    --s_loadedCount;
}

// ---------------------------------------------------------------------
// Asking for one.
// ---------------------------------------------------------------------

static FluxionAssetHandle Fluxion_Assets_InvalidHandle(void)
{
    const FluxionAssetHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return handle;
}

static FluxionJobHandle Fluxion_Assets_InvalidJobHandle(void)
{
    const FluxionJobHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return handle;
}

static u32 Fluxion_Assets_FindFreeSlot(void)
{
    for (u32 i = 0; i < FLUXION_ASSETS_MAX_LOADED; ++i)
    {
        if (!s_slots[i].inUse) return i;
    }
    return FLUXION_HANDLE_INVALID_INDEX;
}

FluxionAssetHandle Fluxion_Assets_Acquire(FluxionUUID id)
{
    if (!s_initialized) return Fluxion_Assets_InvalidHandle();

    {
        const u32* existing = (const u32*)Fluxion_HashMap_Find(&s_byId, &id);
        if (existing)
        {
            FluxionAssetSlot* slot = &s_slots[*existing];
            ++slot->referenceCount;

            const FluxionAssetHandle handle = { *existing, slot->generation };
            return handle;
        }
    }

    const FluxionAssetRecord* record = Fluxion_AssetDatabase_Find(id);
    if (!record)
    {
        char text[37];
        Fluxion_UUID_ToString(id, text);
        FLUXION_LOG_ERROR(FLUXION_ASSETS_LOG_CATEGORY, "no asset %s in the database", text);
        return Fluxion_Assets_InvalidHandle();
    }

    const FluxionAssetTypeDesc* type = Fluxion_AssetTypes_Find(record->type);
    if (!type)
    {
        char text[37];
        Fluxion_UUID_ToString(id, text);
        FLUXION_LOG_ERROR(FLUXION_ASSETS_LOG_CATEGORY, "asset %s is of a type nothing has registered", text);
        return Fluxion_Assets_InvalidHandle();
    }

    const char* cookedPath = Fluxion_AssetDatabase_GetCookedPath(record);
    if (cookedPath[0] == '\0')
    {
        char text[37];
        Fluxion_UUID_ToString(id, text);
        FLUXION_LOG_ERROR(FLUXION_ASSETS_LOG_CATEGORY, "asset %s has no cooked form to load", text);
        return Fluxion_Assets_InvalidHandle();
    }

    const u32 index = Fluxion_Assets_FindFreeSlot();
    if (index == FLUXION_HANDLE_INVALID_INDEX)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSETS_LOG_CATEGORY, "no free slot: %u assets are already held", (u32)FLUXION_ASSETS_MAX_LOADED);
        return Fluxion_Assets_InvalidHandle();
    }

    FluxionAssetSlot* slot = &s_slots[index];
    const u32 generation = slot->generation;
    memset(slot, 0, sizeof(*slot));
    slot->generation = generation;

    const usize pathLength = strlen(cookedPath);
    slot->cookedPathCapacity = pathLength + 1;
    slot->cookedPath = (char*)Fluxion_Allocator_Alloc(s_allocator, slot->cookedPathCapacity, FLUXION_DEFAULT_ALIGNMENT);
    if (!slot->cookedPath)
    {
        slot->cookedPathCapacity = 0;
        return Fluxion_Assets_InvalidHandle();
    }
    memcpy(slot->cookedPath, cookedPath, slot->cookedPathCapacity);

    slot->id = id;
    slot->type = record->type;
    slot->inUse = true;
    slot->referenceCount = 1;
    slot->load = type->load;
    slot->finalize = type->finalize;
    slot->unload = type->unload;
    slot->userData = type->userData;

    // Said outright, and not left to the memset above: a zeroed handle
    // has index zero, and index zero is a real job. Every path out of
    // here that waits on this would otherwise wait on whatever job
    // happened to be sitting in the first slot.
    slot->loadJob = Fluxion_Assets_InvalidJobHandle();

    Fluxion_AtomicI32_Store(&slot->state, (i32)FLUXION_ASSET_STATE_LOADING);

    if (!Fluxion_HashMap_Set(&s_byId, &id, &index))
    {
        Fluxion_Allocator_Free(s_allocator, slot->cookedPath, slot->cookedPathCapacity);
        memset(slot, 0, sizeof(*slot));
        slot->generation = generation;
        return Fluxion_Assets_InvalidHandle();
    }

    ++s_loadedCount;

    if (Fluxion_JobSystem_IsInitialized())
    {
        FluxionJobDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.function = Fluxion_Assets_RunLoad;
        desc.dataSize = sizeof(FluxionAssetSlot*);
        memcpy(desc.data, &slot, sizeof(FluxionAssetSlot*));

        slot->loadJob = Fluxion_JobSystem_Submit(&desc);

        // An invalid handle means one of two things: the job already ran
        // to completion on this thread, or there was no room to submit
        // it. The state tells them apart -- a job that ran has left the
        // slot somewhere other than loading.
        if (!FLUXION_HANDLE_IS_VALID(slot->loadJob) &&
            (FluxionAssetState)Fluxion_AtomicI32_Load(&slot->state) == FLUXION_ASSET_STATE_LOADING)
        {
            FLUXION_LOG_ERROR(FLUXION_ASSETS_LOG_CATEGORY, "could not submit the load; marking it failed rather than leaving it pending forever");
            Fluxion_AtomicI32_Store(&slot->state, (i32)FLUXION_ASSET_STATE_FAILED);
        }
    }
    else
    {
        // Nothing to hand the work to, so it is done here. A caller that
        // never started the job system still gets a loaded asset rather
        // than one stuck partway.
        Fluxion_Assets_RunLoad(&slot);
    }

    const FluxionAssetHandle handle = { index, slot->generation };
    return handle;
}

FluxionAssetHandle Fluxion_Assets_AcquireRef(FluxionAssetRef ref)
{
    if (!Fluxion_AssetRef_IsSet(ref)) return Fluxion_Assets_InvalidHandle();
    return Fluxion_Assets_Acquire(ref.asset);
}

void Fluxion_Assets_Release(FluxionAssetHandle handle)
{
    FluxionAssetSlot* slot = Fluxion_Assets_Resolve(handle);
    if (!slot) return;

    FLUXION_ASSERT_MSG(slot->referenceCount > 0, "an asset was released more times than it was acquired");
    if (slot->referenceCount > 0) --slot->referenceCount;
    if (slot->referenceCount > 0) return;

    Fluxion_Assets_ReleaseSlot(slot);
}

FluxionAssetState Fluxion_Assets_GetState(FluxionAssetHandle handle)
{
    const FluxionAssetSlot* slot = Fluxion_Assets_Resolve(handle);
    if (!slot) return FLUXION_ASSET_STATE_UNLOADED;
    return (FluxionAssetState)Fluxion_AtomicI32_Load(&slot->state);
}

void* Fluxion_Assets_GetObject(FluxionAssetHandle handle)
{
    FluxionAssetSlot* slot = Fluxion_Assets_Resolve(handle);
    if (!slot) return NULL;
    if ((FluxionAssetState)Fluxion_AtomicI32_Load(&slot->state) != FLUXION_ASSET_STATE_READY) return NULL;
    return slot->object;
}

FluxionAssetTypeId Fluxion_Assets_GetType(FluxionAssetHandle handle)
{
    const FluxionAssetSlot* slot = Fluxion_Assets_Resolve(handle);
    return slot ? slot->type : FLUXION_ASSET_TYPE_ID_INVALID;
}

FluxionUUID Fluxion_Assets_GetId(FluxionAssetHandle handle)
{
    const FluxionAssetSlot* slot = Fluxion_Assets_Resolve(handle);
    if (!slot)
    {
        FluxionUUID nil;
        memset(&nil, 0, sizeof(nil));
        return nil;
    }
    return slot->id;
}

u32 Fluxion_Assets_GetReferenceCount(FluxionAssetHandle handle)
{
    const FluxionAssetSlot* slot = Fluxion_Assets_Resolve(handle);
    return slot ? (u32)slot->referenceCount : 0;
}

u32 Fluxion_Assets_GetLoadedCount(void)
{
    return s_initialized ? s_loadedCount : 0;
}

void Fluxion_Assets_Update(void)
{
    if (!s_initialized) return;

    for (u32 i = 0; i < FLUXION_ASSETS_MAX_LOADED; ++i)
    {
        if (s_slots[i].inUse) Fluxion_Assets_FinishSlot(&s_slots[i]);
    }
}

FluxionAssetState Fluxion_Assets_Wait(FluxionAssetHandle handle)
{
    FluxionAssetSlot* slot = Fluxion_Assets_Resolve(handle);
    if (!slot) return FLUXION_ASSET_STATE_UNLOADED;

    if (FLUXION_HANDLE_IS_VALID(slot->loadJob))
    {
        Fluxion_JobSystem_Wait(slot->loadJob);
        slot->loadJob = Fluxion_Assets_InvalidJobHandle();
    }

    Fluxion_Assets_FinishSlot(slot);
    return (FluxionAssetState)Fluxion_AtomicI32_Load(&slot->state);
}
