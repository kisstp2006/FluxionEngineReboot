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
