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

// Converting between the thirty-two-bit float the machine computes in and
// the sixteen-bit one a texture stores.
//
// Both formats are the same three fields in different widths -- a sign, an
// exponent, and a mantissa -- so everything below is written in terms of
// those three and never in terms of bit patterns. The layout is named once
// at the top; nothing further down repeats a number from it.
//
// Three cases have to be handled separately, and only one is the ordinary
// one:
//
//   too small   the exponent cannot go low enough, so the number is stored
//               SUBNORMAL: the leading one that is normally implied gets
//               written out and the mantissa shifts down to make room
//   ordinary    rebias the exponent, drop the mantissa bits that do not fit
//   too large   infinity
//
// Rounding is nearest, ties to even, in one place used by both paths.

#include <Fluxion/Foundation/Half.h>

#include <string.h>

// --- What each format is made of ----------------------------------------

#define FLUXION_FLOAT_MANTISSA_BITS 23
#define FLUXION_FLOAT_EXPONENT_BITS 8
#define FLUXION_FLOAT_EXPONENT_BIAS 127

#define FLUXION_HALF_MANTISSA_BITS 10
#define FLUXION_HALF_EXPONENT_BITS 5
#define FLUXION_HALF_EXPONENT_BIAS 15

// Everything below follows from the six numbers above.

#define FLUXION_FLOAT_SIGN_SHIFT     (FLUXION_FLOAT_MANTISSA_BITS + FLUXION_FLOAT_EXPONENT_BITS)
#define FLUXION_FLOAT_MANTISSA_MASK  ((1u << FLUXION_FLOAT_MANTISSA_BITS) - 1u)
#define FLUXION_FLOAT_EXPONENT_MASK  ((1u << FLUXION_FLOAT_EXPONENT_BITS) - 1u)

// The leading one a normal number does not store, because it is always
// there. Writing it out is what the subnormal paths below are doing.
#define FLUXION_FLOAT_IMPLICIT_ONE   (1u << FLUXION_FLOAT_MANTISSA_BITS)

#define FLUXION_HALF_SIGN_SHIFT      (FLUXION_HALF_MANTISSA_BITS + FLUXION_HALF_EXPONENT_BITS)
#define FLUXION_HALF_MANTISSA_MASK   ((1u << FLUXION_HALF_MANTISSA_BITS) - 1u)
#define FLUXION_HALF_EXPONENT_MASK   ((1u << FLUXION_HALF_EXPONENT_BITS) - 1u)
#define FLUXION_HALF_IMPLICIT_ONE    (1u << FLUXION_HALF_MANTISSA_BITS)

// An exponent field of all ones is not an exponent. It is how both formats
// say "infinity or not a number", and the mantissa says which.
#define FLUXION_FLOAT_EXPONENT_INFINITE FLUXION_FLOAT_EXPONENT_MASK
#define FLUXION_HALF_EXPONENT_INFINITE  FLUXION_HALF_EXPONENT_MASK

// How many mantissa bits a half cannot keep.
#define FLUXION_HALF_MANTISSA_BITS_LOST (FLUXION_FLOAT_MANTISSA_BITS - FLUXION_HALF_MANTISSA_BITS)

// The exponent range a half can actually write down, once the two reserved
// fields -- all zeros for subnormals, all ones for infinity -- are set
// aside. Anything below the first is stored subnormal or rounds to zero;
// anything above the second is infinity.
#define FLUXION_HALF_SMALLEST_NORMAL_EXPONENT (1 - FLUXION_HALF_EXPONENT_BIAS)
#define FLUXION_HALF_LARGEST_EXPONENT         ((i32)FLUXION_HALF_EXPONENT_MASK - 1 - FLUXION_HALF_EXPONENT_BIAS)

// --- Taking the three fields apart and putting them back together -------

static u32 Fluxion_Half_FloatToBits(f32 value)
{
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static f32 Fluxion_Half_FloatFromBits(u32 bits)
{
    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static u32 Fluxion_Half_MakeFloatBits(u32 sign, u32 exponent, u32 mantissa)
{
    return (sign << FLUXION_FLOAT_SIGN_SHIFT) | (exponent << FLUXION_FLOAT_MANTISSA_BITS) | mantissa;
}

static FluxionHalf Fluxion_Half_Make(u32 sign, u32 exponent, u32 mantissa)
{
    return (FluxionHalf)((sign << FLUXION_HALF_SIGN_SHIFT) | (exponent << FLUXION_HALF_MANTISSA_BITS) | mantissa);
}

// Drops the lowest `bitsToDrop` bits, rounding to the nearest survivor.
//
// A value exactly between two survivors goes to whichever of them is even.
// The alternative -- always rounding a tie upwards -- is easier to write
// and drifts upwards over a long chain of conversions, because every tie
// goes the same way. Ties to even is also the rule the machine's own float
// arithmetic follows, so a conversion here matches one done anywhere else.
static u32 Fluxion_Half_RoundToNearestEven(u32 value, u32 bitsToDrop)
{
    const u32 kept = value >> bitsToDrop;
    const u32 dropped = value & ((1u << bitsToDrop) - 1u);
    const u32 exactlyHalf = 1u << (bitsToDrop - 1u);

    if (dropped > exactlyHalf) return kept + 1u;
    if (dropped == exactlyHalf && (kept & 1u) != 0u) return kept + 1u;
    return kept;
}

// --- Float to half ------------------------------------------------------

FluxionHalf Fluxion_Half_FromFloat(f32 value)
{
    const u32 bits = Fluxion_Half_FloatToBits(value);

    const u32 sign = (bits >> FLUXION_FLOAT_SIGN_SHIFT) & 1u;
    const u32 rawExponent = (bits >> FLUXION_FLOAT_MANTISSA_BITS) & FLUXION_FLOAT_EXPONENT_MASK;
    const u32 mantissa = bits & FLUXION_FLOAT_MANTISSA_MASK;

    if (rawExponent == FLUXION_FLOAT_EXPONENT_INFINITE)
    {
        if (mantissa == 0) return Fluxion_Half_Make(sign, FLUXION_HALF_EXPONENT_INFINITE, 0);

        // Not a number. Its mantissa is a payload, and at least one bit of
        // it has to survive: a payload that lived only in the bits being
        // dropped would leave an empty mantissa, and an empty mantissa
        // with this exponent is INFINITY -- an ordinary number, which
        // nothing downstream would ever report.
        u32 payload = mantissa >> FLUXION_HALF_MANTISSA_BITS_LOST;
        if (payload == 0) payload = 1;
        return Fluxion_Half_Make(sign, FLUXION_HALF_EXPONENT_INFINITE, payload);
    }

    const i32 exponent = (i32)rawExponent - FLUXION_FLOAT_EXPONENT_BIAS;

    if (exponent < FLUXION_HALF_SMALLEST_NORMAL_EXPONENT)
    {
        // Below the smallest normal half. It may still be representable
        // subnormally: the leading one is written out and the whole
        // significand shifts down by however far the exponent could not
        // go.
        const u32 shortfall = (u32)(FLUXION_HALF_SMALLEST_NORMAL_EXPONENT - exponent);

        // Past this there is nothing left to round to, not even the
        // smallest subnormal -- and the shift below would run off the end
        // of the word.
        if (shortfall > FLUXION_HALF_MANTISSA_BITS + 1u) return Fluxion_Half_Make(sign, 0, 0);

        const u32 significand = mantissa | FLUXION_FLOAT_IMPLICIT_ONE;
        const u32 rounded = Fluxion_Half_RoundToNearestEven(significand, FLUXION_HALF_MANTISSA_BITS_LOST + shortfall);

        // Rounding up can carry a subnormal all the way to the smallest
        // NORMAL number, which is a different exponent and an empty
        // mantissa. Said out rather than left to the two fields being
        // next to each other in the word.
        if (rounded > FLUXION_HALF_MANTISSA_MASK) return Fluxion_Half_Make(sign, 1, 0);
        return Fluxion_Half_Make(sign, 0, rounded);
    }

    // Too large for a half before rounding is even considered. Infinity
    // rather than the largest finite half: a number that overflowed is not
    // the same as one that happens to be big, and clamping is a decision
    // for whoever knows what the number means.
    if (exponent > FLUXION_HALF_LARGEST_EXPONENT) return Fluxion_Half_Make(sign, FLUXION_HALF_EXPONENT_INFINITE, 0);

    u32 halfExponent = (u32)(exponent + FLUXION_HALF_EXPONENT_BIAS);
    u32 halfMantissa = Fluxion_Half_RoundToNearestEven(mantissa, FLUXION_HALF_MANTISSA_BITS_LOST);

    if (halfMantissa > FLUXION_HALF_MANTISSA_MASK)
    {
        // The rounding carried out of the top of the mantissa, which means
        // the number doubled: empty mantissa, one higher exponent.
        halfMantissa = 0;
        halfExponent += 1u;

        if (halfExponent >= FLUXION_HALF_EXPONENT_INFINITE)
            return Fluxion_Half_Make(sign, FLUXION_HALF_EXPONENT_INFINITE, 0);
    }

    return Fluxion_Half_Make(sign, halfExponent, halfMantissa);
}

// --- Half to float ------------------------------------------------------

f32 Fluxion_Half_ToFloat(FluxionHalf value)
{
    const u32 sign = ((u32)value >> FLUXION_HALF_SIGN_SHIFT) & 1u;
    const u32 exponent = ((u32)value >> FLUXION_HALF_MANTISSA_BITS) & FLUXION_HALF_EXPONENT_MASK;
    const u32 mantissa = (u32)value & FLUXION_HALF_MANTISSA_MASK;

    if (exponent == FLUXION_HALF_EXPONENT_INFINITE)
    {
        // Infinity when the mantissa is empty, not a number when it is
        // not -- and shifting the payload up keeps it non-empty either
        // way, so the two need no separate treatment here.
        return Fluxion_Half_FloatFromBits(Fluxion_Half_MakeFloatBits(
            sign, FLUXION_FLOAT_EXPONENT_INFINITE, mantissa << FLUXION_HALF_MANTISSA_BITS_LOST));
    }

    if (exponent == 0)
    {
        if (mantissa == 0) return Fluxion_Half_FloatFromBits(Fluxion_Half_MakeFloatBits(sign, 0, 0));

        // Subnormal as a half, which a float has room to hold normally.
        // Shift the leading one up to where the implied bit belongs, and
        // lower the exponent by as far as it travelled.
        u32 significand = mantissa;
        i32 significandExponent = FLUXION_HALF_SMALLEST_NORMAL_EXPONENT;
        while ((significand & FLUXION_HALF_IMPLICIT_ONE) == 0u)
        {
            significand <<= 1;
            --significandExponent;
        }
        significand &= FLUXION_HALF_MANTISSA_MASK;

        return Fluxion_Half_FloatFromBits(Fluxion_Half_MakeFloatBits(
            sign, (u32)(significandExponent + FLUXION_FLOAT_EXPONENT_BIAS),
            significand << FLUXION_HALF_MANTISSA_BITS_LOST));
    }

    const u32 floatExponent = exponent - FLUXION_HALF_EXPONENT_BIAS + FLUXION_FLOAT_EXPONENT_BIAS;
    return Fluxion_Half_FloatFromBits(
        Fluxion_Half_MakeFloatBits(sign, floatExponent, mantissa << FLUXION_HALF_MANTISSA_BITS_LOST));
}
