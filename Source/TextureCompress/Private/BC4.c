// BC4: one channel in eight bytes a block.
//
// Two endpoint bytes and sixteen three-bit indices into a palette built
// from them. Which palette depends on which endpoint is larger, and that
// is not a detail: one arrangement spends all eight entries between the
// endpoints, the other spends six and reserves the last two for exact
// zero and exact one. A block holding a mask that is mostly fully on and
// fully off is much better served by the second, and choosing between
// them costs almost nothing -- so both are tried here and the better one
// kept.
//
// BC5 is two of these blocks, red then green, which is why it lives in
// this file.

#include "BlockCompressInternal.h"

// The palette an encoded pair of endpoints produces. Written once and
// used by both the encoder and the decoder, so the two cannot come to
// different conclusions about what a block means.
static void Fluxion_BC4_BuildPalette(u32 red0, u32 red1, u32 outPalette[8])
{
    outPalette[0] = red0;
    outPalette[1] = red1;

    // Rounded, not truncated. The format describes its interpolants as
    // exact fractions and leaves the rounding to whoever implements it;
    // hardware rounds to nearest, and a decoder here that threw the
    // remainder away would sit one short of the driver for about half the
    // values -- close enough to look right and far enough to fail an
    // exact comparison against it.
    if (red0 > red1)
    {
        // Eight values spread across the whole span.
        for (u32 i = 2; i < 8; ++i)
        {
            outPalette[i] = ((8u - i) * red0 + (i - 1u) * red1 + 3u) / 7u;
        }
        return;
    }

    // Six values, plus an exact zero and an exact one.
    for (u32 i = 2; i < 6; ++i)
    {
        outPalette[i] = ((6u - i) * red0 + (i - 1u) * red1 + 2u) / 5u;
    }
    outPalette[6] = 0;
    outPalette[7] = 255;
}

static u32 Fluxion_BC4_ChoosePalette(const u32 palette[8], u32 value, u32* outError)
{
    u32 best = 0;
    u32 bestError = 0xFFFFFFFFu;

    for (u32 i = 0; i < 8; ++i)
    {
        const i32 difference = (i32)palette[i] - (i32)value;
        const u32 error = (u32)(difference * difference);
        if (error < bestError)
        {
            bestError = error;
            best = i;
        }
    }

    *outError = bestError;
    return best;
}

static u32 Fluxion_BC4_TotalError(const u8 values[16], u32 red0, u32 red1)
{
    u32 palette[8];
    Fluxion_BC4_BuildPalette(red0, red1, palette);

    u32 total = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        u32 error = 0;
        Fluxion_BC4_ChoosePalette(palette, values[i], &error);
        total += error;
    }
    return total;
}

void Fluxion_BC4_EncodeBlock(const u8 values[16], u8 outBlock[8])
{
    u32 lowest = 255;
    u32 highest = 0;

    // The same again, but blind to the two values the second arrangement
    // can reproduce exactly for free.
    u32 innerLowest = 255;
    u32 innerHighest = 0;
    bool anyInner = false;

    for (u32 i = 0; i < 16; ++i)
    {
        const u32 value = values[i];
        if (value < lowest) lowest = value;
        if (value > highest) highest = value;

        if (value != 0 && value != 255)
        {
            if (value < innerLowest) innerLowest = value;
            if (value > innerHighest) innerHighest = value;
            anyInner = true;
        }
    }

    if (!anyInner)
    {
        innerLowest = 0;
        innerHighest = 0;
    }

    // The larger endpoint first selects the eight-value arrangement, the
    // smaller first selects the six-plus-two one. That is the whole of the
    // choice, and it is made by which byte goes where.
    u32 red0 = highest;
    u32 red1 = lowest;

    if (Fluxion_BC4_TotalError(values, innerLowest, innerHighest) < Fluxion_BC4_TotalError(values, red0, red1))
    {
        red0 = innerLowest;
        red1 = innerHighest;
    }

    u32 palette[8];
    Fluxion_BC4_BuildPalette(red0, red1, palette);

    FluxionBlockBits bits;
    Fluxion_BlockBits_Init(&bits);
    Fluxion_BlockBits_Put(&bits, red0, 8);
    Fluxion_BlockBits_Put(&bits, red1, 8);

    for (u32 i = 0; i < 16; ++i)
    {
        u32 error = 0;
        Fluxion_BlockBits_Put(&bits, Fluxion_BC4_ChoosePalette(palette, values[i], &error), 3);
    }

    for (u32 i = 0; i < 8; ++i) outBlock[i] = bits.bytes[i];
}

void Fluxion_BC4_DecodeBlock(const u8 block[8], u8 outValues[16])
{
    FluxionBlockBits bits;
    Fluxion_BlockBits_Init(&bits);
    for (u32 i = 0; i < 8; ++i) bits.bytes[i] = block[i];

    const u32 red0 = Fluxion_BlockBits_Get(&bits, 8);
    const u32 red1 = Fluxion_BlockBits_Get(&bits, 8);

    u32 palette[8];
    Fluxion_BC4_BuildPalette(red0, red1, palette);

    for (u32 i = 0; i < 16; ++i)
    {
        outValues[i] = (u8)palette[Fluxion_BlockBits_Get(&bits, 3)];
    }
}
