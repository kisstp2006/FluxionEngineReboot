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

// BC6H, unsigned: three channels of HDR colour, no alpha. This encoder
// writes mode 10 throughout -- one region, ten bits per endpoint
// channel, both endpoints in full, the one mode with no difference
// encoding and therefore a plain sequence of fields.
//
// The numbers in a block are not the colour: they are quantized
// positions the decoder expands, interpolates, scales by 31/64 and
// finally READS AS THE BITS OF A HALF -- interpolation in a roughly
// logarithmic space, which is what an HDR format wants and nothing like
// BC7.

#include "BlockCompressInternal.h"

#include <Fluxion/Foundation/Half.h>

#include <string.h>

#define FLUXION_BC6H_PRECISION 10

static const u32 kBC6HWeights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

// --- Half precision -----------------------------------------------------
//
// The conversion itself belongs to Foundation, because nothing about it
// is this format's. What IS this format's is the clamp below: the
// unsigned form of BC6H has no way to store a negative number and no
// exponent above what a half holds, so a value outside that has to become
// something before it is converted at all.

// The largest value a half can hold. Anything above it converts to
// infinity, and an infinity read back as a quantized position is not a
// colour at all.
#define FLUXION_LARGEST_FINITE_HALF 65504.0f

static u16 Fluxion_BC6H_FloatToHalfBits(f32 value)
{
    // Written as "not greater than zero" so that it also catches
    // not-a-number, which compares false against everything. A NaN has no
    // sensible block encoding, and the general conversion rightly keeps
    // one as a NaN -- so it has to be stopped here rather than turned
    // into a half NaN and read back as an enormous quantized position.
    if (!(value > 0.0f)) return 0;

    if (value > FLUXION_LARGEST_FINITE_HALF) value = FLUXION_LARGEST_FINITE_HALF;

    return Fluxion_Half_FromFloat(value);
}

// --- The format's own arithmetic ----------------------------------------

static u32 Fluxion_BC6H_Unquantize(u32 quantized)
{
    // The two ends are exact by definition rather than by the formula --
    // without them the brightest value a block can name would fall just
    // short of the brightest a half can hold.
    if (quantized == 0) return 0;
    if (quantized == (1u << FLUXION_BC6H_PRECISION) - 1u) return 0xFFFFu;
    return ((quantized << 15) + 0x4000u) >> (FLUXION_BC6H_PRECISION - 1u);
}

static u32 Fluxion_BC6H_Quantize(u32 unquantized)
{
    // Found by inverting the formula above and then checking the
    // neighbours, rather than by trusting the inversion. The formula has
    // two special cases in it, so its algebraic inverse is wrong near
    // both ends -- and near the ends is exactly where a highlight lives.
    i64 guess = ((i64)unquantized - 32) / 64;
    if (guess < 0) guess = 0;
    if (guess > (1 << FLUXION_BC6H_PRECISION) - 1) guess = (1 << FLUXION_BC6H_PRECISION) - 1;

    u32 best = (u32)guess;
    i64 bestError = -1;

    for (i64 candidate = guess - 1; candidate <= guess + 1; ++candidate)
    {
        if (candidate < 0 || candidate > (1 << FLUXION_BC6H_PRECISION) - 1) continue;

        const i64 difference = (i64)Fluxion_BC6H_Unquantize((u32)candidate) - (i64)unquantized;
        const i64 error = difference < 0 ? -difference : difference;
        if (bestError < 0 || error < bestError)
        {
            bestError = error;
            best = (u32)candidate;
        }
    }

    return best;
}

// The value a decoder ends up with, as the bits of a half.
static u16 Fluxion_BC6H_Finish(u32 interpolated)
{
    return (u16)((interpolated * 31u) >> 6);
}

static u32 Fluxion_BC6H_Interpolate(u32 endpoint0, u32 endpoint1, u32 weight)
{
    return (endpoint0 * (64u - weight) + endpoint1 * weight + 32u) >> 6;
}

// The inverse of Finish: where an interpolation would have to land for a
// decoder to arrive at these half bits.
static u32 Fluxion_BC6H_UnFinish(u16 halfBits)
{
    const u32 value = ((u32)halfBits << 6) / 31u;
    return value > 0xFFFFu ? 0xFFFFu : value;
}

void Fluxion_BC6H_EncodeBlock(const f32 texels[64], u8 outBlock[16])
{
    // Alpha is dropped rather than approximated: this format has no
    // fourth channel at all, and a caller handing one over is better
    // served by knowing it went nowhere than by seeing it reappear
    // somewhere unexpected.
    u16 halfBits[16][3];
    u32 targets[16][3];

    for (u32 i = 0; i < 16; ++i)
    {
        for (u32 channel = 0; channel < 3; ++channel)
        {
            halfBits[i][channel] = Fluxion_BC6H_FloatToHalfBits(texels[i * 4 + channel]);
            targets[i][channel] = Fluxion_BC6H_UnFinish(halfBits[i][channel]);
        }
    }

    u32 lowest[3] = { 0xFFFFu, 0xFFFFu, 0xFFFFu };
    u32 highest[3] = { 0, 0, 0 };
    for (u32 i = 0; i < 16; ++i)
    {
        for (u32 channel = 0; channel < 3; ++channel)
        {
            if (targets[i][channel] < lowest[channel]) lowest[channel] = targets[i][channel];
            if (targets[i][channel] > highest[channel]) highest[channel] = targets[i][channel];
        }
    }

    u32 quantized0[3], quantized1[3];
    for (u32 channel = 0; channel < 3; ++channel)
    {
        quantized0[channel] = Fluxion_BC6H_Quantize(lowest[channel]);
        quantized1[channel] = Fluxion_BC6H_Quantize(highest[channel]);
    }

    u32 endpoint0[3], endpoint1[3];
    for (u32 channel = 0; channel < 3; ++channel)
    {
        endpoint0[channel] = Fluxion_BC6H_Unquantize(quantized0[channel]);
        endpoint1[channel] = Fluxion_BC6H_Unquantize(quantized1[channel]);
    }

    // Compared as half bits rather than as the interpolated numbers,
    // because half bits are roughly logarithmic in brightness -- which
    // means an error of one here means the same thing in a highlight as
    // it does in a shadow, and an error measured the other way does not.
    u32 indices[16];
    for (u32 i = 0; i < 16; ++i)
    {
        u32 best = 0;
        i64 bestError = -1;

        for (u32 index = 0; index < 16; ++index)
        {
            i64 error = 0;
            for (u32 channel = 0; channel < 3; ++channel)
            {
                const i64 value = (i64)Fluxion_BC6H_Finish(Fluxion_BC6H_Interpolate(endpoint0[channel], endpoint1[channel], kBC6HWeights4[index]));
                const i64 difference = value - (i64)halfBits[i][channel];
                error += difference * difference;
            }

            if (bestError < 0 || error < bestError)
            {
                bestError = error;
                best = index;
            }
        }

        indices[i] = best;
    }

    // The first texel's index is three bits wide, as in BC7 and for the
    // same reason.
    if (indices[0] >= 8)
    {
        for (u32 channel = 0; channel < 3; ++channel)
        {
            const u32 swap = quantized0[channel];
            quantized0[channel] = quantized1[channel];
            quantized1[channel] = swap;
        }
        for (u32 i = 0; i < 16; ++i) indices[i] = 15u - indices[i];
    }

    FluxionBlockBits bits;
    Fluxion_BlockBits_Init(&bits);

    // Mode 10 -- five bits, read as a number, is three. The one-region
    // modes are the four whose five-bit field is odd in its low two bits;
    // the two-region ones and the two short-form modes fill the rest.
    Fluxion_BlockBits_Put(&bits, 3, 5);

    for (u32 channel = 0; channel < 3; ++channel) Fluxion_BlockBits_Put(&bits, quantized0[channel], FLUXION_BC6H_PRECISION);
    for (u32 channel = 0; channel < 3; ++channel) Fluxion_BlockBits_Put(&bits, quantized1[channel], FLUXION_BC6H_PRECISION);

    Fluxion_BlockBits_Put(&bits, indices[0], 3);
    for (u32 i = 1; i < 16; ++i) Fluxion_BlockBits_Put(&bits, indices[i], 4);

    for (u32 i = 0; i < 16; ++i) outBlock[i] = bits.bytes[i];
}

void Fluxion_BC6H_DecodeBlock(const u8 block[16], f32 outTexels[64])
{
    FluxionBlockBits bits;
    Fluxion_BlockBits_Init(&bits);
    for (u32 i = 0; i < 16; ++i) bits.bytes[i] = block[i];

    // Mode 10 and nothing else, for the same reason the BC7 decoder reads
    // one mode: these are the only blocks it is ever handed.
    if (Fluxion_BlockBits_Get(&bits, 5) != 3)
    {
        for (u32 i = 0; i < 64; ++i) outTexels[i] = 0.0f;
        return;
    }

    u32 quantized0[3], quantized1[3];
    for (u32 channel = 0; channel < 3; ++channel) quantized0[channel] = Fluxion_BlockBits_Get(&bits, FLUXION_BC6H_PRECISION);
    for (u32 channel = 0; channel < 3; ++channel) quantized1[channel] = Fluxion_BlockBits_Get(&bits, FLUXION_BC6H_PRECISION);

    u32 endpoint0[3], endpoint1[3];
    for (u32 channel = 0; channel < 3; ++channel)
    {
        endpoint0[channel] = Fluxion_BC6H_Unquantize(quantized0[channel]);
        endpoint1[channel] = Fluxion_BC6H_Unquantize(quantized1[channel]);
    }

    for (u32 i = 0; i < 16; ++i)
    {
        const u32 index = (i == 0) ? Fluxion_BlockBits_Get(&bits, 3) : Fluxion_BlockBits_Get(&bits, 4);

        for (u32 channel = 0; channel < 3; ++channel)
        {
            const u16 half = Fluxion_BC6H_Finish(Fluxion_BC6H_Interpolate(endpoint0[channel], endpoint1[channel], kBC6HWeights4[index]));
            outTexels[i * 4 + channel] = Fluxion_Half_ToFloat(half);
        }

        // Said rather than left as whatever the caller's buffer held: this
        // format carries no alpha, and one is fully opaque.
        outTexels[i * 4 + 3] = 1.0f;
    }
}
