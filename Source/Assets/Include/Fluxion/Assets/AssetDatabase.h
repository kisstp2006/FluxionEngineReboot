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
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// What the project knows about each asset: which one it is, what kind it
// is, where its source is, where its cooked form is, and what it needs in
// order to be usable.
//
// In a project being worked on, this is built by looking at what is
// there. In a shipped game it is the package's own index, read once --
// and the source paths in it are empty, because nothing in a running game
// has any use for them.

// What this build writes and the newest it will read. A file from a newer
// build is refused rather than half understood.
#define FLUXION_ASSET_DATABASE_VERSION 2

// A path in this database is a path in the file system above, so it is
// bounded by the same thing rather than by a second number that could
// disagree with it. A name is only ever read by a person.
#define FLUXION_ASSET_MAX_PATH_LENGTH (FLUXION_VFS_MAX_PATH - 1)
#define FLUXION_ASSET_MAX_NAME_LENGTH 127

// How long a cook target's name may be, and how many one asset may have.
#define FLUXION_ASSET_COOK_TARGET_NAME_LENGTH 31
#define FLUXION_ASSET_MAX_COOKED_FORMS 8

// The largest per-asset import settings blob the database will hold.
// Generous: these are a handful of numbers and flags for every type there
// is reason to expect.
#define FLUXION_ASSET_MAX_IMPORT_SETTINGS_BYTES 512

// One cooked form of an asset, and which builds it is for.
//
// NAMED, NOT AN OPERATING SYSTEM. What makes two cooked forms differ is
// what the hardware will accept -- a texture compressed one way for one
// family of GPUs and another way for another -- and that does not line up
// with which system is running. A name lets a project draw the line where
// its own content actually differs.
//
// An empty name means "suits every build", which is what almost
// everything is: a mesh is cooked once and that is the end of it. A build
// asking for a target this asset has no entry for falls back to that one,
// so nothing has to list every target it does not care about.
typedef struct FluxionAssetCookedForm
{
    char target[FLUXION_ASSET_COOK_TARGET_NAME_LENGTH + 1];
    const char* path;
} FluxionAssetCookedForm;

// Text and dependency lists both live in shared pools rather than in the
// record. This is the one table that grows with the size of a project, so
// a per-record path buffer would be paid for by every asset whether it
// had a path or not -- unlike the small fixed-size descriptors elsewhere
// in the engine, where the generous cap really is cheaper than the
// bookkeeping.
typedef struct FluxionAssetRecord
{
    FluxionUUID id;
    FluxionAssetTypeId type;
    u32 version;

    // The database's own bookkeeping. Read them through the accessors
    // below rather than directly.
    //
    // Zero is a real offset here and means the empty string, because the
    // text pool begins with a terminator that nothing else uses. "No
    // path" and "an empty path" are therefore the same value, which is
    // exactly right -- and it means no separate way of saying "none" can
    // be forgotten at one of the places that sets these.
    u32 nameOffset;
    u32 sourcePathOffset;
    u32 dependencyOffset;
    u32 dependencyCount;

    // Into the database's cooked-form pool. An asset with no cooked form
    // at all has a count of zero, which is what an asset that only ever
    // ships as its source looks like.
    u32 cookedOffset;
    u32 cookedCount;

    // Into the database's import-settings pool, and a hash of the bytes.
    //
    // The hash is what makes re-importing decidable: source unchanged and
    // settings unchanged means the cooked form is still right, and
    // comparing the bytes every time would mean reading them every time.
    u32 importSettingsOffset;
    u32 importSettingsSize;
    u64 importSettingsHash;
} FluxionAssetRecord;

typedef struct FluxionAssetDesc
{
    // Nil asks for a fresh one, which is what importing something new
    // wants; a set value is used as given, which is what reading a
    // database back wants.
    FluxionUUID id;

    FluxionAssetTypeId type;

    // All optional. A name is for reading logs and build reports -- it
    // resolves nothing, and two assets may share one.
    const char* name;
    const char* sourcePath;

    // The common case, said briefly: one cooked form that suits every
    // build. Exactly equivalent to a single entry in `cookedForms` with
    // an empty target.
    //
    // Giving both this and `cookedForms` is refused rather than merged --
    // two ways of saying where the bytes are is two things that can
    // disagree.
    const char* cookedPath;

    const FluxionAssetCookedForm* cookedForms;
    u32 cookedFormCount;

    u32 version;

    const FluxionUUID* dependencies;
    u32 dependencyCount;

    // Whatever this asset's TYPE makes of them. The database stores the
    // bytes and hashes them; it does not read them, and could not -- a
    // plugin's own asset type brings its own settings through the same
    // field.
    const void* importSettings;
    u32 importSettingsSize;
} FluxionAssetDesc;

void Fluxion_AssetDatabase_Init(FluxionAllocator* allocator);
void Fluxion_AssetDatabase_Shutdown(void);
bool Fluxion_AssetDatabase_IsInitialized(void);

bool Fluxion_AssetDatabase_Add(const FluxionAssetDesc* desc, FluxionUUID* outId);

// Removing leaves the entry's text and dependency list behind in the
// pools until the whole database is cleared. Bounded by how much one
// session removes, and worth it for a table this size.
bool Fluxion_AssetDatabase_Remove(FluxionUUID id);
void Fluxion_AssetDatabase_Clear(void);

// Every returned record pointer, and every string returned from one, is
// good only until the next Add, Remove or Clear. The records live in one
// growable run, so adding may move all of them at once. Hold the id, not
// the pointer.
const FluxionAssetRecord* Fluxion_AssetDatabase_Find(FluxionUUID id);

u32 Fluxion_AssetDatabase_GetCount(void);

// Index order is not stable across removals -- an entry is removed by
// moving the last one into its place. Enumerate for a pass over
// everything, look up by id for anything that must stay pointing at the
// same asset.
const FluxionAssetRecord* Fluxion_AssetDatabase_GetAt(u32 index);

const char* Fluxion_AssetDatabase_GetName(const FluxionAssetRecord* record);
const char* Fluxion_AssetDatabase_GetSourcePath(const FluxionAssetRecord* record);
// The cooked form that suits every build, or the first one there is.
// Empty when the asset has none.
const char* Fluxion_AssetDatabase_GetCookedPath(const FluxionAssetRecord* record);

// The cooked form for one build, falling back to the one that suits every
// build. Empty when there is neither.
//
// A NULL or empty `target` asks for the general one directly.
const char* Fluxion_AssetDatabase_GetCookedPathForTarget(const FluxionAssetRecord* record, const char* target);

u32 Fluxion_AssetDatabase_GetCookedFormCount(const FluxionAssetRecord* record);
const char* Fluxion_AssetDatabase_GetCookedFormTargetAt(const FluxionAssetRecord* record, u32 index);
const char* Fluxion_AssetDatabase_GetCookedFormPathAt(const FluxionAssetRecord* record, u32 index);

// The bytes this asset's type was given at import time. NULL with a zero
// size when there are none.
//
// Handed back as bytes because that is what they are here: the database
// stores and hashes them, and only the asset type knows what they mean.
const void* Fluxion_AssetDatabase_GetImportSettings(const FluxionAssetRecord* record, u32* outSize);
const FluxionUUID* Fluxion_AssetDatabase_GetDependencies(const FluxionAssetRecord* record, u32* outCount);

// Reads or writes the whole database, depending on the stream's mode.
// Reading clears whatever was there first -- this is a load, not a merge,
// for the same reason loading a scene is.
bool Fluxion_AssetDatabase_Serialize(FluxionStream* stream);

// What a build writes instead of the whole thing: fewer entries, and
// different paths in them.
//
// This exists so that the index a game ships and the database an editor
// keeps go through ONE writer. The alternative -- a second writer that
// knows the same format -- is two things that have to agree forever, and
// the day they stop agreeing is the day a shipped game cannot read its
// own index.
typedef struct FluxionAssetDatabaseWriteFilter
{
    // Which records to write at all. NULL writes every one.
    bool (*shouldWrite)(const FluxionAssetRecord* record, void* userData);

    // Where the bytes actually ended up. NULL writes what is stored.
    const char* (*cookedPathFor)(const FluxionAssetRecord* record, void* userData);

    // A shipped index sets this false, and the source path in it comes
    // out empty. That is not tidiness: it is the reason a running game
    // has nothing to reach for even if some code one day tried.
    bool includeSourcePaths;

    void* userData;
} FluxionAssetDatabaseWriteFilter;

// Write only. Reading uses Serialize above -- a filter on the way in
// would mean a file whose contents depend on who opened it.
bool Fluxion_AssetDatabase_SerializeFiltered(FluxionStream* stream, const FluxionAssetDatabaseWriteFilter* filter);

#ifdef __cplusplus
}
#endif
