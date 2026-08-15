#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/Format.h>

#ifdef __cplusplus
extern "C" {
#endif

// Turning an image into blocks, and back again.
//
// This module is NOT part of what ships. Compressing a texture is
// something an importer does once, on a machine that has the original
// image; a built game only ever reads the blocks. It is a separate module
// for exactly that reason -- an importer plugin and the editor link it,
// and the shipped program does not.
//
// The encoders here are written from the published descriptions of the
// formats. They do not reach for every mode each format offers: a BC7
// block written in one mode is still a BC7 block, and every decoder in
// hardware reads it. What that costs is quality at a given size, not
// correctness, and it is said here rather than left to be discovered from
// the pictures.
//
// The decoders are not here to be fast. They exist so that an encoder can
// be checked against something -- and, because an encoder checked against
// nothing but its own matching decoder agrees with itself and proves
// nothing, so can the hardware.

// What one image's worth of blocks occupies, tightly packed -- the same
// layout a cooked texture level holds. Zero for a format this module does
// not encode.
usize Fluxion_TextureCompress_GetOutputSize(FluxionRHIFormat format, u32 width, u32 height);

// Which pixel layout a format's encoder reads, and its decoder writes.
//
// The low-dynamic-range formats work in four bytes a texel; BC6H works in
// four floats, because a format whose whole point is values above one
// cannot be handed bytes.
typedef enum FluxionTextureCompressPixels
{
    FLUXION_TEXTURE_COMPRESS_PIXELS_NONE = 0,
    FLUXION_TEXTURE_COMPRESS_PIXELS_RGBA8,
    FLUXION_TEXTURE_COMPRESS_PIXELS_RGBA32F,
} FluxionTextureCompressPixels;

FluxionTextureCompressPixels Fluxion_TextureCompress_GetPixelLayout(FluxionRHIFormat format);

// Encodes `width` x `height` texels, read from tightly packed rows in the
// layout above, into `output`.
//
// An image whose size is not a whole number of blocks is padded by
// repeating its edge texels, which is what keeps the last block from
// being encoded against whatever happened to follow the image in memory.
bool Fluxion_TextureCompress_Encode(FluxionRHIFormat format, const void* source, u32 width, u32 height,
                                    void* output, usize outputSize);

// The other direction, into tightly packed rows of `width` x `height`.
bool Fluxion_TextureCompress_Decode(FluxionRHIFormat format, const void* source, usize sourceSize,
                                    u32 width, u32 height, void* output);

#ifdef __cplusplus
}
#endif
