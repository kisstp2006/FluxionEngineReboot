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
// A rewritten cooked file is read again behind the SAME handle, by the
// type's own load and finalize -- nothing asks for this and nothing is
// written per type. Holders must not keep the GetObject pointer across
// frames; something that must cache can watch GetReloadCount below.
//
// What is watched follows from the source: a directory is looked at, a
// package cannot change and is not -- so a built game looks at nothing.
// The old object goes only AFTER the new one is finished, and a failed
// reload keeps the old one, with a message, rather than a hole.

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
