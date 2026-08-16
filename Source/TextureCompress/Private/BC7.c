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

// BC7, mode 6 throughout: one region, colour and alpha together at
// 7+1 bits per endpoint channel, four-bit indices -- the most index
// precision and no partition, the best single choice when only one mode
// is written. Smooth blocks reproduce very closely; a block a
// partitioned mode would have split reproduces less well. Still a valid
// BC7 block every hardware decoder reads; the cost is quality, never
// correctness. The decoder below reads mode 6 only and says so, because
// those are the only blocks it is ever handed.

#include "BlockCompressInternal.h"

#include <math.h>

// The sixteen positions a four-bit index names along the line between the
// endpoints, out of sixty-four.
static const u32 kBC7Weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

static u32 Fluxion_BC7_Interpolate(u32 endpoint0, u32 endpoint1, u32 weight)
{
    return (endpoint0 * (64u - weight) + endpoint1 * weight + 32u) >> 6;
}

// A seven-bit value and the block's own extra bit, together making the
// eight-bit number a decoder actually interpolates between. The extra bit
// is shared by all four channels of one endpoint, which is why it is
// chosen for the endpoint as a whole rather than per channel.
static void Fluxion_BC7_QuantizeEndpoint(const f32 colour[4], u32 outSeven[4], u32* outParityBit)
{
    u32 bestSeven[4] = { 0, 0, 0, 0 };
    u32 bestParity = 0;
    f32 bestError = 0.0f;
    bool haveBest = false;

    for (u32 parity = 0; parity < 2; ++parity)
    {
        u32 seven[4];
        f32 error = 0.0f;

        for (u32 channel = 0; channel < 4; ++channel)
        {
            f32 target = colour[channel];
            if (target < 0.0f) target = 0.0f;
            if (target > 255.0f) target = 255.0f;

            i32 value = (i32)((target - (f32)parity) * 0.5f + 0.5f);
            if (value < 0) value = 0;
            if (value > 127) value = 127;

            seven[channel] = (u32)value;

            const f32 reconstructed = (f32)((value << 1) | (i32)parity);
            const f32 difference = reconstructed - target;
            error += difference * difference;
        }

        if (!haveBest || error < bestError)
        {
            haveBest = true;
            bestError = error;
            bestParity = parity;
            for (u32 channel = 0; channel < 4; ++channel) bestSeven[channel] = seven[channel];
        }
    }

    for (u32 channel = 0; channel < 4; ++channel) outSeven[channel] = bestSeven[channel];
    *outParityBit = bestParity;
}

static void Fluxion_BC7_ExpandEndpoint(const u32 seven[4], u32 parityBit, u32 outEight[4])
{
    for (u32 channel = 0; channel < 4; ++channel) outEight[channel] = (seven[channel] << 1) | parityBit;
}

static u32 Fluxion_BC7_ChooseIndex(const u32 endpoint0[4], const u32 endpoint1[4], const u8 texel[4])
{
    u32 best = 0;
    i64 bestError = -1;

    for (u32 index = 0; index < 16; ++index)
    {
        i64 error = 0;
        for (u32 channel = 0; channel < 4; ++channel)
        {
            const i64 value = (i64)Fluxion_BC7_Interpolate(endpoint0[channel], endpoint1[channel], kBC7Weights4[index]);
            const i64 difference = value - (i64)texel[channel];
            error += difference * difference;
        }

        if (bestError < 0 || error < bestError)
        {
            bestError = error;
            best = index;
        }
    }

    return best;
}

// Given which position along the line each texel was assigned, the pair of
// endpoints that best fits them. This is what turns a bounding box -- which
// is only a starting guess, and a poor one for a block whose colours lie
// along a diagonal -- into a line actually fitted to the data.
static void Fluxion_BC7_RefitEndpoints(const u8 texels[64], const u32 indices[16], f32 outEndpoint0[4], f32 outEndpoint1[4])
{
    f32 sumAA = 0.0f;
    f32 sumAB = 0.0f;
    f32 sumBB = 0.0f;
    f32 sumAC[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    f32 sumBC[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    for (u32 i = 0; i < 16; ++i)
    {
        const f32 b = (f32)kBC7Weights4[indices[i]] * (1.0f / 64.0f);
        const f32 a = 1.0f - b;

        sumAA += a * a;
        sumAB += a * b;
        sumBB += b * b;

        for (u32 channel = 0; channel < 4; ++channel)
        {
            const f32 c = (f32)texels[i * 4 + channel];
            sumAC[channel] += a * c;
            sumBC[channel] += b * c;
        }
    }

    const f32 determinant = sumAA * sumBB - sumAB * sumAB;
    if (fabsf(determinant) < 1e-6f)
    {
        // Every texel landed on the same position, so the line is not
        // determined by them. Whatever the caller already had is at least
        // consistent with the data; a fit computed from a singular system
        // would not be.
        return;
    }

    const f32 inverseDeterminant = 1.0f / determinant;
    for (u32 channel = 0; channel < 4; ++channel)
    {
        outEndpoint0[channel] = (sumBB * sumAC[channel] - sumAB * sumBC[channel]) * inverseDeterminant;
        outEndpoint1[channel] = (sumAA * sumBC[channel] - sumAB * sumAC[channel]) * inverseDeterminant;
    }
}

// The direction the block's colours actually vary along.
//
// The obvious starting guess -- the corners of the box the colours sit in
// -- is wrong whenever a channel runs the other way from the rest. A ramp
// from blue to orange has its red rising while its blue falls, so neither
// box corner is anywhere near the line the colours are on, and an encoder
// that starts there produces a block that is visibly worse for a picture
// that ought to be easy.
//
// Found by repeatedly applying the covariance to a vector, which converges
// on the direction of greatest variation.
static void Fluxion_BC7_PrincipalAxis(const u8 texels[64], const f32 mean[4], f32 outAxis[4])
{
    f32 axis[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    for (u32 iteration = 0; iteration < 8; ++iteration)
    {
        f32 next[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        for (u32 i = 0; i < 16; ++i)
        {
            f32 offset[4];
            f32 projection = 0.0f;
            for (u32 channel = 0; channel < 4; ++channel)
            {
                offset[channel] = (f32)texels[i * 4 + channel] - mean[channel];
                projection += offset[channel] * axis[channel];
            }

            for (u32 channel = 0; channel < 4; ++channel) next[channel] += offset[channel] * projection;
        }

        f32 length = 0.0f;
        for (u32 channel = 0; channel < 4; ++channel) length += next[channel] * next[channel];
        length = sqrtf(length);

        if (length < 1e-6f)
        {
            // Every texel is the mean, so there is no direction to find.
            // Any unit vector will do: the endpoints will coincide and
            // the block is a solid colour either way.
            for (u32 channel = 0; channel < 4; ++channel) outAxis[channel] = (channel == 0) ? 1.0f : 0.0f;
            return;
        }

        const f32 inverseLength = 1.0f / length;
        for (u32 channel = 0; channel < 4; ++channel) axis[channel] = next[channel] * inverseLength;
    }

    for (u32 channel = 0; channel < 4; ++channel) outAxis[channel] = axis[channel];
}

void Fluxion_BC7_EncodeBlock(const u8 texels[64], u8 outBlock[16])
{
    f32 mean[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (u32 i = 0; i < 16; ++i)
    {
        for (u32 channel = 0; channel < 4; ++channel) mean[channel] += (f32)texels[i * 4 + channel];
    }
    for (u32 channel = 0; channel < 4; ++channel) mean[channel] *= 1.0f / 16.0f;

    f32 axis[4];
    Fluxion_BC7_PrincipalAxis(texels, mean, axis);

    // The two texels furthest apart along that direction. Refined below,
    // because the extremes of the data are not quite the ends of the line
    // that fits it best.
    f32 lowest = 0.0f;
    f32 highest = 0.0f;
    for (u32 i = 0; i < 16; ++i)
    {
        f32 projection = 0.0f;
        for (u32 channel = 0; channel < 4; ++channel) projection += ((f32)texels[i * 4 + channel] - mean[channel]) * axis[channel];

        if (i == 0 || projection < lowest) lowest = projection;
        if (i == 0 || projection > highest) highest = projection;
    }

    f32 endpoint0[4];
    f32 endpoint1[4];
    for (u32 channel = 0; channel < 4; ++channel)
    {
        endpoint0[channel] = mean[channel] + axis[channel] * lowest;
        endpoint1[channel] = mean[channel] + axis[channel] * highest;
    }

    u32 seven0[4], seven1[4];
    u32 parity0 = 0, parity1 = 0;
    u32 eight0[4], eight1[4];
    u32 indices[16];

    // Two passes: assign indices to the current line, then move the line
    // to where those indices say it should be. A third pass moves it
    // almost nowhere.
    for (u32 pass = 0; pass < 2; ++pass)
    {
        Fluxion_BC7_QuantizeEndpoint(endpoint0, seven0, &parity0);
        Fluxion_BC7_QuantizeEndpoint(endpoint1, seven1, &parity1);
        Fluxion_BC7_ExpandEndpoint(seven0, parity0, eight0);
        Fluxion_BC7_ExpandEndpoint(seven1, parity1, eight1);

        for (u32 i = 0; i < 16; ++i) indices[i] = Fluxion_BC7_ChooseIndex(eight0, eight1, texels + i * 4);

        if (pass == 0) Fluxion_BC7_RefitEndpoints(texels, indices, endpoint0, endpoint1);
    }

    // The first texel's index is stored in three bits, its high bit taken
    // to be zero. That is not a constraint on the data but on which end of
    // the line is written first, so it is satisfied by turning the line
    // around rather than by accepting a worse index.
    if (indices[0] >= 8)
    {
        for (u32 channel = 0; channel < 4; ++channel)
        {
            const u32 swapSeven = seven0[channel];
            seven0[channel] = seven1[channel];
            seven1[channel] = swapSeven;
        }
        const u32 swapParity = parity0;
        parity0 = parity1;
        parity1 = swapParity;

        for (u32 i = 0; i < 16; ++i) indices[i] = 15u - indices[i];
    }

    FluxionBlockBits bits;
    Fluxion_BlockBits_Init(&bits);

    // A mode is named by that many zero bits followed by a one, so mode
    // six is six zeros and then the bit at position six.
    Fluxion_BlockBits_Put(&bits, 1u << 6, 7);

    // Both endpoints of one channel, then the next channel -- not both
    // channels of one endpoint. The order matters and is the format's,
    // not a choice made here.
    for (u32 channel = 0; channel < 4; ++channel)
    {
        Fluxion_BlockBits_Put(&bits, seven0[channel], 7);
        Fluxion_BlockBits_Put(&bits, seven1[channel], 7);
    }

    Fluxion_BlockBits_Put(&bits, parity0, 1);
    Fluxion_BlockBits_Put(&bits, parity1, 1);

    Fluxion_BlockBits_Put(&bits, indices[0], 3);
    for (u32 i = 1; i < 16; ++i) Fluxion_BlockBits_Put(&bits, indices[i], 4);

    for (u32 i = 0; i < 16; ++i) outBlock[i] = bits.bytes[i];
}

void Fluxion_BC7_DecodeBlock(const u8 block[16], u8 outTexels[64])
{
    FluxionBlockBits bits;
    Fluxion_BlockBits_Init(&bits);
    for (u32 i = 0; i < 16; ++i) bits.bytes[i] = block[i];

    // Mode six and nothing else. Anything written by this module is mode
    // six; a block from elsewhere would need the other seven modes, and
    // returning something plausible for one of those would be worse than
    // returning nothing -- a wrong picture that looks like a right one.
    const u32 modeBits = Fluxion_BlockBits_Get(&bits, 7);
    if (modeBits != (1u << 6))
    {
        for (u32 i = 0; i < 64; ++i) outTexels[i] = 0;
        return;
    }

    u32 seven0[4], seven1[4];
    for (u32 channel = 0; channel < 4; ++channel)
    {
        seven0[channel] = Fluxion_BlockBits_Get(&bits, 7);
        seven1[channel] = Fluxion_BlockBits_Get(&bits, 7);
    }

    const u32 parity0 = Fluxion_BlockBits_Get(&bits, 1);
    const u32 parity1 = Fluxion_BlockBits_Get(&bits, 1);

    u32 eight0[4], eight1[4];
    Fluxion_BC7_ExpandEndpoint(seven0, parity0, eight0);
    Fluxion_BC7_ExpandEndpoint(seven1, parity1, eight1);

    for (u32 i = 0; i < 16; ++i)
    {
        const u32 index = (i == 0) ? Fluxion_BlockBits_Get(&bits, 3) : Fluxion_BlockBits_Get(&bits, 4);
        for (u32 channel = 0; channel < 4; ++channel)
        {
            outTexels[i * 4 + channel] = (u8)Fluxion_BC7_Interpolate(eight0[channel], eight1[channel], kBC7Weights4[index]);
        }
    }
}
