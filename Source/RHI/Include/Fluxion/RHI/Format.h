#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starting set (color + depth-stencil + a couple of common
// vertex-attribute formats) -- purely additive to extend: new entries
// never renumber existing ones,
// since the enum values are never persisted (a shader/pipeline cache key
// hashes the whole descriptor, not this integer alone, so a reordering
// would only matter within a single process run).
typedef enum FluxionRHIFormat
{
    FLUXION_RHI_FORMAT_UNKNOWN = 0,

    FLUXION_RHI_FORMAT_R8G8B8A8_UNORM,
    FLUXION_RHI_FORMAT_R8G8B8A8_SRGB,
    FLUXION_RHI_FORMAT_B8G8R8A8_UNORM,
    FLUXION_RHI_FORMAT_B8G8R8A8_SRGB,

    FLUXION_RHI_FORMAT_R32_FLOAT,
    FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT,
    FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT,
    FLUXION_RHI_FORMAT_R32G32B32_FLOAT,
    FLUXION_RHI_FORMAT_R32G32_FLOAT,

    FLUXION_RHI_FORMAT_D32_FLOAT,
    FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT,

    // --- Block-compressed ------------------------------------------------
    //
    // These store a rectangle of texels as one indivisible unit, so their
    // size arithmetic counts blocks and not texels, and a mip level
    // narrower than one block still costs a whole block. Nothing outside
    // Fluxion_RHI_GetFormatInfo needs to know which formats are which:
    // every size question below is asked in a way that a 1x1 "block"
    // answers correctly for the uncompressed ones.

    FLUXION_RHI_FORMAT_BC4_UNORM,   // one channel
    FLUXION_RHI_FORMAT_BC5_UNORM,   // two channels
    FLUXION_RHI_FORMAT_BC6H_UFLOAT, // three channels, values above one
    FLUXION_RHI_FORMAT_BC7_UNORM,
    FLUXION_RHI_FORMAT_BC7_SRGB,

    // Every ASTC block size is the same sixteen bytes; what changes is how
    // many texels those bytes cover, which is the whole of the quality/size
    // trade.
    FLUXION_RHI_FORMAT_ASTC_4X4_UNORM,
    FLUXION_RHI_FORMAT_ASTC_4X4_SRGB,
    FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT,
    FLUXION_RHI_FORMAT_ASTC_6X6_UNORM,
    FLUXION_RHI_FORMAT_ASTC_6X6_SRGB,
    FLUXION_RHI_FORMAT_ASTC_6X6_FLOAT,
    FLUXION_RHI_FORMAT_ASTC_8X8_UNORM,
    FLUXION_RHI_FORMAT_ASTC_8X8_SRGB,
    FLUXION_RHI_FORMAT_ASTC_8X8_FLOAT,
} FluxionRHIFormat;

// What a format costs and what it means, in the one place that knows.
//
// Both the backends and the layers above them need this, and each of them
// working it out for itself is how a compressed format ends up sized as
// though it were four bytes a texel -- an answer that is wrong by a factor
// of eight and produces a staging buffer that is merely too big, so
// nothing fails and the picture is simply garbage.
typedef struct FluxionRHIFormatInfo
{
    // How many texels across and down one block covers. One by one for an
    // uncompressed format, which is what lets the same arithmetic serve
    // both kinds.
    u32 blockWidth;
    u32 blockHeight;

    // What one block occupies. For an uncompressed format this is one
    // texel's worth.
    u32 blockBytes;

    // Whether the block covers more than a single texel. Derivable from
    // the two above, and said anyway: a caller asking "is this
    // compressed" should not have to know that the answer is spelled
    // `blockWidth > 1`.
    bool compressed;

    // Whether sampling this format applies the sRGB transfer function.
    // THE COLOUR SPACE IS THE FORMAT, so this is read off the format and
    // never stored beside it.
    bool srgb;

    bool depth;
} FluxionRHIFormatInfo;

// Zeroed blockBytes for a format this does not know, which is a size of
// zero rather than a guess: an allocation made from a confidently wrong
// number is silently too small, and nothing reports that.
FluxionRHIFormatInfo Fluxion_RHI_GetFormatInfo(FluxionRHIFormat format);

// Blocks across and down, for a level of this size in texels. A level
// narrower or shorter than one block still costs one, which is why these
// round up and are not a division.
u32 Fluxion_RHI_GetFormatBlockColumns(FluxionRHIFormat format, u32 widthInTexels);
u32 Fluxion_RHI_GetFormatBlockRows(FluxionRHIFormat format, u32 heightInTexels);

// One row of blocks, tightly packed. Zero for a format this does not
// know, or for a width of zero.
usize Fluxion_RHI_GetFormatRowBytes(FluxionRHIFormat format, u32 widthInTexels);

// One whole level, tightly packed.
usize Fluxion_RHI_GetFormatLevelBytes(FluxionRHIFormat format, u32 widthInTexels, u32 heightInTexels);

bool Fluxion_RHI_IsFormatCompressed(FluxionRHIFormat format);
bool Fluxion_RHI_IsFormatSRGB(FluxionRHIFormat format);

#ifdef __cplusplus
}
#endif
