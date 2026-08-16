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

// The longest step the clock will ever report, until something says
// otherwise. A frame that really did take longer than this -- a debugger
// break, a window being dragged, a driver stall -- is reported as this
// instead, because a simulation handed the true figure moves everything
// it owns a quarter of a second or more in one step and comes apart:
// things pass through each other, integrators diverge, and none of it is
// recoverable afterwards. Running slow is the lesser wrong.
#define FLUXION_TIME_DEFAULT_MAXIMUM_DELTA 0.25f

void Fluxion_Time_Init(void);
void Fluxion_Time_Shutdown(void);

// Closes the frame that was running and opens the next one: everything
// below answers about the frame this call began, and answers the same
// thing every time it is asked until the next call. Nothing else advances
// the clock, so two parts of one frame asking how much time has passed
// are never told different numbers.
//
// The first call after Init reports no time at all, because there was no
// previous frame for any to have passed during.
void Fluxion_Time_BeginFrame(void);

// How long the previous frame took, clamped and then scaled -- this is
// what a simulation should step by.
f32 Fluxion_Time_GetDeltaTime(void);

// The same figure with the scale left out. This is what anything that
// must keep real time regardless of how fast the world is running should
// use: a menu animation, a stopwatch, a frame-rate readout.
f32 Fluxion_Time_GetUnscaledDeltaTime(void);

// Every delta so far, added up. Held as a double because a float stops
// being able to tell consecutive frames apart after a few hours of them.
f64 Fluxion_Time_GetElapsedTime(void);
f64 Fluxion_Time_GetUnscaledElapsedTime(void);

// How many frames have begun. Zero until the first Fluxion_Time_BeginFrame.
u64 Fluxion_Time_GetFrameCount(void);

// How fast the world runs relative to real time: 1 is real time, 0 stops
// it, 0.5 halves it. A negative figure is refused and taken as 0 -- time
// running backwards is not something the rest of the engine is written to
// survive.
void Fluxion_Time_SetTimeScale(f32 scale);
f32 Fluxion_Time_GetTimeScale(void);

// The ceiling described above. A negative figure is taken as 0, which
// stops the clock entirely rather than meaning anything stranger.
void Fluxion_Time_SetMaximumDeltaTime(f32 seconds);
f32 Fluxion_Time_GetMaximumDeltaTime(void);

#ifdef __cplusplus
}
#endif
