#pragma once

#include <Fluxion/Foundation/Types.h>

// The pieces every block format here needs: a way to say bits into a
// sixteen-byte block and read them back out in the same order, and a way
// to lift one block's worth of texels out of an image that may not be a
// whole number of blocks across.

#define FLUXION_BLOCK_BYTES_MAX 16

typedef struct FluxionBlockBits
{
    u8 bytes[FLUXION_BLOCK_BYTES_MAX];
    u32 position; // in bits, from the low bit of byte zero upwards
} FluxionBlockBits;

static inline void Fluxion_BlockBits_Init(FluxionBlockBits* bits)
{
    for (u32 i = 0; i < FLUXION_BLOCK_BYTES_MAX; ++i) bits->bytes[i] = 0;
    bits->position = 0;
}

// Least significant bit first, byte zero first. Every format here is
// described that way, and describing it once means the description cannot
// differ between the writer and the reader.
static inline void Fluxion_BlockBits_Put(FluxionBlockBits* bits, u32 value, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        const u32 bit = (value >> i) & 1u;
        const u32 at = bits->position + i;
        if (at >= FLUXION_BLOCK_BYTES_MAX * 8) return;
        bits->bytes[at >> 3] |= (u8)(bit << (at & 7u));
    }
    bits->position += count;
}

static inline u32 Fluxion_BlockBits_Get(FluxionBlockBits* bits, u32 count)
{
    u32 value = 0;
    for (u32 i = 0; i < count; ++i)
    {
        const u32 at = bits->position + i;
        if (at >= FLUXION_BLOCK_BYTES_MAX * 8) break;
        const u32 bit = (bits->bytes[at >> 3] >> (at & 7u)) & 1u;
        value |= bit << i;
    }
    bits->position += count;
    return value;
}

// One 4x4 block of an RGBA8 image, with the edge repeated where the image
// runs out. Repeated rather than zero-filled: a black or transparent
// margin invented here would be encoded as though it were real, and would
// show along the right and bottom edges of anything whose size is not a
// multiple of four.
static inline void Fluxion_Block_GatherRGBA8(const u8* source, u32 width, u32 height, u32 blockX, u32 blockY, u8 outTexels[64])
{
    for (u32 y = 0; y < 4; ++y)
    {
        u32 sourceY = blockY * 4 + y;
        if (sourceY >= height) sourceY = height - 1;

        for (u32 x = 0; x < 4; ++x)
        {
            u32 sourceX = blockX * 4 + x;
            if (sourceX >= width) sourceX = width - 1;

            const u8* texel = source + ((usize)sourceY * width + sourceX) * 4;
            u8* out = outTexels + (y * 4 + x) * 4;
            out[0] = texel[0];
            out[1] = texel[1];
            out[2] = texel[2];
            out[3] = texel[3];
        }
    }
}

static inline void Fluxion_Block_GatherRGBA32F(const f32* source, u32 width, u32 height, u32 blockX, u32 blockY, f32 outTexels[64])
{
    for (u32 y = 0; y < 4; ++y)
    {
        u32 sourceY = blockY * 4 + y;
        if (sourceY >= height) sourceY = height - 1;

        for (u32 x = 0; x < 4; ++x)
        {
            u32 sourceX = blockX * 4 + x;
            if (sourceX >= width) sourceX = width - 1;

            const f32* texel = source + ((usize)sourceY * width + sourceX) * 4;
            f32* out = outTexels + (y * 4 + x) * 4;
            out[0] = texel[0];
            out[1] = texel[1];
            out[2] = texel[2];
            out[3] = texel[3];
        }
    }
}

// Writes one decoded block back into an image, dropping whatever fell
// outside it.
static inline void Fluxion_Block_ScatterRGBA8(u8* destination, u32 width, u32 height, u32 blockX, u32 blockY, const u8 texels[64])
{
    for (u32 y = 0; y < 4; ++y)
    {
        const u32 destinationY = blockY * 4 + y;
        if (destinationY >= height) continue;

        for (u32 x = 0; x < 4; ++x)
        {
            const u32 destinationX = blockX * 4 + x;
            if (destinationX >= width) continue;

            u8* out = destination + ((usize)destinationY * width + destinationX) * 4;
            const u8* texel = texels + (y * 4 + x) * 4;
            out[0] = texel[0];
            out[1] = texel[1];
            out[2] = texel[2];
            out[3] = texel[3];
        }
    }
}

static inline void Fluxion_Block_ScatterRGBA32F(f32* destination, u32 width, u32 height, u32 blockX, u32 blockY, const f32 texels[64])
{
    for (u32 y = 0; y < 4; ++y)
    {
        const u32 destinationY = blockY * 4 + y;
        if (destinationY >= height) continue;

        for (u32 x = 0; x < 4; ++x)
        {
            const u32 destinationX = blockX * 4 + x;
            if (destinationX >= width) continue;

            f32* out = destination + ((usize)destinationY * width + destinationX) * 4;
            const f32* texel = texels + (y * 4 + x) * 4;
            out[0] = texel[0];
            out[1] = texel[1];
            out[2] = texel[2];
            out[3] = texel[3];
        }
    }
}

// --- Per-format entry points, one file each -----------------------------

void Fluxion_BC4_EncodeBlock(const u8 values[16], u8 outBlock[8]);
void Fluxion_BC4_DecodeBlock(const u8 block[8], u8 outValues[16]);

void Fluxion_BC7_EncodeBlock(const u8 texels[64], u8 outBlock[16]);
void Fluxion_BC7_DecodeBlock(const u8 block[16], u8 outTexels[64]);

void Fluxion_BC6H_EncodeBlock(const f32 texels[64], u8 outBlock[16]);
void Fluxion_BC6H_DecodeBlock(const u8 block[16], f32 outTexels[64]);
