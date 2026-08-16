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

#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Handle.h>

void Test_Window_Run(TestContext* ctx)
{
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 64);
    Fluxion_WindowSystem_Init(NULL, &queue, 4);

    FluxionWindowDesc desc;
    desc.title = "Fluxion Window Test";
    desc.width = 320;
    desc.height = 240;
    desc.resizable = true;

    FluxionWindowHandle handle = Fluxion_Window_Create(&desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(handle));

    u32 width = 0, height = 0;
    Fluxion_Window_GetSize(handle, &width, &height);
    TEST_CHECK(ctx, width > 0 && height > 0);

    Fluxion_Window_SetTitle(handle, "Renamed");

    FluxionNativeWindowHandle native = Fluxion_Window_GetNativeHandle(handle);
    TEST_CHECK(ctx, native.value != NULL);

    TEST_CHECK(ctx, Fluxion_Window_IsFullscreen(handle) == false);

    Fluxion_WindowSystem_PollEvents(); // must not crash with a live window

    Fluxion_Window_Destroy(handle);

    // A destroyed handle must no longer resolve.
    FluxionNativeWindowHandle destroyedNative = Fluxion_Window_GetNativeHandle(handle);
    TEST_CHECK(ctx, destroyedNative.value == NULL);

    Fluxion_WindowSystem_PollEvents(); // must also be safe with zero live windows

    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
}
