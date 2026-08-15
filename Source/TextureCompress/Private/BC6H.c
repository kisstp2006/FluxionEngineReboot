// BC6H, unsigned, written in one of its fourteen modes.
//
// BC6H stores three channels of high-dynamic-range colour and no alpha.
// Its modes trade endpoint precision against cutting a block into two
// regions; most of them also store the second endpoint as a small
// difference from the first, which is what makes their bit layouts
// intricate. This encoder writes mode 10 throughout: one region, ten bits
// per endpoint channel, both endpoints written out in full. It is the one
// mode with no difference encoding, and therefore the one whose layout is
// a plain sequence of fields.
//
// The numbers in a BC6H block are not the colour. They are quantized
// positions that a decoder expands into sixteen-bit values, interpolates
// between, scales by thirty-one sixty-fourths, and finally READS AS THE
// BITS OF A HALF-PRECISION FLOAT. That last step is the one worth saying
// out loud: the interpolation happens in a space that is roughly
// logarithmic in brightness, not linear in it, which is exactly what a
// format for values above one wants and is nothing like how BC7 behaves.

#include "BlockCompressInternal.h"

#include <string.h>

#define FLUXION_BC6H_PRECISION 10

static const u32 kBC6HWeights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

// --- Half precision, both directions ------------------------------------
//
// Written here rather than reached for: this is the only thing in the
// engine that needs them, and a conversion that rounds differently from
// the one a decoder assumes would show up as a whole texture being
// slightly wrong.

static u16 Fluxion_BC6H_FloatToHalfBits(f32 value)
{
    // Negative values have no representation in the unsigned form of this
    // format, and neither does anything above what a half can hold.
    if (!(value > 0.0f)) return 0; // also catches NaN, which has no sensible block encoding
    if (value > 65504.0f) value = 65504.0f;

    u32 bits;
    memcpy(&bits, &value, sizeof(bits));

    const i32 exponent = (i32)((bits >> 23) & 0xFFu) - 127;
    const u32 mantissa = bits & 0x7FFFFFu;

    if (exponent < -24) return 0;

    if (exponent < -14)
    {
        // Subnormal in half: shift the implicit one back in and round.
        const u32 shift = (u32)(-exponent - 14);
        const u32 full = mantissa | 0x800000u;
        return (u16)((full + (1u << (12u + shift))) >> (13u + shift));
    }

    const u32 halfExponent = (u32)(exponent + 15);
    u32 half = (halfExponent << 10) | (mantissa >> 13);

    // Round to nearest, ties away from zero -- carrying into the exponent
    // if the mantissa overflows, which the addition does on its own
    // because the two fields are adjacent.
    if ((mantissa & 0x1000u) != 0) half += 1u;
    return (u16)half;
}

static f32 Fluxion_BC6H_HalfBitsToFloat(u16 half)
{
    const u32 sign = (u32)(half >> 15) & 1u;
    const u32 exponent = (u32)(half >> 10) & 0x1Fu;
    const u32 mantissa = (u32)half & 0x3FFu;

    u32 bits;
    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            bits = sign << 31;
        }
        else
        {
            // Subnormal: normalise it into the wider exponent range.
            u32 shifted = mantissa;
            i32 e = -1;
            while ((shifted & 0x400u) == 0)
            {
                shifted <<= 1;
                --e;
            }
            shifted &= 0x3FFu;
            bits = (sign << 31) | ((u32)(e + 15 + 112) << 23) | (shifted << 13);
        }
    }
    else if (exponent == 31)
    {
        bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = (sign << 31) | ((exponent + 112u) << 23) | (mantissa << 13);
    }

    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
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
            outTexels[i * 4 + channel] = Fluxion_BC6H_HalfBitsToFloat(half);
        }

        // Said rather than left as whatever the caller's buffer held: this
        // format carries no alpha, and one is fully opaque.
        outTexels[i * 4 + 3] = 1.0f;
    }
}
