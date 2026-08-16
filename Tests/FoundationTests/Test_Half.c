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

#include "TestFramework.h"

#include <Fluxion/Foundation/Half.h>

#include <string.h>

// The sixteen-bit float.
//
// Most of these check the BITS rather than the value, because that is what
// the thing is. A value that compares equal after a round trip says
// nothing about whether the bits in between were the ones a GPU will read.
//
// Nothing here writes a bit pattern out by hand. Every expected half is
// built by naming its three fields, so that reading a check does not
// require knowing the layout by heart.

#define HALF_MANTISSA_BITS 10
#define HALF_EXPONENT_BITS 5
#define HALF_EXPONENT_BIAS 15

#define HALF_MANTISSA_MASK ((1u << HALF_MANTISSA_BITS) - 1u)
#define HALF_EXPONENT_MASK ((1u << HALF_EXPONENT_BITS) - 1u)
#define HALF_SIGN_SHIFT    (HALF_MANTISSA_BITS + HALF_EXPONENT_BITS)

// The exponent field of all ones, which means infinity or not-a-number.
#define HALF_EXPONENT_INFINITE HALF_EXPONENT_MASK

// The exponent field of all zeros, which means the number is subnormal --
// smaller than the smallest one that can be written the ordinary way.
#define HALF_EXPONENT_SUBNORMAL 0

// The two ends of the ordinary range, once those two reserved fields are
// set aside.
#define HALF_EXPONENT_SMALLEST_NORMAL 1
#define HALF_EXPONENT_LARGEST_FINITE  (HALF_EXPONENT_INFINITE - 1u)

#define POSITIVE 0
#define NEGATIVE 1

// `power` is the power of two the number sits on: 0 for values in [1, 2),
// 1 for [2, 4), -1 for [0.5, 1). The field stored is that plus the bias.
static FluxionHalf Half(u32 sign, i32 power, u32 mantissa)
{
    const u32 exponent = (u32)(power + HALF_EXPONENT_BIAS);
    return (FluxionHalf)((sign << HALF_SIGN_SHIFT) | (exponent << HALF_MANTISSA_BITS) | mantissa);
}

// For the two exponent fields that are not a power of two at all.
static FluxionHalf HalfRaw(u32 sign, u32 exponent, u32 mantissa)
{
    return (FluxionHalf)((sign << HALF_SIGN_SHIFT) | (exponent << HALF_MANTISSA_BITS) | mantissa);
}

static FluxionHalf HalfInfinity(u32 sign) { return HalfRaw(sign, HALF_EXPONENT_INFINITE, 0); }
static FluxionHalf HalfZero(u32 sign) { return HalfRaw(sign, HALF_EXPONENT_SUBNORMAL, 0); }

static bool IsNaN(f32 value)
{
    return value != value;
}

static bool IsNegativeZero(f32 value)
{
    // Negative zero compares EQUAL to positive zero, so asking about the
    // value cannot tell them apart. Dividing by it can: one over it is
    // negative infinity, and one over positive zero is positive infinity.
    return value == 0.0f && (1.0f / value) < 0.0f;
}

static f32 SmallestSubnormalHalf(void)
{
    // The smallest number a half can hold at all: the subnormal exponent
    // with a single bit of mantissa.
    return Fluxion_Half_ToFloat(HalfRaw(POSITIVE, HALF_EXPONENT_SUBNORMAL, 1));
}

static f32 LargestFiniteHalf(void)
{
    // The largest exponent that is not the infinity one, with every
    // mantissa bit set.
    return Fluxion_Half_ToFloat(HalfRaw(POSITIVE, HALF_EXPONENT_LARGEST_FINITE, HALF_MANTISSA_MASK));
}

// Every half there is, put through both conversions.
//
// Sixty-five thousand is not many, and it means the two are checked
// against each other over the WHOLE domain rather than at the handful of
// values somebody thought of.
static void EveryHalfSurvivesGoingBothWays(TestContext* ctx)
{
    u32 mismatches = 0;

    for (u32 i = 0; i < 65536u; ++i)
    {
        const FluxionHalf half = (FluxionHalf)i;

        const u32 exponent = (i >> HALF_MANTISSA_BITS) & HALF_EXPONENT_MASK;
        const u32 mantissa = i & HALF_MANTISSA_MASK;

        const f32 value = Fluxion_Half_ToFloat(half);

        // Not a number is not equal to itself, so it is checked for what
        // it is rather than for what it converts back to.
        if (exponent == HALF_EXPONENT_INFINITE && mantissa != 0u)
        {
            if (!IsNaN(value)) ++mismatches;
            continue;
        }

        // Every finite half is exactly representable as a float, so this
        // direction loses nothing and the way back has nothing to round.
        if (Fluxion_Half_FromFloat(value) != half) ++mismatches;
    }

    TEST_CHECK(ctx, mismatches == 0);
}

static void TheOrdinaryValues(TestContext* ctx)
{
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(0.0f) == HalfZero(POSITIVE));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(1.0f) == Half(POSITIVE, 0, 0));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-1.0f) == Half(NEGATIVE, 0, 0));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(2.0f) == Half(POSITIVE, 1, 0));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(0.5f) == Half(POSITIVE, -1, 0));

    // One and a half: the exponent of one, and the mantissa exactly half
    // way to the next power of two.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(1.5f) == Half(POSITIVE, 0, 1u << (HALF_MANTISSA_BITS - 1)));

    TEST_CHECK(ctx, Fluxion_Half_FromFloat(LargestFiniteHalf()) == HalfRaw(POSITIVE, HALF_EXPONENT_LARGEST_FINITE,
                                                                          HALF_MANTISSA_MASK));

    // The smallest normal number: the lowest exponent that is not the
    // subnormal one, and an empty mantissa.
    const f32 smallestNormal = Fluxion_Half_ToFloat(HalfRaw(POSITIVE, HALF_EXPONENT_SMALLEST_NORMAL, 0));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallestNormal) == HalfRaw(POSITIVE, HALF_EXPONENT_SMALLEST_NORMAL, 0));

    // Negative zero keeps its sign. It compares equal to positive zero, so
    // a conversion that dropped the sign here would be invisible to every
    // check that only looked at the value.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-0.0f) == HalfZero(NEGATIVE));
    TEST_CHECK(ctx, IsNegativeZero(Fluxion_Half_ToFloat(HalfZero(NEGATIVE))));
}

static void SubnormalsAreNotFlushedToZero(TestContext* ctx)
{
    const f32 smallest = SmallestSubnormalHalf();

    // The smallest one, and one step above it. A conversion that gave up
    // below the smallest NORMAL number would answer zero for both, and a
    // texture of very dim values would go black rather than dim.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallest) == HalfRaw(POSITIVE, HALF_EXPONENT_SUBNORMAL, 1));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallest * 2.0f) == HalfRaw(POSITIVE, HALF_EXPONENT_SUBNORMAL, 2));

    // Below half of the smallest there is nothing left to round to.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallest * 0.4f) == HalfZero(POSITIVE));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-smallest * 0.4f) == HalfZero(NEGATIVE));

    // Exactly half of the smallest is a tie, and the even neighbour is
    // zero -- so it goes down, while anything the least bit above it goes
    // up.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallest * 0.5f) == HalfZero(POSITIVE));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(smallest * 0.75f) == HalfRaw(POSITIVE, HALF_EXPONENT_SUBNORMAL, 1));

    // And the boundary between the two paths: the largest subnormal,
    // rounded up, has to land on the smallest normal. That only happens
    // if the carry out of the mantissa reaches the exponent, and the
    // subnormal path is where that is easy to get wrong.
    const f32 largestSubnormal =
        Fluxion_Half_ToFloat(HalfRaw(POSITIVE, HALF_EXPONENT_SUBNORMAL, HALF_MANTISSA_MASK));
    const f32 justUnderNormal = largestSubnormal + smallest * 0.75f;

    TEST_CHECK(ctx, Fluxion_Half_FromFloat(justUnderNormal) == HalfRaw(POSITIVE, HALF_EXPONENT_SMALLEST_NORMAL, 0));
}

static void TiesGoToEven(TestContext* ctx)
{
    // One, and the two halves next to it. Their midpoints are ties, and
    // ties go to whichever neighbour has an even mantissa -- so the first
    // rounds DOWN to one and the second rounds UP to two steps above it.
    const f32 one = Fluxion_Half_ToFloat(Half(POSITIVE, 0, 0));
    const f32 oneStepUp = Fluxion_Half_ToFloat(Half(POSITIVE, 0, 1));
    const f32 twoStepsUp = Fluxion_Half_ToFloat(Half(POSITIVE, 0, 2));

    const f32 firstTie = (one + oneStepUp) * 0.5f;
    const f32 secondTie = (oneStepUp + twoStepsUp) * 0.5f;

    TEST_CHECK(ctx, Fluxion_Half_FromFloat(firstTie) == Half(POSITIVE, 0, 0));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(secondTie) == Half(POSITIVE, 0, 2));

    // Just off each tie, so that the two checks above are about the TIE
    // and not about the neighbourhood.
    const f32 step = oneStepUp - one;
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(firstTie + step * 0.25f) == Half(POSITIVE, 0, 1));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(firstTie - step * 0.25f) == Half(POSITIVE, 0, 0));
}

static void WhatOverflowsBecomesInfinity(TestContext* ctx)
{
    const f32 largest = LargestFiniteHalf();

    // Above the largest half, but nowhere near a float's own limit.
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(largest * 2.0f) == HalfInfinity(POSITIVE));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(largest * -2.0f) == HalfInfinity(NEGATIVE));

    // The boundary. Half way between the largest finite half and the next
    // value its exponent pattern would have described is a tie, and the
    // even neighbour there is infinity -- so it goes up. Rounding it down
    // instead would turn a number that overflowed into one that merely
    // happens to be large, which a later division would carry.
    const f32 secondLargest =
        Fluxion_Half_ToFloat(HalfRaw(POSITIVE, HALF_EXPONENT_LARGEST_FINITE, HALF_MANTISSA_MASK - 1u));
    const f32 lastStep = largest - secondLargest;

    TEST_CHECK(ctx, Fluxion_Half_FromFloat(largest + lastStep * 0.5f) == HalfInfinity(POSITIVE));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(largest + lastStep * 0.25f) == HalfRaw(POSITIVE, HALF_EXPONENT_LARGEST_FINITE,
                                                                                 HALF_MANTISSA_MASK));

    // A float's own infinity stays one. Reached by dividing rather than
    // by writing the bits out, and through a variable so the compiler has
    // to do it at run time rather than refusing the expression.
    f32 zero = 0.0f;
    const f32 infinity = 1.0f / zero;
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(infinity) == HalfInfinity(POSITIVE));
    TEST_CHECK(ctx, Fluxion_Half_FromFloat(-infinity) == HalfInfinity(NEGATIVE));
}

static void NotANumberStaysNotANumber(TestContext* ctx)
{
    // A float whose payload lives entirely in the bits that get dropped.
    // Truncating it would leave an empty mantissa with a full exponent --
    // which is INFINITY, a perfectly ordinary number, and nothing
    // downstream would ever report it.
    //
    // Built by hand rather than taken from 0.0f/0.0f, because the point is
    // precisely WHERE the payload bit sits.
    const u32 kFloatMantissaBits = 23;
    const u32 kFloatExponentInfinite = 255;
    const u32 lowestPayloadBit = 1u;

    const u32 bits = (kFloatExponentInfinite << kFloatMantissaBits) | lowestPayloadBit;
    f32 value;
    memcpy(&value, &bits, sizeof(value));

    const FluxionHalf half = Fluxion_Half_FromFloat(value);
    TEST_CHECK(ctx, ((half >> HALF_MANTISSA_BITS) & HALF_EXPONENT_MASK) == HALF_EXPONENT_INFINITE);
    TEST_CHECK(ctx, (half & (HALF_MANTISSA_MASK)) != 0u);
    TEST_CHECK(ctx, IsNaN(Fluxion_Half_ToFloat(half)));
}

void Test_Half_Run(TestContext* ctx)
{
    EveryHalfSurvivesGoingBothWays(ctx);
    TheOrdinaryValues(ctx);
    SubnormalsAreNotFlushedToZero(ctx);
    TiesGoToEven(ctx);
    WhatOverflowsBecomesInfinity(ctx);
    NotANumberStaysNotANumber(ctx);
}
