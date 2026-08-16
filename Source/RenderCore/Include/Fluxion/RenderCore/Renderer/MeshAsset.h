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
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>

#ifdef __cplusplus
extern "C" {
#endif

// The engine's own form of a mesh: what is left of a model once nothing
// has to be worked out from it again.
//
// This is the half that ships. There is no reader for any interchange
// format here and there is not meant to be -- that half belongs to an
// importer, an importer belongs to a plugin, and a built game loads no
// such plugin. Which is how a shipped program ends up without the large
// library such a reader would have brought with it.
//
// Registered from this module rather than from the asset system, because
// this is where a mesh becomes something a device holds. The asset system
// knows nothing about meshes; it is handed a load function and an upload
// function, exactly as a plugin would hand it some.

#define FLUXION_MESH_ASSET_FORMAT_VERSION 1
#define FLUXION_MESH_ASSET_MAGIC          0x464C584Du // "FLXM"

// The name a build setting writes down to say what happens to meshes.
#define FLUXION_MESH_ASSET_TYPE_NAME "Mesh"

// What a loaded mesh is. This is also what the asset system hands back
// for an asset of this type -- the object is a FluxionMeshAsset, so
// nothing has to guess what to cast it to.
typedef struct FluxionMeshAsset
{
    // Valid once the asset is ready. Before the upload step there is no
    // buffer, because nothing has been given to a device yet.
    FluxionMeshBufferHandle buffer;

    FluxionAABB bounds;
    FluxionRHIVertexLayout vertexLayout;
    bool use16BitIndices;
    u32 indexCount;

    // The mesh as it was read, until the upload takes it. NULL afterwards
    // -- once a device holds the mesh there is no reason to keep a second
    // copy of it, and a field that is sometimes there and sometimes not
    // is better said than hidden.
    const void* vertexData;
    usize vertexDataSize;
    const void* indexData;
    usize indexDataSize;
} FluxionMeshAsset;

// What a cooked mesh is made of on the way in. The same struct describes
// what to write and what was read, so the two cannot describe different
// things.
typedef struct FluxionMeshAssetData
{
    const void* vertexData;
    usize vertexDataSize;
    const void* indexData;
    usize indexDataSize;
    bool use16BitIndices;
    FluxionRHIVertexLayout vertexLayout;
    FluxionAABB bounds;
} FluxionMeshAssetData;

// Writes the cooked form into `stream`, which must be a writer. This is
// what an importer produces, and what the loader below reads back -- one
// file describing both directions, so a change to one is a change to the
// other.
bool Fluxion_MeshAsset_Write(FluxionStream* stream, const FluxionMeshAssetData* data);

// Reads the cooked form, with no device involved. The asset type's own
// load half is this function, so a tool that opens a mesh and a game that
// loads one go through the same decoder rather than two that could come
// to disagree.
//
// What comes back has an invalid `buffer` until something uploads it.
// Give it back with Fluxion_MeshAsset_Destroy.
bool Fluxion_MeshAsset_Read(const u8* bytes, usize size, FluxionMeshAsset** outAsset);
void Fluxion_MeshAsset_Destroy(FluxionMeshAsset* asset);

FluxionAssetTypeId Fluxion_MeshAsset_TypeId(void);

// Registers the mesh type with the asset system. The device and queue are
// what the upload step needs; they are captured here rather than looked
// up later, so a mesh being uploaded cannot find them changed underneath
// it.
bool Fluxion_MeshAsset_RegisterType(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue);
void Fluxion_MeshAsset_UnregisterType(void);

#ifdef __cplusplus
}
#endif
