#include "TestFramework.h"

#include <Fluxion/Foundation/Half.h>

#include <string.h>

// The sixteen-bit float.
//
// Most of these check bit patterns rather than values, because that is
// what the thing IS -- a value that comes back equal after a round trip
// says nothing about whether the bits in between were the ones a GPU will
// read.

static u32 FloatBits(f32 value)
{
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static bool IsNaN(f32 value)
{
    return value != value;
}

// Every half there is, put through both conversions.
//
// Sixty-five thousand of them is not many, and it means the two are
// checked against each other over the WHOLE domain rather than at the
// handful of values somebody thought of. A half that survives this cannot
// be one the pair disagrees about.
static void EveryHalfSurvivesGoingBothWays(TestContext* ctx)
{
    u32 mismatches = 0;

    for (u32 i = 0; i < 65536u; ++i)
    {
        const FluxionHalf half = (FluxionHalf)i;

        const u32 exponent = (i >> 10) & 0x1Fu;
        const u32 mantissa = i & 0x3FFu;

        const f32 value = Fluxion_Half_ToFloat(half);

        // A NaN is not equal to itself, so it is checked for what it is
        // rather than for what it converts back to.
        if (exponent == 0x1Fu && mantissa != 0u)
        {
            if (!IsNaN(value)) ++mismatches;
            continue;
        }

        // Every finite half is exactly representable as a float, so this
        // direction is lossless and the way back has nothing to round.
        if (Fluxion_Half_FromFloat(value) != half) ++mismatches;
    }

    TEST_CHECK(ctx, mismatches == 0);
}

static void TheOrdinaryValues(TestContext* ctx)
{
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(0.0f) == 0x0000u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(1.0f) == 0x3C00u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-1.0f) == 0xBC00u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(2.0f) == 0x4000u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(0.5f) == 0x3800u);

    // The largest finite half, and the smallest normal one.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(65504.0f) == 0x7BFFu);
    TEST_CHECK(ctx, Fluxion_Half_ToFloat(0x7BFFu) == 65504.0f);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(0.00006103515625f) == 0x0400u);

    // Negative zero keeps its sign. It compares equal to positive zero,
    // so the bits are what has to be looked at -- and a conversion that
    // dropped the sign here would be invisible to every comparison a
    // test could write about the value.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-0.0f) == 0x8000u);
    TEST_CHECK(ctx, FloatBits(Fluxion_Half_ToFloat(0x8000u)) == 0x80000000u);
}

static void SubnormalsAreNotFlushedToZero(TestContext* ctx)
{
    // The smallest subnormal half, and one step above it. A conversion
    // that gave up below the smallest NORMAL would return zero for both,
    // and a texture of very dim values would go black rather than dim.
    const f32 smallest = 5.9604644775390625e-8f;

    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallest) == 0x0001u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallest * 2.0f) == 0x0002u);
    TEST_CHECK(ctx, Fluxion_Half_ToFloat(0x0001u) == smallest);

    // Below half of the smallest there is nothing left to round to.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallest * 0.4f) == 0x0000u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-smallest * 0.4f) == 0x8000u);

    // And the boundary itself: a value just under the smallest normal
    // that rounds up has to land ON the smallest normal, which only
    // happens if the carry out of the mantissa is allowed to reach the
    // exponent. The subnormal path is the one where that is easy to get
    // wrong, because the shifting there is its own arithmetic.
    const f32 justUnderNormal = 0.000061005353927612304688f;
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(justUnderNormal) == 0x0400u);
}

static void TiesGoToEven(TestContext* ctx)
{
    // Exactly between two halves. Ties to even sends it to the one whose
    // last bit is zero, which is 0x3C00 below and 0x3C02 above -- a rule
    // that rounds ties upward instead would answer 0x3C01 for the first
    // of these.
    const f32 belowTie = 1.00048828125f;       // halfway between 0x3C00 and 0x3C01
    const f32 aboveTie = 1.0014648437500000f;  // halfway between 0x3C01 and 0x3C02

    TEST_CHECK(ctx, Fluxion_Half_FromFloat(belowTie) == 0x3C00u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(aboveTie) == 0x3C02u);

    // Just off the tie, either side, goes the obvious way -- so the
    // checks above are about the TIE and not about the neighbourhood.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(1.0006f) == 0x3C01u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(1.0003f) == 0x3C00u);
}

static void WhatOverflowsBecomesInfinity(TestContext* ctx)
{
    // Above the largest half, but nowhere near a float's own limit.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(70000.0f) == 0x7C00u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-70000.0f) == 0xFC00u);

    // The exact boundary. Anything at or above 65520 is closer to
    // infinity than to the largest finite half, and rounding it down to
    // 65504 would turn a number that overflowed into one that merely
    // happens to be large -- which is a lie a later division would carry.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(65520.0f) == 0x7C00u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(65519.0f) == 0x7BFFu);

    // A float's infinity stays one.
    f32 infinity;
    const u32 infinityBits = 0x7F800000u;
    memcpy(&infinity, &infinityBits, sizeof(infinity));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(infinity) == 0x7C00u);
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-infinity) == 0xFC00u);
}

static void NaNStaysNaN(TestContext* ctx)
{
    // A NaN whose payload lives entirely in the bits that get shifted
    // away. Truncating it would leave an all-zero mantissa with a full
    // exponent -- which is INFINITY, a perfectly ordinary number, and
    // nothing downstream would ever report it.
    f32 quiet;
    const u32 quietBits = 0x7F800001u;
    memcpy(&quiet, &quietBits, sizeof(quiet));

    const FluxionHalf half = Fluxion_Half_FromFloat(quiet);
    TEST_CHECK(ctx, ((half >> 10) & 0x1Fu) == 0x1Fu);
    TEST_CHECK(ctx, (half & 0x3FFu) != 0u);
    TEST_CHECK(ctx, IsNaN(Fluxion_Half_ToFloat(half)));
}

void Test_Half_Run(TestContext* ctx)
{
    EveryHalfSurvivesGoingBothWays(ctx);
    TheOrdinaryValues(ctx);
    SubnormalsAreNotFlushedToZero(ctx);
    TiesGoToEven(ctx);
    WhatOverflowsBecomesInfinity(ctx);
    NaNStaysNaN(ctx);
}
