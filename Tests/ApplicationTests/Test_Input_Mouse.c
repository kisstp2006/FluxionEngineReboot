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

void Test_Input_Mouse_Run(TestContext* ctx)
{
    Fluxion_Input_Init();
    Fluxion_Input_BeginFrame();

    FluxionEvent moveEvent;
    moveEvent.type = FLUXION_EVENT_MOUSE_MOVED;
    moveEvent.window.index = 0;
    moveEvent.window.generation = 1;
    moveEvent.data.mouseMoved.x = 100;
    moveEvent.data.mouseMoved.y = 50;
    Fluxion_Input_ProcessEvent(&moveEvent);

    i32 x = 0, y = 0;
    Fluxion_Input_GetMousePosition(&x, &y);
    TEST_CHECK(ctx, x == 100 && y == 50);

    i32 dx = 0, dy = 0;
    Fluxion_Input_GetMouseDelta(&dx, &dy);
    TEST_CHECK(ctx, dx == 100 && dy == 50); // moved from (0,0) since Init

    moveEvent.data.mouseMoved.x = 110;
    moveEvent.data.mouseMoved.y = 45;
    Fluxion_Input_ProcessEvent(&moveEvent);
    Fluxion_Input_GetMouseDelta(&dx, &dy);
    TEST_CHECK(ctx, dx == 110 && dy == 45); // still accumulating since the last BeginFrame

    Fluxion_Input_BeginFrame(); // resets the accumulated delta
    Fluxion_Input_GetMouseDelta(&dx, &dy);
    TEST_CHECK(ctx, dx == 0 && dy == 0);

    TEST_CHECK(ctx, !Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT));

    FluxionEvent buttonDown;
    buttonDown.type = FLUXION_EVENT_MOUSE_BUTTON_DOWN;
    buttonDown.window.index = 0;
    buttonDown.window.generation = 1;
    buttonDown.data.mouseButton.button = FLUXION_MOUSE_BUTTON_LEFT;
    buttonDown.data.mouseButton.x = 110;
    buttonDown.data.mouseButton.y = 45;
    Fluxion_Input_ProcessEvent(&buttonDown);

    TEST_CHECK(ctx, Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT));
    TEST_CHECK(ctx, Fluxion_Input_WasMouseButtonPressed(FLUXION_MOUSE_BUTTON_LEFT));

    Fluxion_Input_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT));
    TEST_CHECK(ctx, !Fluxion_Input_WasMouseButtonPressed(FLUXION_MOUSE_BUTTON_LEFT));

    FluxionEvent buttonUp = buttonDown;
    buttonUp.type = FLUXION_EVENT_MOUSE_BUTTON_UP;
    Fluxion_Input_ProcessEvent(&buttonUp);
    TEST_CHECK(ctx, !Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT));
    TEST_CHECK(ctx, Fluxion_Input_WasMouseButtonReleased(FLUXION_MOUSE_BUTTON_LEFT));

    TEST_CHECK(ctx, Fluxion_Input_GetMouseScrollDelta() == 0.0f);

    FluxionEvent scroll;
    scroll.type = FLUXION_EVENT_MOUSE_SCROLLED;
    scroll.window.index = 0;
    scroll.window.generation = 1;
    scroll.data.mouseScroll.deltaX = 0.0f;
    scroll.data.mouseScroll.deltaY = 1.0f;
    Fluxion_Input_ProcessEvent(&scroll);
    TEST_CHECK(ctx, Fluxion_Input_GetMouseScrollDelta() == 1.0f);

    Fluxion_Input_Shutdown();
}
