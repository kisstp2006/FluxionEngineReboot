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
#include <Fluxion/Platform/Time.h>

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

    // --- Reading it again, when the file behind it changes ---------------
    //
    // A second, complete set of the load's working state rather than
    // reusing the fields above. The asset stays USABLE for the whole of a
    // reload -- its state stays ready and its object stays valid -- and
    // that is only true because the new one is built somewhere else and
    // swapped in at the end.

    // What the file looked like when it was last read. Zero means it was
    // never known, which is the same as "this file cannot change" and is
    // why a package costs nothing here.
    u64 revision;

    // The worker writes this one, the owning thread reads it -- the same
    // arrangement as `state` above, and for the same reason.
    FluxionAtomicI32 reloadState;

    void* reloadObject;
    FluxionJobHandle reloadJob;

    u32 reloadCount;
} FluxionAssetSlot;

// Where a reload has got to. Deliberately NOT FluxionAssetState: the
// asset's own state stays ready throughout, because the asset IS ready
// throughout, and using one field for both would make a reload look like
// an unload to everything that asks.
typedef enum FluxionAssetReloadState
{
    FLUXION_ASSET_RELOAD_IDLE = 0,
    FLUXION_ASSET_RELOAD_READING,

    // Read and decoded; waiting for the step that has to happen on the
    // thread that owns the device.
    FLUXION_ASSET_RELOAD_CPU_READY,
    FLUXION_ASSET_RELOAD_FAILED,
} FluxionAssetReloadState;

static FluxionAllocator* s_allocator = NULL;
static FluxionAssetSlot s_slots[FLUXION_ASSETS_MAX_LOADED];
static FluxionHashMap s_byId; // FluxionUUID -> u32 slot index
static u32 s_loadedCount = 0;
static bool s_initialized = false;

// How often the files behind loaded assets are looked at.
//
// Two hundred milliseconds is chosen to be under what a person notices
// between saving a file and seeing it, while being far more than the cost
// of asking -- one question per held asset, five times a second.
#define FLUXION_ASSETS_DEFAULT_WATCH_MILLISECONDS 200

static u32 s_watchMilliseconds = FLUXION_ASSETS_DEFAULT_WATCH_MILLISECONDS;
static u64 s_lastWatchTicks = 0;

void Fluxion_Assets_Init(FluxionAllocator* allocator)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_Assets_Init called twice without a Shutdown in between");

    s_allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    memset(s_slots, 0, sizeof(s_slots));
    Fluxion_HashMap_Init(&s_byId, s_allocator, sizeof(FluxionUUID), sizeof(u32), Fluxion_HashBytes64, Fluxion_BytesEqual);
    s_loadedCount = 0;

    // Now, not zero. Zero would make the first Update look like a whole
    // clock's worth of time had passed, so every asset would be asked
    // about on the very frame it was loaded -- which is the one frame it
    // certainly has not changed on.
    s_lastWatchTicks = Fluxion_Platform_GetHighResolutionTicks();

    s_initialized = true;
}

static void Fluxion_Assets_FinishSlot(FluxionAssetSlot* slot);
static void Fluxion_Assets_ReleaseSlot(FluxionAssetSlot* slot);

void Fluxion_Assets_Shutdown(void)
{
    if (!s_initialized) return;

    // Back to what a fresh run has. A build that starts the asset system
    // twice would otherwise carry the previous run's setting into a
    // second one that never asked for it.
    s_watchMilliseconds = FLUXION_ASSETS_DEFAULT_WATCH_MILLISECONDS;

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

// ---------------------------------------------------------------------
// Reading one again, because its file changed.
//
// The same two steps as a first load, in the same order, run by the same
// two functions the type provided -- which is what makes this work for
// every asset type there is and for every one added later, with nothing
// written per type.
//
// What is different is where the result goes. A first load has nothing to
// protect and writes straight into the slot; this one builds the new
// object beside the old and swaps at the very end, so that the asset is
// usable at every instant in between.
// ---------------------------------------------------------------------

static void Fluxion_Assets_RunReload(void* data)
{
    FluxionAssetSlot* slot = *(FluxionAssetSlot**)data;

    usize size = 0;
    u8* bytes = Fluxion_Vfs_ReadAll(slot->cookedPath, &size);
    if (!bytes)
    {
        Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_FAILED);
        return;
    }

    void* object = NULL;
    const bool ok = slot->load(bytes, size, &object, slot->userData);
    Fluxion_Vfs_FreeBuffer(bytes, size);

    if (!ok)
    {
        Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_FAILED);
        return;
    }

    slot->reloadObject = object;
    Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_CPU_READY);
}

// Puts the new object in place of the old one.
//
// The order is the whole of it: finish the new one, put it in, and only
// then let go of the old one. Reversed, there would be a moment when the
// asset had nothing -- and a frame that landed on that moment would draw
// with a destroyed texture rather than with the previous one.
static void Fluxion_Assets_SwapInReload(FluxionAssetSlot* slot)
{
    void* replaced = slot->object;
    slot->object = slot->reloadObject;
    slot->reloadObject = NULL;

    if (replaced && slot->unload) slot->unload(replaced, slot->userData);

    ++slot->reloadCount;

    // Said out loud, once per reload. Something changing on screen with
    // nothing to say why is the harder thing to work with of the two --
    // and when it does NOT change, this line is how somebody finds out
    // whether the file was even noticed.
    char text[37];
    Fluxion_UUID_ToString(slot->id, text);
    FLUXION_LOG_INFO(FLUXION_ASSETS_LOG_CATEGORY, "asset %s changed on disc and was read again", text);
}

// Everything a reload leaves behind when it comes to nothing.
static void Fluxion_Assets_AbandonReload(FluxionAssetSlot* slot, const char* why)
{
    if (slot->reloadObject && slot->unload) slot->unload(slot->reloadObject, slot->userData);
    slot->reloadObject = NULL;

    char text[37];
    Fluxion_UUID_ToString(slot->id, text);

    // The old one is still there and still correct, so this is a message
    // and not a failure. An asset marked failed here would take a working
    // picture off the screen because a half-written file was read once.
    FLUXION_LOG_ERROR(FLUXION_ASSETS_LOG_CATEGORY,
                      "asset %s was not read again (%s); what was already loaded is still in use", text, why);
}

// Carries a reload as far as this thread can take it, which is all the
// way -- the device-side step is this thread's to run.
static void Fluxion_Assets_FinishReload(FluxionAssetSlot* slot)
{
    const FluxionAssetReloadState state = (FluxionAssetReloadState)Fluxion_AtomicI32_Load(&slot->reloadState);

    if (state == FLUXION_ASSET_RELOAD_FAILED)
    {
        Fluxion_Assets_AbandonReload(slot, "the file could not be read or made sense of");
        Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_IDLE);
        return;
    }

    if (state != FLUXION_ASSET_RELOAD_CPU_READY) return;

    if (slot->finalize && !slot->finalize(slot->reloadObject, slot->userData))
    {
        Fluxion_Assets_AbandonReload(slot, "it could not be given to the device");
        Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_IDLE);
        return;
    }

    Fluxion_Assets_SwapInReload(slot);
    Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_IDLE);
}

static void Fluxion_Assets_StartReload(FluxionAssetSlot* slot)
{
    Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_READING);
    slot->reloadObject = NULL;

    if (Fluxion_JobSystem_IsInitialized())
    {
        FluxionJobDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.function = Fluxion_Assets_RunReload;
        desc.dataSize = sizeof(FluxionAssetSlot*);
        memcpy(desc.data, &slot, sizeof(FluxionAssetSlot*));

        slot->reloadJob = Fluxion_JobSystem_Submit(&desc);

        // Same two possibilities as the first load: it already ran here,
        // or there was no room for it. The state tells them apart.
        if (!FLUXION_HANDLE_IS_VALID(slot->reloadJob) &&
            (FluxionAssetReloadState)Fluxion_AtomicI32_Load(&slot->reloadState) == FLUXION_ASSET_RELOAD_READING)
        {
            Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_FAILED);
        }
    }
    else
    {
        Fluxion_Assets_RunReload(&slot);
    }
}

// Looks at the file behind every held asset, and starts reading again the
// ones that changed.
static void Fluxion_Assets_LookForChanges(void)
{
    for (u32 i = 0; i < FLUXION_ASSETS_MAX_LOADED; ++i)
    {
        FluxionAssetSlot* slot = &s_slots[i];
        if (!slot->inUse) continue;

        // Never known, which is what a file that cannot change looks
        // like. Asking again would cost a question per asset per poll for
        // an answer that is always the same.
        if (slot->revision == 0) continue;

        // Still arriving for the first time, or already being read again.
        // Either way there is a load in flight writing into this slot,
        // and a second one would race it.
        const FluxionAssetState state = (FluxionAssetState)Fluxion_AtomicI32_Load(&slot->state);
        if (state == FLUXION_ASSET_STATE_LOADING || state == FLUXION_ASSET_STATE_CPU_READY ||
            state == FLUXION_ASSET_STATE_UPLOADING)
            continue;
        if ((FluxionAssetReloadState)Fluxion_AtomicI32_Load(&slot->reloadState) != FLUXION_ASSET_RELOAD_IDLE) continue;

        const u64 revision = Fluxion_Vfs_GetRevision(slot->cookedPath);

        // Zero is "no answer just now" -- the file is missing, or
        // something is in the middle of writing it. Not a change, and
        // treating it as one would read a half-written file.
        if (revision == 0 || revision == slot->revision) continue;

        // Written down BEFORE the attempt, and that is what stops a file
        // that cannot be loaded from being tried five times a second for
        // the rest of the run. It gets one attempt per change, which is
        // what a person editing it expects.
        slot->revision = revision;

        Fluxion_Assets_StartReload(slot);
    }
}

static void Fluxion_Assets_ReleaseSlot(FluxionAssetSlot* slot)
{
    // Whatever is still being decoded belongs to this asset, and there
    // would be nowhere for it to land once the slot is gone. True of a
    // reload as much as of a first load -- and a reload is the easier one
    // to forget, because the asset it is rebuilding looks perfectly
    // finished from outside.
    if (FLUXION_HANDLE_IS_VALID(slot->loadJob)) Fluxion_JobSystem_Wait(slot->loadJob);
    if (FLUXION_HANDLE_IS_VALID(slot->reloadJob)) Fluxion_JobSystem_Wait(slot->reloadJob);

    // The half-built one first: it belongs to nobody, so nothing can
    // notice it going, and leaving it would leak exactly as much as the
    // asset itself is worth.
    if (slot->reloadObject && slot->unload) slot->unload(slot->reloadObject, slot->userData);

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
    slot->reloadJob = Fluxion_Assets_InvalidJobHandle();

    // What the file looks like right now, taken BEFORE anything reads it.
    //
    // Before, so that a change made while the read is happening is not
    // written down as already seen. The worst that order costs is one
    // reload of a file that was already current; the other order loses
    // the change entirely.
    //
    // Taken on this thread rather than in the job, because this is the
    // thread that owns the mount table -- and zero coming back is the
    // ordinary answer for a file that cannot change, which is what makes
    // a packaged asset free to hold.
    slot->revision = Fluxion_Vfs_GetRevision(slot->cookedPath);

    Fluxion_AtomicI32_Store(&slot->state, (i32)FLUXION_ASSET_STATE_LOADING);
    Fluxion_AtomicI32_Store(&slot->reloadState, (i32)FLUXION_ASSET_RELOAD_IDLE);

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
        if (!s_slots[i].inUse) continue;

        Fluxion_Assets_FinishSlot(&s_slots[i]);

        // A reload that is waiting for this thread gets it here, in the
        // same pass. Finishing one is what puts the new object in place,
        // so a reload that never reached this line would have read the
        // file for nothing.
        Fluxion_Assets_FinishReload(&s_slots[i]);
    }

    if (s_watchMilliseconds == 0) return;

    const u64 ticks = Fluxion_Platform_GetHighResolutionTicks();
    const u64 frequency = Fluxion_Platform_GetHighResolutionFrequency();
    if (frequency == 0) return;

    // Multiplied out rather than divided down, so that an interval
    // shorter than one tick is not rounded to nothing.
    const u64 elapsedMilliseconds = (ticks - s_lastWatchTicks) * 1000ull / frequency;
    if (elapsedMilliseconds < (u64)s_watchMilliseconds) return;

    s_lastWatchTicks = ticks;
    Fluxion_Assets_LookForChanges();
}

void Fluxion_Assets_SetWatchInterval(u32 milliseconds)
{
    s_watchMilliseconds = milliseconds;

    // The clock starts again from here. Otherwise switching watching back
    // on after a long pause would look like the interval had elapsed many
    // times over, and every held asset would be asked about at once.
    s_lastWatchTicks = Fluxion_Platform_GetHighResolutionTicks();
}

u32 Fluxion_Assets_GetWatchInterval(void)
{
    return s_watchMilliseconds;
}

u32 Fluxion_Assets_GetReloadCount(FluxionAssetHandle handle)
{
    const FluxionAssetSlot* slot = Fluxion_Assets_Resolve(handle);
    return slot ? slot->reloadCount : 0;
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

    // And a reload, if one happens to be in flight. A caller waiting to
    // use this asset means the newest of it, not whichever version was
    // there when the wait began -- and leaving one half-finished would
    // hold a decoded object nothing goes back to.
    if (FLUXION_HANDLE_IS_VALID(slot->reloadJob))
    {
        Fluxion_JobSystem_Wait(slot->reloadJob);
        slot->reloadJob = Fluxion_Assets_InvalidJobHandle();
    }

    Fluxion_Assets_FinishReload(slot);

    return (FluxionAssetState)Fluxion_AtomicI32_Load(&slot->state);
}
