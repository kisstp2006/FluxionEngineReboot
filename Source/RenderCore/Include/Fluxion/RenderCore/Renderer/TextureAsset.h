#pragma once

#include <Fluxion/Assets/AssetTypeId.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>

#ifdef __cplusplus
extern "C" {
#endif

// The engine's own form of a texture.
//
// The same arrangement as the mesh, and for the same reasons: this is the
// half that ships, there is no image decoder here and there is not meant
// to be, and it is registered from this module because this is where a
// texture becomes something a device holds.
//
// THE COLOUR SPACE IS THE FORMAT, not a flag beside it. A texture that
// holds colour is stored in an sRGB format and one that holds data is
// stored in a linear one, and there is nowhere for the two to disagree.
// A separate flag would be a second place to say the same thing, and the
// failure when the two disagreed would be a picture that looks washed out
// or lighting that is subtly wrong -- neither of which is an error
// message.

#define FLUXION_TEXTURE_ASSET_FORMAT_VERSION 1
#define FLUXION_TEXTURE_ASSET_MAGIC          0x464C5854u // "FLXT"

// The name a build setting writes down to say what happens to textures.
#define FLUXION_TEXTURE_ASSET_TYPE_NAME "Texture"

// Enough for a 32768-wide texture, which is larger than any hardware in
// sight accepts.
#define FLUXION_TEXTURE_ASSET_MAX_MIPS 16

typedef struct FluxionTextureAsset
{
    // Valid once the asset is ready. Before the upload step there is
    // neither, because nothing has been given to a device yet.
    FluxionRHITextureHandle texture;
    FluxionRHITextureViewHandle view;

    u32 width;
    u32 height;
    u32 mipCount;
    u32 arrayLayers;
    FluxionRHIFormat format;

    // Every level, tightly packed, largest first -- until the upload
    // takes them. NULL afterwards, for the same reason a mesh's vertices
    // are: once a device holds them there is no reason to keep a second
    // copy, and a field that is sometimes there is better said than
    // hidden.
    const void* pixels;
    usize pixelBytes;
} FluxionTextureAsset;

// What a cooked texture is made of on the way in. The same struct
// describes what to write and what was read.
typedef struct FluxionTextureAssetData
{
    u32 width;
    u32 height;
    u32 mipCount;
    u32 arrayLayers;
    FluxionRHIFormat format;

    // Every level of every layer, tightly packed: layer 0's mip 0 first,
    // then its mip 1, and so on. Tightly, because that is what a file
    // wants; the alignment a device wants is imposed when it is uploaded
    // and nowhere else.
    const void* pixels;
    usize pixelBytes;
} FluxionTextureAssetData;

// How many bytes one level of this format at this size occupies, tightly
// packed. Zero for a format this does not handle.
usize Fluxion_TextureAsset_GetLevelByteSize(FluxionRHIFormat format, u32 width, u32 height);

// All the levels of all the layers, tightly packed -- what a cooked file
// holds and what `pixels` above points at.
usize Fluxion_TextureAsset_GetTotalByteSize(FluxionRHIFormat format, u32 width, u32 height, u32 mipCount, u32 arrayLayers);

// A level's size, which halves and stops at one rather than reaching
// zero.
u32 Fluxion_TextureAsset_GetLevelExtent(u32 base, u32 level);

// Writes the cooked form into `stream`, which must be a writer.
bool Fluxion_TextureAsset_Write(FluxionStream* stream, const FluxionTextureAssetData* data);

// Reads the cooked form, with no device involved. What comes back has an
// invalid `texture` until something uploads it. Give it back with
// Fluxion_TextureAsset_Destroy.
bool Fluxion_TextureAsset_Read(const u8* bytes, usize size, FluxionTextureAsset** outAsset);
void Fluxion_TextureAsset_Destroy(FluxionTextureAsset* asset);

FluxionAssetTypeId Fluxion_TextureAsset_TypeId(void);

bool Fluxion_TextureAsset_RegisterType(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue);
void Fluxion_TextureAsset_UnregisterType(void);

#ifdef __cplusplus
}
#endif
