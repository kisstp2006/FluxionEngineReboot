// Which encoder a format gets, and the walk over an image that feeds it.
//
// The per-format work is one file each; this one only decides which and
// hands it a block at a time.

#include <Fluxion/TextureCompress/BlockCompress.h>

#include "BlockCompressInternal.h"

#include <Fluxion/Foundation/Log.h>

#include <string.h>

#define FLUXION_TEXTURE_COMPRESS_LOG_CATEGORY "TextureCompress"

FluxionTextureCompressPixels Fluxion_TextureCompress_GetPixelLayout(FluxionRHIFormat format)
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_BC4_UNORM:
        case FLUXION_RHI_FORMAT_BC5_UNORM:
        case FLUXION_RHI_FORMAT_BC7_UNORM:
        case FLUXION_RHI_FORMAT_BC7_SRGB:
        // Four texels by four is the ASTC block size whose weight grid can
        // cover every texel on its own. The larger ones carry a smaller
        // grid and lean on the decoder to interpolate between its points,
        // which is a piece of the format this module does not write --
        // see ASTC.c. They are refused here rather than encoded badly.
        case FLUXION_RHI_FORMAT_ASTC_4X4_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_4X4_SRGB:
            return FLUXION_TEXTURE_COMPRESS_PIXELS_RGBA8;

        case FLUXION_RHI_FORMAT_BC6H_UFLOAT:
            return FLUXION_TEXTURE_COMPRESS_PIXELS_RGBA32F;

        default:
            return FLUXION_TEXTURE_COMPRESS_PIXELS_NONE;
    }
}

usize Fluxion_TextureCompress_GetOutputSize(FluxionRHIFormat format, u32 width, u32 height)
{
    if (Fluxion_TextureCompress_GetPixelLayout(format) == FLUXION_TEXTURE_COMPRESS_PIXELS_NONE) return 0;

    // Asked of the format rather than worked out here, so a cooked file
    // and the thing that wrote it cannot disagree about how large a level
    // is.
    return Fluxion_RHI_GetFormatLevelBytes(format, width, height);
}

// --- Per-format block handling ------------------------------------------

static void Fluxion_TextureCompress_EncodeOneBlock(FluxionRHIFormat format, const u8 texels[64], u8* outBlock)
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_BC4_UNORM:
        {
            // Red only. A caller storing a mask in some other channel has
            // to move it first -- picking a channel here would be a
            // decision made where nobody can see it.
            u8 values[16];
            for (u32 i = 0; i < 16; ++i) values[i] = texels[i * 4 + 0];
            Fluxion_BC4_EncodeBlock(values, outBlock);
            return;
        }

        case FLUXION_RHI_FORMAT_BC5_UNORM:
        {
            // Two BC4 blocks side by side, red then green -- which is all
            // BC5 is.
            u8 red[16];
            u8 green[16];
            for (u32 i = 0; i < 16; ++i)
            {
                red[i] = texels[i * 4 + 0];
                green[i] = texels[i * 4 + 1];
            }
            Fluxion_BC4_EncodeBlock(red, outBlock);
            Fluxion_BC4_EncodeBlock(green, outBlock + 8);
            return;
        }

        case FLUXION_RHI_FORMAT_BC7_UNORM:
        case FLUXION_RHI_FORMAT_BC7_SRGB:
            // The sRGB and the linear form hold identical bits; which
            // curve applies is the reading, not the writing.
            Fluxion_BC7_EncodeBlock(texels, outBlock);
            return;

        case FLUXION_RHI_FORMAT_ASTC_4X4_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_4X4_SRGB:
            Fluxion_ASTC_EncodeBlock(texels, outBlock);
            return;

        default:
            return;
    }
}

static void Fluxion_TextureCompress_DecodeOneBlock(FluxionRHIFormat format, const u8* block, u8 outTexels[64])
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_BC4_UNORM:
        {
            u8 values[16];
            Fluxion_BC4_DecodeBlock(block, values);
            for (u32 i = 0; i < 16; ++i)
            {
                outTexels[i * 4 + 0] = values[i];
                outTexels[i * 4 + 1] = 0;
                outTexels[i * 4 + 2] = 0;
                outTexels[i * 4 + 3] = 255;
            }
            return;
        }

        case FLUXION_RHI_FORMAT_BC5_UNORM:
        {
            u8 red[16];
            u8 green[16];
            Fluxion_BC4_DecodeBlock(block, red);
            Fluxion_BC4_DecodeBlock(block + 8, green);
            for (u32 i = 0; i < 16; ++i)
            {
                outTexels[i * 4 + 0] = red[i];
                outTexels[i * 4 + 1] = green[i];
                outTexels[i * 4 + 2] = 0;
                outTexels[i * 4 + 3] = 255;
            }
            return;
        }

        case FLUXION_RHI_FORMAT_BC7_UNORM:
        case FLUXION_RHI_FORMAT_BC7_SRGB:
            Fluxion_BC7_DecodeBlock(block, outTexels);
            return;

        case FLUXION_RHI_FORMAT_ASTC_4X4_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_4X4_SRGB:
            Fluxion_ASTC_DecodeBlock(block, outTexels);
            return;

        default:
            memset(outTexels, 0, 64);
            return;
    }
}

// --- The walk over an image ---------------------------------------------

bool Fluxion_TextureCompress_Encode(FluxionRHIFormat format, const void* source, u32 width, u32 height,
                                    void* output, usize outputSize)
{
    const FluxionTextureCompressPixels layout = Fluxion_TextureCompress_GetPixelLayout(format);
    if (layout == FLUXION_TEXTURE_COMPRESS_PIXELS_NONE)
    {
        FLUXION_LOG_ERROR(FLUXION_TEXTURE_COMPRESS_LOG_CATEGORY, "nothing here encodes format %d", (int)format);
        return false;
    }

    if (source == NULL || output == NULL || width == 0 || height == 0) return false;

    const usize needed = Fluxion_TextureCompress_GetOutputSize(format, width, height);
    if (needed == 0 || outputSize < needed) return false;

    const FluxionRHIFormatInfo info = Fluxion_RHI_GetFormatInfo(format);
    const u32 blockColumns = Fluxion_RHI_GetFormatBlockColumns(format, width);
    const u32 blockRows = Fluxion_RHI_GetFormatBlockRows(format, height);
    const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(format, width);

    u8* out = (u8*)output;

    for (u32 blockY = 0; blockY < blockRows; ++blockY)
    {
        for (u32 blockX = 0; blockX < blockColumns; ++blockX)
        {
            u8* block = out + (usize)blockY * rowBytes + (usize)blockX * info.blockBytes;

            if (layout == FLUXION_TEXTURE_COMPRESS_PIXELS_RGBA32F)
            {
                f32 texels[64];
                Fluxion_Block_GatherRGBA32F((const f32*)source, width, height, blockX, blockY, texels);
                Fluxion_BC6H_EncodeBlock(texels, block);
                continue;
            }

            u8 texels[64];
            Fluxion_Block_GatherRGBA8((const u8*)source, width, height, blockX, blockY, texels);
            Fluxion_TextureCompress_EncodeOneBlock(format, texels, block);
        }
    }

    return true;
}

bool Fluxion_TextureCompress_Decode(FluxionRHIFormat format, const void* source, usize sourceSize,
                                    u32 width, u32 height, void* output)
{
    const FluxionTextureCompressPixels layout = Fluxion_TextureCompress_GetPixelLayout(format);
    if (layout == FLUXION_TEXTURE_COMPRESS_PIXELS_NONE) return false;
    if (source == NULL || output == NULL || width == 0 || height == 0) return false;

    const usize needed = Fluxion_TextureCompress_GetOutputSize(format, width, height);
    if (needed == 0 || sourceSize < needed) return false;

    const FluxionRHIFormatInfo info = Fluxion_RHI_GetFormatInfo(format);
    const u32 blockColumns = Fluxion_RHI_GetFormatBlockColumns(format, width);
    const u32 blockRows = Fluxion_RHI_GetFormatBlockRows(format, height);
    const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(format, width);

    const u8* in = (const u8*)source;

    for (u32 blockY = 0; blockY < blockRows; ++blockY)
    {
        for (u32 blockX = 0; blockX < blockColumns; ++blockX)
        {
            const u8* block = in + (usize)blockY * rowBytes + (usize)blockX * info.blockBytes;

            if (layout == FLUXION_TEXTURE_COMPRESS_PIXELS_RGBA32F)
            {
                f32 texels[64];
                Fluxion_BC6H_DecodeBlock(block, texels);
                Fluxion_Block_ScatterRGBA32F((f32*)output, width, height, blockX, blockY, texels);
                continue;
            }

            u8 texels[64];
            Fluxion_TextureCompress_DecodeOneBlock(format, block, texels);
            Fluxion_Block_ScatterRGBA8((u8*)output, width, height, blockX, blockY, texels);
        }
    }

    return true;
}
