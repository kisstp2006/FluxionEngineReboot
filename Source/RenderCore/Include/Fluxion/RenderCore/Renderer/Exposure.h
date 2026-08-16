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

// Working out an exposure the way a camera does. The renderer wants one
// multiplier; a person setting it by hand guesses again every time the
// lighting changes. A camera's three settings -- aperture, shutter,
// sensitivity -- are ones people already have intuitions about, and this
// is where they become the one number. Meaningful only because the
// lighting is in real units: a light's colour IS its intensity here.

// The exposure value for these camera settings, at a sensitivity of one
// hundred.
//
// Zero is a dim interior, fifteen is bright daylight, and each step of
// one halves the light that reaches the sensor.
//
// aperture is the f-number (1.4, 2.8, 16), shutterSeconds is how long the
// shutter is open (1/60 is 0.0167), sensitivity is the ISO (100, 400,
// 3200). Returns zero for a setting that is not a positive number, since
// none of the three can be.
f32 Fluxion_Exposure_EV100(f32 aperture, f32 shutterSeconds, f32 sensitivity);

// And the multiplier itself, which is what a render view is given.
//
// The 1.2 in it is the standard calibration between what a meter reads
// and the middle grey it should produce -- not a taste setting, and not
// somewhere to put one.
f32 Fluxion_Exposure_FromEV100(f32 ev100);

// The two above, together, which is what a caller with a camera actually
// wants.
f32 Fluxion_Exposure_FromCamera(f32 aperture, f32 shutterSeconds, f32 sensitivity);

#ifdef __cplusplus
}
#endif
