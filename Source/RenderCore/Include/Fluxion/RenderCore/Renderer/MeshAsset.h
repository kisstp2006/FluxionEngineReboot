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
