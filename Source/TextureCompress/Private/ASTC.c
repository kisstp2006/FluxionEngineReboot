// ASTC, four texels by four, low dynamic range.
//
// ASTC is not one layout but a family. A block names its own weight-grid
// size, its own weight precision and its own way of describing colour,
// and the number of bits left for the endpoints is whatever the first two
// did not use. That flexibility is the format's whole point and also why
// an encoder has to choose a corner of it and stay there.
//
// This one writes: a four-by-four weight grid (one weight per texel, so
// nothing is interpolated between grid points), two bits a weight, one
// region, and colour endpoint mode 12 -- red, green, blue and alpha, each
// endpoint written out in full. Sixteen weights at two bits is
// thirty-two, the configuration fields are seventeen, and the seventy-nine
// bits that remain are more than the sixty-four eight-bit endpoint values
// need.
//
// Only the four-by-four block size is encoded here. The larger ones
// cannot give every texel its own weight -- a six-by-six block would need
// thirty-six weights and an eight-by-eight sixty-four -- so they carry a
// SMALLER grid than the block and the decoder interpolates between grid
// points to reach each texel. That interpolation is a piece of the format
// in its own right, and writing it blind, with no hardware here able to
// check the result, would be guessing. Those sizes are refused rather
// than approximated.
//
// WHAT HAS NOT BEEN CHECKED: no device on the machine this was written on
// supports ASTC at all -- not the discrete GPU, not the software
// rasteriser. The BC formats beside this one are compared against two
// real drivers and agree with them exactly; this one has only its own
// decoder to agree with, which is a much weaker thing. The comparison
// runs automatically on the first device that does support ASTC.

#include "BlockCompressInternal.h"

#include <math.h>

// --- The one configuration this file writes ------------------------------

// Eleven bits saying: one plane, low weight range, a four-by-four weight
// grid, and two bits a weight.
//
// The field is not laid out in one piece. The two low bits and bit four
// together hold the weight range; bits five and six hold one grid
// dimension less two; bits seven and eight the other less four. This
// value is those fields filled in, and it is written as a number rather
// than assembled at runtime because it never varies here.
#define FLUXION_ASTC_BLOCK_MODE_4X4 0x42u

// One region.
#define FLUXION_ASTC_PARTITION_COUNT 1u

// Red, green, blue and alpha, each endpoint given outright rather than as
// a base and a difference.
#define FLUXION_ASTC_COLOR_ENDPOINT_MODE 12u

#define FLUXION_ASTC_WEIGHT_BITS 2u
#define FLUXION_ASTC_WEIGHT_COUNT 16u

// What a two-bit weight means, out of sixty-four. Not evenly spaced by
// division: the format names these four values.
static const u32 kAstcWeightValues[4] = { 0, 21, 43, 64 };

// --- Bit placement -------------------------------------------------------

static void Fluxion_ASTC_PutBit(u8 block[16], u32 position, u32 bit)
{
    if (position >= 128) return;
    if (bit != 0) block[position >> 3] |= (u8)(1u << (position & 7u));
}

static u32 Fluxion_ASTC_GetBit(const u8 block[16], u32 position)
{
    if (position >= 128) return 0;
    return (block[position >> 3] >> (position & 7u)) & 1u;
}

// --- Endpoints -----------------------------------------------------------

// An eight-bit endpoint value expanded to the sixteen bits the
// interpolation happens in. Repeating the byte rather than shifting it:
// shifting alone would make the brightest value fall short of the
// brightest sixteen-bit one, so white would decode as almost-white.
static u32 Fluxion_ASTC_Expand(u32 value)
{
    return (value << 8) | value;
}

static u32 Fluxion_ASTC_Interpolate(u32 endpoint0, u32 endpoint1, u32 weight)
{
    const u32 wide0 = Fluxion_ASTC_Expand(endpoint0);
    const u32 wide1 = Fluxion_ASTC_Expand(endpoint1);
    return ((wide0 * (64u - weight) + wide1 * weight + 32u) >> 6) >> 8;
}

// --- The direction the block's colours vary along ------------------------
//
// The same reasoning as the BC7 encoder's: a box corner is not on the line
// whenever one channel runs the other way from the rest.
static void Fluxion_ASTC_PrincipalAxis(const u8 texels[64], const f32 mean[4], f32 outAxis[4])
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
            for (u32 channel = 0; channel < 4; ++channel) outAxis[channel] = (channel == 0) ? 1.0f : 0.0f;
            return;
        }

        const f32 inverseLength = 1.0f / length;
        for (u32 channel = 0; channel < 4; ++channel) axis[channel] = next[channel] * inverseLength;
    }

    for (u32 channel = 0; channel < 4; ++channel) outAxis[channel] = axis[channel];
}

static u32 Fluxion_ASTC_ChooseWeight(const u32 endpoint0[4], const u32 endpoint1[4], const u8 texel[4])
{
    u32 best = 0;
    i64 bestError = -1;

    for (u32 index = 0; index < 4; ++index)
    {
        i64 error = 0;
        for (u32 channel = 0; channel < 4; ++channel)
        {
            const i64 value = (i64)Fluxion_ASTC_Interpolate(endpoint0[channel], endpoint1[channel], kAstcWeightValues[index]);
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

void Fluxion_ASTC_EncodeBlock(const u8 texels[64], u8 outBlock[16])
{
    for (u32 i = 0; i < 16; ++i) outBlock[i] = 0;

    f32 mean[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (u32 i = 0; i < 16; ++i)
    {
        for (u32 channel = 0; channel < 4; ++channel) mean[channel] += (f32)texels[i * 4 + channel];
    }
    for (u32 channel = 0; channel < 4; ++channel) mean[channel] *= 1.0f / 16.0f;

    f32 axis[4];
    Fluxion_ASTC_PrincipalAxis(texels, mean, axis);

    f32 lowest = 0.0f;
    f32 highest = 0.0f;
    for (u32 i = 0; i < 16; ++i)
    {
        f32 projection = 0.0f;
        for (u32 channel = 0; channel < 4; ++channel) projection += ((f32)texels[i * 4 + channel] - mean[channel]) * axis[channel];

        if (i == 0 || projection < lowest) lowest = projection;
        if (i == 0 || projection > highest) highest = projection;
    }

    u32 endpoint0[4];
    u32 endpoint1[4];
    for (u32 channel = 0; channel < 4; ++channel)
    {
        f32 low = mean[channel] + axis[channel] * lowest;
        f32 high = mean[channel] + axis[channel] * highest;

        if (low < 0.0f) low = 0.0f;
        if (low > 255.0f) low = 255.0f;
        if (high < 0.0f) high = 0.0f;
        if (high > 255.0f) high = 255.0f;

        // Eight bits with nothing thrown away: seventy-nine bits are
        // available and eight endpoint values need sixty-four, so this
        // configuration is the one where the quantization step is the
        // identity.
        endpoint0[channel] = (u32)(low + 0.5f);
        endpoint1[channel] = (u32)(high + 0.5f);
    }

    // Which endpoint is written first is not free. A decoder reading this
    // colour mode compares the two endpoints' red, green and blue sums,
    // and takes the SECOND arrangement -- one that shifts blue -- when the
    // first endpoint is the brighter. Writing the darker one first keeps
    // it out of that branch entirely.
    const u32 sum0 = endpoint0[0] + endpoint0[1] + endpoint0[2];
    const u32 sum1 = endpoint1[0] + endpoint1[1] + endpoint1[2];
    if (sum1 < sum0)
    {
        for (u32 channel = 0; channel < 4; ++channel)
        {
            const u32 swap = endpoint0[channel];
            endpoint0[channel] = endpoint1[channel];
            endpoint1[channel] = swap;
        }
    }

    u32 weights[FLUXION_ASTC_WEIGHT_COUNT];
    for (u32 i = 0; i < FLUXION_ASTC_WEIGHT_COUNT; ++i) weights[i] = Fluxion_ASTC_ChooseWeight(endpoint0, endpoint1, texels + i * 4);

    // --- The configuration and the endpoints, from the bottom up --------
    u32 position = 0;
    for (u32 i = 0; i < 11; ++i, ++position) Fluxion_ASTC_PutBit(outBlock, position, (FLUXION_ASTC_BLOCK_MODE_4X4 >> i) & 1u);
    for (u32 i = 0; i < 2; ++i, ++position) Fluxion_ASTC_PutBit(outBlock, position, ((FLUXION_ASTC_PARTITION_COUNT - 1u) >> i) & 1u);
    for (u32 i = 0; i < 4; ++i, ++position) Fluxion_ASTC_PutBit(outBlock, position, (FLUXION_ASTC_COLOR_ENDPOINT_MODE >> i) & 1u);

    // Both endpoints of one channel together, then the next channel --
    // the order the colour mode names them in, not both channels of one
    // endpoint.
    const u32 endpointValues[8] = {
        endpoint0[0], endpoint1[0],
        endpoint0[1], endpoint1[1],
        endpoint0[2], endpoint1[2],
        endpoint0[3], endpoint1[3],
    };
    for (u32 value = 0; value < 8; ++value)
    {
        for (u32 i = 0; i < 8; ++i, ++position) Fluxion_ASTC_PutBit(outBlock, position, (endpointValues[value] >> i) & 1u);
    }

    // --- The weights, from the top down ---------------------------------
    //
    // Weight data grows DOWNWARD from the last bit of the block while
    // everything else grows upward from the first, and the bits within it
    // are in the reverse order too. The gap between the two is unused.
    // This is not an implementation detail that could be chosen
    // differently: a decoder finds the weights by their distance from the
    // end, which is what lets the endpoint data be however long it needs
    // to be.
    for (u32 i = 0; i < FLUXION_ASTC_WEIGHT_COUNT; ++i)
    {
        for (u32 bit = 0; bit < FLUXION_ASTC_WEIGHT_BITS; ++bit)
        {
            const u32 streamBit = i * FLUXION_ASTC_WEIGHT_BITS + bit;
            Fluxion_ASTC_PutBit(outBlock, 127u - streamBit, (weights[i] >> bit) & 1u);
        }
    }
}

void Fluxion_ASTC_DecodeBlock(const u8 block[16], u8 outTexels[64])
{
    u32 position = 0;

    u32 blockMode = 0;
    for (u32 i = 0; i < 11; ++i, ++position) blockMode |= Fluxion_ASTC_GetBit(block, position) << i;

    u32 partitionsMinusOne = 0;
    for (u32 i = 0; i < 2; ++i, ++position) partitionsMinusOne |= Fluxion_ASTC_GetBit(block, position) << i;

    u32 colorEndpointMode = 0;
    for (u32 i = 0; i < 4; ++i, ++position) colorEndpointMode |= Fluxion_ASTC_GetBit(block, position) << i;

    // The one configuration written above, and nothing else. A block from
    // elsewhere would need the rest of the family, and producing a
    // plausible picture for one of those would be worse than producing
    // none -- a wrong picture that looks like a right one.
    if (blockMode != FLUXION_ASTC_BLOCK_MODE_4X4 || partitionsMinusOne != 0 || colorEndpointMode != FLUXION_ASTC_COLOR_ENDPOINT_MODE)
    {
        for (u32 i = 0; i < 64; ++i) outTexels[i] = 0;
        return;
    }

    u32 endpointValues[8];
    for (u32 value = 0; value < 8; ++value)
    {
        u32 read = 0;
        for (u32 i = 0; i < 8; ++i, ++position) read |= Fluxion_ASTC_GetBit(block, position) << i;
        endpointValues[value] = read;
    }

    const u32 endpoint0[4] = { endpointValues[0], endpointValues[2], endpointValues[4], endpointValues[6] };
    const u32 endpoint1[4] = { endpointValues[1], endpointValues[3], endpointValues[5], endpointValues[7] };

    for (u32 i = 0; i < FLUXION_ASTC_WEIGHT_COUNT; ++i)
    {
        u32 weight = 0;
        for (u32 bit = 0; bit < FLUXION_ASTC_WEIGHT_BITS; ++bit)
        {
            const u32 streamBit = i * FLUXION_ASTC_WEIGHT_BITS + bit;
            weight |= Fluxion_ASTC_GetBit(block, 127u - streamBit) << bit;
        }

        for (u32 channel = 0; channel < 4; ++channel)
        {
            outTexels[i * 4 + channel] = (u8)Fluxion_ASTC_Interpolate(endpoint0[channel], endpoint1[channel], kAstcWeightValues[weight]);
        }
    }
}
