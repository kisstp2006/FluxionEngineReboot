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

#include <Fluxion/Application/Events/Event.h>
#include <Fluxion/Application/Input/Gamepad.h>
#include <Fluxion/Application/Input/KeyCode.h>
#include <Fluxion/Application/Input/MouseButton.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

void Fluxion_Input_Init(void);
void Fluxion_Input_Shutdown(void);

// Call once per frame, before draining the event queue: resets the
// "pressed/released this frame" transient state and polls connected
// gamepads (gamepads are polled, not event-driven, unlike keyboard/mouse).
void Fluxion_Input_BeginFrame(void);

// Call for every event popped from a FluxionEventQueue this frame,
// alongside whatever else the caller does with that same event (e.g.
// handling FLUXION_EVENT_WINDOW_CLOSE_REQUESTED itself). Events Input
// doesn't care about are ignored.
void Fluxion_Input_ProcessEvent(const FluxionEvent* event);

bool Fluxion_Input_IsKeyDown(FluxionKeyCode key);
bool Fluxion_Input_WasKeyPressed(FluxionKeyCode key);
bool Fluxion_Input_WasKeyReleased(FluxionKeyCode key);

void Fluxion_Input_GetMousePosition(i32* outX, i32* outY);
void Fluxion_Input_GetMouseDelta(i32* outDeltaX, i32* outDeltaY);
f32  Fluxion_Input_GetMouseScrollDelta(void);

bool Fluxion_Input_IsMouseButtonDown(FluxionMouseButton button);
bool Fluxion_Input_WasMouseButtonPressed(FluxionMouseButton button);
bool Fluxion_Input_WasMouseButtonReleased(FluxionMouseButton button);

// Returns false (and leaves *outState untouched) if gamepadIndex is out
// of range ([0, FLUXION_MAX_GAMEPADS)).
bool Fluxion_Input_GetGamepadState(u32 gamepadIndex, FluxionGamepadState* outState);

#ifdef __cplusplus
}
#endif
