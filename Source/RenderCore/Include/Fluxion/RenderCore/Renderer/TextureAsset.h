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

// --- Staging layout ------------------------------------------------------
//
// A cooked file packs its levels tightly, because that is what a file
// wants. A device wants every row a fixed distance apart and every level
// beginning on a fixed boundary, and those are not the same layout. The
// re-laying-out happens in one place, and this is it -- which is also why
// a texture whose rows already satisfy both (a 256-byte-wide one, say) is
// exactly the texture that would hide a mistake here.
//
// A "row" throughout is a row of BLOCKS. For an uncompressed format that
// is a row of texels; for a compressed one it is not, and counting texels
// would read four times past the end of what a level holds.
typedef struct FluxionTextureLevelPlacement
{
    usize sourceOffset;
    usize stagingOffset;
    usize sourceRowBytes;
    usize stagingRowBytes;
    u32 rows;
} FluxionTextureLevelPlacement;

// One entry per level of per layer, layer-major, in the order
// CopyBufferToTexture wants them. Returns the size of the staging buffer
// they need -- zero if the format cannot be sized, the shape is not real,
// or `capacity` is too small to say the whole answer.
usize Fluxion_TextureAsset_PlanUpload(FluxionRHIFormat format, u32 width, u32 height, u32 mipCount, u32 arrayLayers,
                                      FluxionTextureLevelPlacement* placements, u32 capacity, u32* outCount);

// Writes the cooked form into `stream`, which must be a writer.
bool Fluxion_TextureAsset_Write(FluxionStream* stream, const FluxionTextureAssetData* data);

// Reads the cooked form, with no device involved. What comes back has an
// invalid `texture` until something uploads it. Give it back with
// Fluxion_TextureAsset_Destroy.
bool Fluxion_TextureAsset_Read(const u8* bytes, usize size, FluxionTextureAsset** outAsset);
void Fluxion_TextureAsset_Destroy(FluxionTextureAsset* asset);

// --- What a texture is for, and everything that follows from it ----------
//
// The one setting that drives the rest.
//
// Not twenty independent switches: those can contradict each other, and
// the worst of the contradictions -- a normal map marked as colour --
// produces no error at all, only lighting that is quietly wrong. The
// usage settles the colour space, the format and the channel layout
// together, and nothing can set them apart from each other.
typedef enum FluxionTextureUsage
{
    // Base colour, emissive: what a person picked by eye, stored the way
    // it was picked.
    FLUXION_TEXTURE_USAGE_COLOR_SRGB = 0,

    // A direction, not a colour.
    FLUXION_TEXTURE_USAGE_NORMAL_MAP,

    // Metallic-roughness, occlusion: numbers that happen to be stored in
    // an image.
    FLUXION_TEXTURE_USAGE_MASK_LINEAR,

    FLUXION_TEXTURE_USAGE_GRAYSCALE,

    // Environments and anything else whose values go above one.
    FLUXION_TEXTURE_USAGE_HDR,

    FLUXION_TEXTURE_USAGE_COUNT,
} FluxionTextureUsage;

#define FLUXION_TEXTURE_IMPORT_GENERATE_MIPS    ((u32)(1u << 0))

// The green channel of a normal map, inverted. Which way is up is a
// convention, and the two conventions in use disagree.
#define FLUXION_TEXTURE_IMPORT_FLIP_GREEN       ((u32)(1u << 1))

#define FLUXION_TEXTURE_IMPORT_PREMULTIPLY_ALPHA ((u32)(1u << 2))

// Rescales each mip so that about as many pixels stay above the alpha
// test as were above it in the level before. Without it, alpha-tested
// foliage DISSOLVES as it recedes: averaging pulls the alpha under the
// threshold, and the leaves thin out to nothing.
#define FLUXION_TEXTURE_IMPORT_ALPHA_COVERAGE   ((u32)(1u << 3))

// Lowers roughness on the smaller mips of the map this normal map is
// paired with, from how much the normals vary within each texel. It is
// specular antialiasing, done where it is nearly free -- the normals are
// being read anyway.
#define FLUXION_TEXTURE_IMPORT_ROUGHNESS_LIMITER ((u32)(1u << 4))

// What the database stores for a texture, and what a re-import compares
// against. Plain data with no pointers: it is written into the asset
// database as bytes, and only this module reads it back.
typedef struct FluxionTextureImportSettings
{
    // Raised when the meaning of a field below changes, so settings
    // written by an older build are recognised rather than misread.
    u32 version;

    u32 usage;
    u32 flags;

    f32 alphaCoverageThreshold;

    // Zero means no cap.
    u32 maxSize;

    u32 addressModeU;
    u32 addressModeV;
    u32 filter;
    f32 maxAnisotropy;
} FluxionTextureImportSettings;

#define FLUXION_TEXTURE_IMPORT_SETTINGS_VERSION 1

// What a texture of this usage is imported as when nobody says otherwise.
FluxionTextureImportSettings Fluxion_TextureAsset_DefaultImportSettings(FluxionTextureUsage usage);

// The format a usage cooks to. Uncompressed for now; a compressed one
// arrives here and nowhere else, which is why every caller asks rather
// than choosing.
FluxionRHIFormat Fluxion_TextureAsset_GetUsageFormat(FluxionTextureUsage usage);

// Whether this usage's format carries an sRGB curve. Answered from the
// format itself, so it cannot disagree with what is stored.
bool Fluxion_TextureAsset_IsUsageSRGB(FluxionTextureUsage usage);

// The sampler a texture imported with these settings wants.
FluxionRHISamplerDesc Fluxion_TextureAsset_GetSamplerDesc(const FluxionTextureImportSettings* settings);

FluxionAssetTypeId Fluxion_TextureAsset_TypeId(void);

bool Fluxion_TextureAsset_RegisterType(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue);
void Fluxion_TextureAsset_UnregisterType(void);

#ifdef __cplusplus
}
#endif
