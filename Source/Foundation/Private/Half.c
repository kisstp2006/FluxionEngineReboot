// Converting between the thirty-two-bit float the machine computes in and
// the sixteen-bit one a texture stores.
//
// Written from the layout in IEEE 754-2008: sign, five exponent bits with
// a bias of fifteen, ten mantissa bits. Three ranges have to be handled
// separately, and only one of them is the ordinary case:
//
//   too small   the exponent cannot go low enough, so the number is
//               stored SUBNORMAL -- the implicit leading one is written
//               out and the mantissa shifted down, which is why this
//               branch cannot reuse the arithmetic below
//   ordinary    rebias the exponent, drop thirteen mantissa bits
//   too large   infinity, and the exponent field is already all ones for
//               a float's own infinities and NaNs
//
// Rounding is nearest-ties-to-even throughout, and it is the same three
// lines in both the subnormal and the ordinary path, which is what makes
// the boundary between them safe: a value just under the smallest normal
// that rounds UP lands exactly on the smallest normal, and it gets there
// because the carry runs out of the mantissa and into the exponent on its
// own.

#include <Fluxion/Foundation/Half.h>

#include <string.h>

FluxionHalf Fluxion_Half_FromFloat(f32 value)
{
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));

    const u32 sign = (bits >> 16) & 0x8000u;
    const i32 exponent = (i32)((bits >> 23) & 0xFFu);
    const u32 mantissa = bits & 0x7FFFFFu;

    // Infinity and NaN. A NaN must stay a NaN, and its payload has to
    // keep at least one bit set: shifting a payload that lives entirely
    // in the low bits would turn it into an infinity, which is a number.
    if (exponent == 0xFF)
    {
        if (mantissa == 0) return (FluxionHalf)(sign | 0x7C00u);
        return (FluxionHalf)(sign | 0x7C00u | (mantissa >> 13) | 1u);
    }

    const i32 unbiased = exponent - 127;

    // Smaller than the smallest subnormal half, by more than half of it:
    // zero, with its sign kept.
    if (unbiased < -25) return (FluxionHalf)sign;

    if (unbiased < -14)
    {
        // Subnormal. The implicit one comes back, and the whole thing
        // shifts down by however far the exponent could not go.
        const u32 shift = (u32)(-unbiased - 14);
        const u32 full = mantissa | 0x800000u;

        const u32 keep = 13u + shift;
        const u32 result = full >> keep;
        const u32 remainder = full & ((1u << keep) - 1u);
        const u32 halfway = 1u << (keep - 1u);

        // Ties to even: exactly halfway goes to whichever neighbour has a
        // zero in its last bit.
        if (remainder > halfway || (remainder == halfway && (result & 1u) != 0u))
            return (FluxionHalf)(sign | (result + 1u));

        return (FluxionHalf)(sign | result);
    }

    // Larger than a half can hold, before rounding is even considered.
    if (unbiased > 15) return (FluxionHalf)(sign | 0x7C00u);

    u32 result = ((u32)(unbiased + 15) << 10) | (mantissa >> 13);
    const u32 remainder = mantissa & 0x1FFFu;

    // The carry, if there is one, runs from the mantissa straight into
    // the exponent because the two fields sit next to each other -- which
    // is also how a value just below infinity becomes infinity rather
    // than wrapping round to zero.
    if (remainder > 0x1000u || (remainder == 0x1000u && (result & 1u) != 0u)) result += 1u;

    return (FluxionHalf)(sign | result);
}

f32 Fluxion_Half_ToFloat(FluxionHalf value)
{
    const u32 sign = ((u32)value & 0x8000u) << 16;
    const u32 exponent = ((u32)value >> 10) & 0x1Fu;
    const u32 mantissa = (u32)value & 0x3FFu;

    u32 bits;

    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            bits = sign; // zero, either sign of it
        }
        else
        {
            // Subnormal, which a float has room to hold normally. Shift
            // the leading one up until it is where the implicit bit
            // belongs, and lower the exponent by as far as it moved.
            u32 shifted = mantissa;
            i32 unbiased = -14;
            while ((shifted & 0x400u) == 0u)
            {
                shifted <<= 1;
                --unbiased;
            }
            shifted &= 0x3FFu;
            bits = sign | ((u32)(unbiased + 127) << 23) | (shifted << 13);
        }
    }
    else if (exponent == 0x1F)
    {
        bits = sign | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    }

    f32 result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}
