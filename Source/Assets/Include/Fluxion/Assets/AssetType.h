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
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// What a kind of asset is, and -- the part that decides what a shipped
// game carries -- which of its two halves is which.
//
// AN ASSET TYPE HAS TWO HALVES, AND THEY DO NOT LIVE IN THE SAME PLACE:
//
//   the import half   a source file -> cooked bytes
//   the load half     cooked bytes  -> something the game can use
//
// The import half is the expensive one. Reading a widely-used interchange
// format means carrying a library that can read it, and a running game
// has no use for that: it never sees a source file. So the import half
// belongs to a plugin, and a built game loads no such plugin. That is
// what actually keeps the reader out of the shipped program -- not a
// setting, which could only ever leave out the DATA while the code went
// along anyway.
//
// The setting still exists, and it is honest, because it is about data:
// see FluxionAssetShipPolicy below.
//
// The other half of that promise is that at runtime NOTHING ASKS FOR A
// SOURCE FILE. A reference is an id; the id resolves to cooked bytes; the
// source path exists only in the editor's database. A game shipped
// without sources is not missing something it might reach for -- there is
// no code path that reaches.

#define FLUXION_ASSET_MAX_TYPES              64
#define FLUXION_ASSET_MAX_TYPE_NAME_LENGTH   63
#define FLUXION_ASSET_MAX_EXTENSION_LENGTH   15
#define FLUXION_ASSET_MAX_SOURCE_EXTENSIONS  8

typedef enum FluxionAssetShipPolicy
{
    // The cooked form goes into the package; the source does not. What
    // almost everything wants, and why it is the zero value.
    FLUXION_ASSET_SHIP_COOKED = 0,

    // The file goes in exactly as it is. For a type that has no import
    // half because there is nothing to cook.
    FLUXION_ASSET_SHIP_SOURCE,

    // Nothing goes in. For a type that only means something while a
    // project is being edited.
    FLUXION_ASSET_SHIP_NEVER,
} FluxionAssetShipPolicy;

// Turns cooked bytes into whatever this type's runtime object is. Runs on
// a worker thread, so it must touch nothing but its arguments.
//
// The object is this type's own business -- the asset system only ever
// holds it as a void* and hands it back. False means the bytes were not
// usable, and *outObject is then left alone.
typedef bool (*FluxionAssetLoadFn)(const u8* bytes, usize size, void** outObject, void* userData);

// The part that cannot happen on a worker: handing the loaded thing to a
// device. Runs on whichever thread pumps the asset system, which is the
// thread that owns the device.
//
// NULL for a type with no such step, and a type that leaves it NULL never
// passes through the uploading state at all -- so that state means
// something rather than being a stop every asset makes for show.
typedef bool (*FluxionAssetFinalizeFn)(void* object, void* userData);

typedef void (*FluxionAssetUnloadFn)(void* object, void* userData);

// The import half. Reads a source file's bytes and writes the cooked form
// into `cookedOut`, a stream in write mode.
//
// NULL on a type that cannot be imported -- which is every type, as far
// as a shipped game is concerned, because the plugin that would have
// filled this in is not loaded there.
typedef bool (*FluxionAssetImportFn)(const u8* sourceBytes, usize sourceSize, FluxionStream* cookedOut, void* userData);

typedef struct FluxionAssetTypeDesc
{
    // Both the display name and, hashed, the identity. A build setting
    // names a type by this string, so changing it is not a rename -- it
    // is a different type, and every setting that mentioned the old one
    // stops applying.
    char name[FLUXION_ASSET_MAX_TYPE_NAME_LENGTH + 1];

    // Without a leading dot, e.g. "fluxmesh".
    char cookedExtension[FLUXION_ASSET_MAX_EXTENSION_LENGTH + 1];

    // Which source files this type claims, e.g. "obj", "gltf". Empty on a
    // type with no import half.
    char sourceExtensions[FLUXION_ASSET_MAX_SOURCE_EXTENSIONS][FLUXION_ASSET_MAX_EXTENSION_LENGTH + 1];
    u32 sourceExtensionCount;

    FluxionAssetShipPolicy defaultShipPolicy;

    FluxionAssetLoadFn load;         // required
    FluxionAssetFinalizeFn finalize; // optional
    FluxionAssetUnloadFn unload;     // required
    FluxionAssetImportFn import;     // optional -- the editor-side half

    void* userData;
} FluxionAssetTypeDesc;

void Fluxion_AssetTypes_Init(FluxionAllocator* allocator);
void Fluxion_AssetTypes_Shutdown(void);
bool Fluxion_AssetTypes_IsInitialized(void);

// The descriptor is COPIED, not kept by pointer -- it is plain data and
// function pointers, so a copy costs nothing and removes one way for a
// registration to go stale.
//
// Unregistering is still required of a plugin, and for the other reason:
// the function pointers in the copy point into the plugin's own library,
// and unloading that library leaves them pointing at nothing. A plugin
// that registers a type in its load must unregister it in its unload.
//
// False when the name is empty or too long, when `load` or `unload` is
// missing (a type nothing can load is not a type), when the name is
// already registered, or when there is no room.
bool Fluxion_AssetTypes_Register(const FluxionAssetTypeDesc* desc);
bool Fluxion_AssetTypes_Unregister(FluxionAssetTypeId id);

const FluxionAssetTypeDesc* Fluxion_AssetTypes_Find(FluxionAssetTypeId id);
const FluxionAssetTypeDesc* Fluxion_AssetTypes_FindByName(const char* name);

// Which type claims a source file with this extension (no leading dot,
// matched without regard to case). FLUXION_ASSET_TYPE_ID_INVALID when
// none does -- in a built game, that is every extension.
FluxionAssetTypeId Fluxion_AssetTypes_FindBySourceExtension(const char* extension);

u32 Fluxion_AssetTypes_GetCount(void);
const FluxionAssetTypeDesc* Fluxion_AssetTypes_GetAt(u32 index);
FluxionAssetTypeId Fluxion_AssetTypes_GetIdAt(u32 index);

#ifdef __cplusplus
}
#endif
