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

#include <Fluxion/Application/Input/Gamepad.h>
#include <Fluxion/Application/Input/KeyCode.h>
#include <Fluxion/Application/Input/MouseButton.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLUXION_INPUT_ACTION_MAX_ACTIONS      32
#define FLUXION_INPUT_ACTION_MAX_BINDINGS     4
#define FLUXION_INPUT_ACTION_MAX_NAME_LENGTH  31

typedef enum FluxionInputSourceType
{
    FLUXION_INPUT_SOURCE_KEY = 0,
    FLUXION_INPUT_SOURCE_MOUSE_BUTTON,
    FLUXION_INPUT_SOURCE_GAMEPAD_BUTTON,
    FLUXION_INPUT_SOURCE_GAMEPAD_AXIS,
} FluxionInputSourceType;

typedef struct FluxionInputBinding
{
    FluxionInputSourceType type;
    union
    {
        FluxionKeyCode key;
        FluxionMouseButton mouseButton;
        struct
        {
            u32 gamepadIndex;
            FluxionGamepadButton button;
        } gamepadButton;
        struct
        {
            u32 gamepadIndex;
            FluxionGamepadAxis axis;
            f32 threshold;  // e.g. 0.5
            bool positive;  // true: axis >= threshold; false: axis <= -threshold
        } gamepadAxis;
    } source;
} FluxionInputBinding;

// Registers (or replaces, if `name` already exists) an action considered
// "down" when ANY of its bound physical inputs is down. Returns false if
// the action table is full (FLUXION_INPUT_ACTION_MAX_ACTIONS), the name
// is too long, or bindingCount exceeds FLUXION_INPUT_ACTION_MAX_BINDINGS.
bool Fluxion_InputAction_Register(const char* name, const FluxionInputBinding* bindings, u32 bindingCount);

bool Fluxion_InputAction_IsDown(const char* name);

#ifdef __cplusplus
}
#endif
