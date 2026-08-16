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

void Fluxion_ASTC_EncodeBlock(const u8 texels[64], u8 outBlock[16]);
void Fluxion_ASTC_DecodeBlock(const u8 block[16], u8 outTexels[64]);
