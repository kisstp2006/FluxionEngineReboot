#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// The sixteen-bit floating point number, and the two conversions.
//
// It is here rather than beside the first thing that needed it because
// there is nothing about it that belongs to any one of them. Anything
// that PRODUCES pixels above one has to write them somewhere, and the
// format that holds them is a half; anything that reads such a texture
// back has to undo it. Two implementations of a rounding rule would agree
// almost everywhere and disagree on the ties, and a texture one bit out
// in its bottom mantissa bit is not something anybody would look at and
// see.
//
// The bits, not a type. C has no half, and inventing a struct for one
// would mean a second thing that has to be kept the size it claims.
typedef u16 FluxionHalf;

// Nearest, ties to even -- the rule every other float operation on the
// machine already follows. Ties away from zero would be simpler to write
// and would drift upwards over a long chain of conversions.
//
// Values too large for a half become infinity rather than the largest
// finite half: a number that overflowed is not the same as a number that
// happened to be big, and clamping is a decision for whoever knows what
// the number means. Infinities and NaNs survive as themselves.
FluxionHalf Fluxion_Half_FromFloat(f32 value);

f32 Fluxion_Half_ToFloat(FluxionHalf value);

#ifdef __cplusplus
}
#endif
