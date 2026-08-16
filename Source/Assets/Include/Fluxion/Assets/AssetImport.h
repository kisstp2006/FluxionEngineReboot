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

#include <Fluxion/Assets/AssetTypeId.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// Turning sources into cooked bytes, for any number of assets at once,
// without the calling thread waiting for any of it.
//
// THE SPLIT IS THE SAME AS THE LOADER'S, AND FOR THE SAME REASON:
//
//   worker thread   read the source, cook it, write the cooked bytes
//   owning thread   put the results into the database
//
// The second half is not fussiness. The asset database is not safe to
// use from several threads and does not need to be: it is one table that
// one thread owns. An importer that wrote into it from a worker would
// force locks onto a structure nothing else contends for. So a worker
// produces a result and stops, and the owning thread takes it on a pump.
//
// Nothing here blocks the caller. Begin returns as soon as the work is
// handed over; Update applies whatever has finished and returns; only End
// waits, and only because it is the point at which the caller has said it
// wants the answer.

typedef enum FluxionAssetImportOutcome
{
    // Not looked at yet, or being looked at right now.
    FLUXION_ASSET_IMPORT_PENDING = 0,

    FLUXION_ASSET_IMPORT_COOKED,

    // The source and the settings are both what they were when the
    // cooked form was made, so there was nothing to do. Which is the
    // ordinary case in a project someone is working in, and the reason
    // the settings are hashed at all.
    FLUXION_ASSET_IMPORT_UP_TO_DATE,

    FLUXION_ASSET_IMPORT_CANCELLED,

    // Errors from here down. Each is reported per asset and none of them
    // stops the rest: an import of nine hundred things must not be lost
    // because the nine hundredth is a broken file.
    FLUXION_ASSET_IMPORT_NO_IMPORTER,
    FLUXION_ASSET_IMPORT_SOURCE_MISSING,
    FLUXION_ASSET_IMPORT_COOK_FAILED,
    FLUXION_ASSET_IMPORT_WRITE_FAILED,
} FluxionAssetImportOutcome;

typedef struct FluxionAssetImportItem
{
    // Nil asks for a new asset; a set value re-imports one that is
    // already in the database.
    FluxionUUID asset;

    FluxionAssetTypeId type;

    const char* name;
    const char* sourcePath;

    // Where the cooked bytes go, and which build they are for. An empty
    // target means the one form that suits every build.
    const char* cookedPath;
    const char* cookTarget;

    // Understood by the asset type and by nothing else here.
    const void* importSettings;
    u32 importSettingsSize;
} FluxionAssetImportItem;

typedef struct FluxionAssetImportDesc
{
    // The items are copied, so the caller's array and strings need not
    // outlive Begin.
    const FluxionAssetImportItem* items;
    u32 itemCount;

    // Cook everything, even what has not changed. What a caller reaches
    // for when it suspects the cooked bytes rather than the sources.
    bool force;
} FluxionAssetImportDesc;

typedef struct FluxionAssetImportProgress
{
    u32 total;
    u32 completed;
    u32 failed;

    // Zero to one, WEIGHTED BY HOW LARGE EACH SOURCE IS.
    //
    // By count, a four-thousand-pixel texture and a line of configuration
    // are worth the same, and the bar jumps: it sits still through the
    // one that takes a second and leaps through twenty that take none.
    f32 fraction;

    // The item a worker most recently started, or -1.
    //
    // An INDEX, not a name. Several workers are going at once and each
    // would be writing into a shared string; handing back a number the
    // caller looks up in its own unchanging list means nothing races.
    // With several workers there is more than one current item, and this
    // is the latest of them rather than all of them.
    i32 currentItem;
} FluxionAssetImportProgress;

typedef struct FluxionAssetImport FluxionAssetImport;

// Copies the work and hands it over. Returns NULL if the asset system is
// not running or the description is empty.
//
// Returns as soon as the work is handed over. Where there is no job
// system, everything is done here instead -- a caller that never started
// one still gets its assets imported rather than an import that never
// runs.
FluxionAssetImport* Fluxion_AssetImport_Begin(const FluxionAssetImportDesc* desc);

// Never blocks. Safe to ask every frame.
bool Fluxion_AssetImport_IsFinished(const FluxionAssetImport* import);
FluxionAssetImportProgress Fluxion_AssetImport_GetProgress(const FluxionAssetImport* import);

// Stops what has not started yet. What is already being cooked is left to
// finish, because a half-written cooked file is worse than one more.
void Fluxion_AssetImport_Cancel(FluxionAssetImport* import);

// Puts whatever has finished into the database, and returns how many it
// put in this time. Call it from the thread that owns the database --
// which is the whole reason it exists.
u32 Fluxion_AssetImport_Update(FluxionAssetImport* import);

u32 Fluxion_AssetImport_GetItemCount(const FluxionAssetImport* import);
FluxionAssetImportOutcome Fluxion_AssetImport_GetOutcomeAt(const FluxionAssetImport* import, u32 index);
const char* Fluxion_AssetImport_GetNameAt(const FluxionAssetImport* import, u32 index);
FluxionUUID Fluxion_AssetImport_GetAssetAt(const FluxionAssetImport* import, u32 index);

// Waits for the rest, applies it, and releases. The handle must not be
// used afterwards.
void Fluxion_AssetImport_End(FluxionAssetImport* import);

#ifdef __cplusplus
}
#endif
