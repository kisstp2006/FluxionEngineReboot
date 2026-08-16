#pragma once

#include <Fluxion/Assets/AssetRef.h>
#include <Fluxion/Assets/AssetTypeId.h>
#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// Turning an id into something the game can use, and keeping track of how
// many people still want it.

// How many assets may be held at once. Fixed, so a slot's address never
// moves and a load running on a worker can be handed one directly.
#define FLUXION_ASSETS_MAX_LOADED 4096

FLUXION_DEFINE_HANDLE(FluxionAssetHandle);

typedef enum FluxionAssetState
{
    FLUXION_ASSET_STATE_UNLOADED = 0,
    FLUXION_ASSET_STATE_LOADING,

    // Read and decoded, waiting for the step that has to happen on the
    // thread that owns the device. A type with no such step is never in
    // this state -- it goes straight to ready.
    FLUXION_ASSET_STATE_CPU_READY,
    FLUXION_ASSET_STATE_UPLOADING,

    FLUXION_ASSET_STATE_READY,
    FLUXION_ASSET_STATE_FAILED,
} FluxionAssetState;

void Fluxion_Assets_Init(FluxionAllocator* allocator);

// Releases everything still held, whatever its reference count says.
// Shutting down is not a moment to honour a count that was already wrong.
void Fluxion_Assets_Shutdown(void);
bool Fluxion_Assets_IsInitialized(void);

// Asking for an asset. The same id asked for twice is one asset with two
// holders, not two copies.
//
// An invalid handle means the load could not be STARTED: no such asset in
// the database, no registered type for it (the plugin that would have
// registered it is not loaded), or no free slot. A load that starts and
// then goes wrong reports itself through the failed state instead, so the
// two are distinguishable.
//
// Acquire, Release and Update are not safe to call at the same time as
// each other. They are the owning thread's business; the reading and
// decoding they set off is what happens elsewhere.
FluxionAssetHandle Fluxion_Assets_Acquire(FluxionUUID id);
FluxionAssetHandle Fluxion_Assets_AcquireRef(FluxionAssetRef ref);

// Waits for a load still in flight before letting go -- the bytes being
// decoded belong to this asset, and there is nowhere for them to land if
// it stops existing first.
void Fluxion_Assets_Release(FluxionAssetHandle handle);

FluxionAssetState Fluxion_Assets_GetState(FluxionAssetHandle handle);

// NULL until the state is ready. Not stable across a Release that drops
// the last reference, which is what makes the count worth keeping.
void* Fluxion_Assets_GetObject(FluxionAssetHandle handle);

FluxionAssetTypeId Fluxion_Assets_GetType(FluxionAssetHandle handle);
FluxionUUID Fluxion_Assets_GetId(FluxionAssetHandle handle);
u32 Fluxion_Assets_GetReferenceCount(FluxionAssetHandle handle);
u32 Fluxion_Assets_GetLoadedCount(void);

// Carries out the steps that can only happen on this thread. Call it once
// per frame from the thread that owns the device.
//
// This is also where a file that changed on disc gets noticed -- see
// below.
void Fluxion_Assets_Update(void);

// --- Assets that change while the game is running ------------------------
//
// A cooked file that is rewritten while something is holding it gets read
// again, and what the holder has changes underneath it. NOTHING HAS TO ASK
// FOR THIS AND NOTHING HAS TO BE WRITTEN PER TYPE: the handle stays the
// same, so every reference already pointing at the asset goes on pointing
// at it, and the new object is built by the type's own load and finalize
// -- the same two functions that built the first one.
//
// What a holder must not do is keep the pointer from Fluxion_Assets_GetObject
// across frames. Asking again each time is one call and always right;
// something that really must cache can watch Fluxion_Assets_GetReloadCount
// below.
//
// Which files are watched is not a setting. It follows from where they
// come from: a directory can change and is looked at, a package cannot and
// is not. A built game mounts packages, so it looks at nothing at all.
//
// The old object is unloaded only AFTER the new one is finished, so there
// is no moment when the asset is neither -- and a reload that fails leaves
// the old one exactly where it was, with a message saying so, rather than
// a hole.

// How often the files behind loaded assets are looked at, in
// milliseconds. Zero switches it off entirely.
//
// It is a poll and not a subscription. Every platform reports changes
// differently, several report them for directories rather than files, and
// the ones that report nothing would need this anyway -- so there is one
// mechanism instead of one plus a fallback that only ever runs on the
// machines nobody tests.
void Fluxion_Assets_SetWatchInterval(u32 milliseconds);
u32 Fluxion_Assets_GetWatchInterval(void);

// How many times this asset has been read again since it was first
// loaded. For a holder that cannot ask for the object every time it needs
// it: when this number changes, whatever was worked out from the old one
// is out of date.
u32 Fluxion_Assets_GetReloadCount(FluxionAssetHandle handle);

// Blocks until this asset is ready or has failed.
//
// It pumps the device-side step itself rather than waiting for the next
// Update. Without that, waiting on the owning thread for something that
// needs the owning thread would be waiting for itself.
FluxionAssetState Fluxion_Assets_Wait(FluxionAssetHandle handle);

#ifdef __cplusplus
}
#endif
