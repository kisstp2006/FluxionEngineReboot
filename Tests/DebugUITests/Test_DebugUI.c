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

#include <Fluxion/DebugUI/DebugUI.h>
#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Log.h>

#include <string.h>

// A PANEL IS ONLY A PANEL IF IT ANSWERS.
//
// Everything about a user interface is easy to get wrong in a way that
// still looks right in a screenshot: the controls draw, the labels are
// spelled correctly, and clicking does nothing because the pointer never
// arrives or the box is somewhere other than where it was drawn.
//
// So what is checked here is the ANSWER: a click at a place where a
// checkbox is turns the caller's own variable over, and the same click a
// hundred pixels away does not. Nothing here reads a pixel -- what is
// being tested is the path from "the mouse is here and down" to "this
// bool changed", which is the whole of what a panel is for.

static bool DebugUI_Start(FluxionRHIBackendType backend, FluxionRHIInstanceHandle* outInstance, FluxionRHIDeviceHandle* outDevice)
{
    FluxionRHIInstanceDesc instanceDesc = { "DebugUITests", true };
    *outInstance = Fluxion_RHI_CreateInstance(backend, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(*outInstance)) return false;

    FluxionRHIAdapterHandle adapters[8];
    if (Fluxion_RHI_EnumerateAdapters(*outInstance, adapters, 8) == 0)
    {
        Fluxion_RHI_DestroyInstance(*outInstance);
        return false;
    }

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    *outDevice = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    if (!FLUXION_HANDLE_IS_VALID(*outDevice))
    {
        Fluxion_RHI_DestroyInstance(*outInstance);
        return false;
    }
    return true;
}

// One frame of a panel holding one checkbox, with the mouse where the
// caller says and the button in the state the caller says.
static bool DebugUI_FrameWithClick(bool* value, f32 mouseX, f32 mouseY, bool down)
{
    FluxionDebugUIInput input;
    memset(&input, 0, sizeof(input));
    input.mouseX = mouseX;
    input.mouseY = mouseY;
    input.mouseDown = down;

    Fluxion_DebugUI_BeginFrame(&input, 640, 480);

    bool changed = false;
    if (Fluxion_DebugUI_BeginPanel("Panel", 0.0f, 0.0f, 300.0f, 200.0f))
    {
        Fluxion_DebugUI_Row(30.0f, 1);
        changed = Fluxion_DebugUI_Checkbox("Switch", value);
    }
    Fluxion_DebugUI_EndPanel();
    Fluxion_DebugUI_EndFrame();
    return changed;
}

void Test_DebugUI_Run(TestContext* ctx)
{
    // ANY DEVICE WILL DO: what is being measured is arithmetic about where
    // the mouse is, and the device is needed only because a panel that
    // cannot be drawn is not worth asking questions of.
    static const FluxionRHIBackendType kBackends[] = { FLUXION_RHI_BACKEND_VULKAN, FLUXION_RHI_BACKEND_D3D12,
                                                       FLUXION_RHI_BACKEND_OPENGL };

    FluxionRHIInstanceHandle instance = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIDeviceHandle device = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    bool started = false;

    for (usize i = 0; i < sizeof(kBackends) / sizeof(kBackends[0]) && !started; ++i)
    {
        if (!Fluxion_RHI_IsBackendAvailable(kBackends[i])) continue;
        started = DebugUI_Start(kBackends[i], &instance, &device);
    }

    if (!started)
    {
        FLUXION_LOG_WARN("DebugUITests", "No device on this machine -- the panels were NOT asked anything.");
        return;
    }

    FluxionDebugUIDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.device = device;
    desc.queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    desc.colorFormat = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;

    if (!Fluxion_DebugUI_Init(&desc))
    {
        FLUXION_LOG_WARN("DebugUITests", "The panels could not be set up here -- nothing was asked of them.");
        Fluxion_RHI_DestroyDevice(device);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    TEST_CHECK(ctx, Fluxion_DebugUI_IsReady());

    // The checkbox sits in the first row of a panel at the top left
    // corner, below its title bar. A press and a release is what a click
    // IS -- one frame with the button down and one with it up -- because
    // a control that acted on the press alone would fire while the
    // pointer was still being dragged across it.
    bool value = false;
    const f32 checkboxX = 30.0f;
    const f32 checkboxY = 50.0f;

    // WHICH of the two frames reports the change is the toolkit's
    // business -- what a caller can rely on is that a click reports one,
    // and that the variable it was given now says the other thing.
    const bool changedOnPress = DebugUI_FrameWithClick(&value, checkboxX, checkboxY, true);
    const bool changedOnRelease = DebugUI_FrameWithClick(&value, checkboxX, checkboxY, false);
    const bool changed = changedOnPress || changedOnRelease;

    FLUXION_LOG_INFO("DebugUITests", "A click on the switch left it %s (it started off).", value ? "on" : "off");

    TEST_CHECK(ctx, changed);
    TEST_CHECK(ctx, value);

    // AND THE SAME CLICK SOMEWHERE ELSE DOES NOTHING, which is what says
    // the first one was about the control and not about the frame.
    bool other = false;
    const bool awayOnPress = DebugUI_FrameWithClick(&other, 600.0f, 400.0f, true);
    const bool awayOnRelease = DebugUI_FrameWithClick(&other, 600.0f, 400.0f, false);
    const bool changedAway = awayOnPress || awayOnRelease;

    TEST_CHECK(ctx, !changedAway);
    TEST_CHECK(ctx, !other);

    Fluxion_DebugUI_Shutdown();
    TEST_CHECK(ctx, !Fluxion_DebugUI_IsReady());

    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
