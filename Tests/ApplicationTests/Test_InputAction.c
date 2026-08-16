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
#include <Fluxion/Application/Input/InputAction.h>

// Uses mouse-button and gamepad-button bindings (not keyboard) so this
// test stays fully portable — no OS-specific key code needed to exercise
// FluxionInputBinding/registration/lookup logic itself.
void Test_InputAction_Run(TestContext* ctx)
{
    Fluxion_Input_Init();
    Fluxion_Input_BeginFrame();

    FluxionInputBinding bindings[2];
    bindings[0].type = FLUXION_INPUT_SOURCE_MOUSE_BUTTON;
    bindings[0].source.mouseButton = FLUXION_MOUSE_BUTTON_LEFT;
    bindings[1].type = FLUXION_INPUT_SOURCE_GAMEPAD_BUTTON;
    bindings[1].source.gamepadButton.gamepadIndex = 0;
    bindings[1].source.gamepadButton.button = FLUXION_GAMEPAD_BUTTON_A;

    TEST_CHECK(ctx, Fluxion_InputAction_Register("Fire", bindings, 2));
    TEST_CHECK(ctx, !Fluxion_InputAction_IsDown("Fire"));
    TEST_CHECK(ctx, !Fluxion_InputAction_IsDown("DoesNotExist"));

    FluxionEvent buttonDown;
    buttonDown.type = FLUXION_EVENT_MOUSE_BUTTON_DOWN;
    buttonDown.window.index = 0;
    buttonDown.window.generation = 1;
    buttonDown.data.mouseButton.button = FLUXION_MOUSE_BUTTON_LEFT;
    buttonDown.data.mouseButton.x = 0;
    buttonDown.data.mouseButton.y = 0;
    Fluxion_Input_ProcessEvent(&buttonDown);

    TEST_CHECK(ctx, Fluxion_InputAction_IsDown("Fire"));

    FluxionEvent buttonUp = buttonDown;
    buttonUp.type = FLUXION_EVENT_MOUSE_BUTTON_UP;
    Fluxion_Input_ProcessEvent(&buttonUp);
    TEST_CHECK(ctx, !Fluxion_InputAction_IsDown("Fire"));

    // Re-registering the same name replaces its bindings entirely.
    FluxionInputBinding singleBinding;
    singleBinding.type = FLUXION_INPUT_SOURCE_MOUSE_BUTTON;
    singleBinding.source.mouseButton = FLUXION_MOUSE_BUTTON_RIGHT;
    TEST_CHECK(ctx, Fluxion_InputAction_Register("Fire", &singleBinding, 1));

    Fluxion_Input_ProcessEvent(&buttonDown); // left button — no longer bound
    TEST_CHECK(ctx, !Fluxion_InputAction_IsDown("Fire"));

    Fluxion_Input_Shutdown();
}
