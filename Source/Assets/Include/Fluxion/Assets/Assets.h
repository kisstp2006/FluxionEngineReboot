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
void Fluxion_Assets_Update(void);

// Blocks until this asset is ready or has failed.
//
// It pumps the device-side step itself rather than waiting for the next
// Update. Without that, waiting on the owning thread for something that
// needs the owning thread would be waiting for itself.
FluxionAssetState Fluxion_Assets_Wait(FluxionAssetHandle handle);

#ifdef __cplusplus
}
#endif
