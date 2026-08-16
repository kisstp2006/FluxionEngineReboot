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

#include <Fluxion/Application/Input/Input.h>

// Whether any slot reports connected depends on what the host exposes at
// /dev/input (Linux) or via XInput (Windows) -- some CI runner images
// expose a virtual joystick device even with no physical hardware
// plugged in, so this can't assert "nothing is ever connected" as a
// build-machine guarantee. What it does verify: the query itself is
// well-formed for every valid slot (doesn't crash, rejects an
// out-of-range index), and that a slot reporting NOT connected reports
// an all-zero state rather than stale/garbage data.
void Test_Input_Gamepad_Run(TestContext* ctx)
{
    Fluxion_Input_Init();
    Fluxion_Input_BeginFrame(); // polls gamepads

    for (u32 i = 0; i < FLUXION_MAX_GAMEPADS; ++i)
    {
        FluxionGamepadState state;
        TEST_CHECK(ctx, Fluxion_Input_GetGamepadState(i, &state));

        if (!state.connected)
        {
            for (u32 axis = 0; axis < FLUXION_GAMEPAD_AXIS_COUNT; ++axis)
            {
                TEST_CHECK(ctx, state.axes[axis] == 0.0f);
            }
            for (u32 button = 0; button < FLUXION_GAMEPAD_BUTTON_COUNT; ++button)
            {
                TEST_CHECK(ctx, state.buttons[button] == false);
            }
        }
    }

    FluxionGamepadState outOfRange;
    TEST_CHECK(ctx, Fluxion_Input_GetGamepadState(FLUXION_MAX_GAMEPADS, &outOfRange) == false);

    Fluxion_Input_Shutdown();
}
