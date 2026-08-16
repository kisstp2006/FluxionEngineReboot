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

// Manual test tool, not an automated test: opens a window and prints the
// live keyboard/mouse/gamepad state to the console every frame. Run it
// yourself and press keys / move the mouse / use a gamepad to sanity-check
// Input against real hardware and human feel -- the one thing the
// automated ApplicationTests can't do without a physical controller.
#include <Fluxion/Application/Events/EventQueue.h>
#include <Fluxion/Application/Input/Input.h>
#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Platform/Time.h>

#include <stdio.h>

typedef struct FluxionWatchedKey
{
    FluxionKeyCode key;
    const char* name;
} FluxionWatchedKey;

static const FluxionWatchedKey s_watchedKeys[] =
{
    { FLUXION_KEY_W,     "W" },
    { FLUXION_KEY_A,     "A" },
    { FLUXION_KEY_S,     "S" },
    { FLUXION_KEY_D,     "D" },
    { FLUXION_KEY_SPACE, "Space" },
    { FLUXION_KEY_UP,    "Up" },
    { FLUXION_KEY_DOWN,  "Down" },
    { FLUXION_KEY_LEFT,  "Left" },
    { FLUXION_KEY_RIGHT, "Right" },
};

int main(void)
{
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 256);
    Fluxion_WindowSystem_Init(NULL, &queue, 1);
    Fluxion_Input_Init();

    FluxionWindowDesc desc;
    desc.title = "Fluxion InputDemo (Escape or close the window to quit)";
    desc.width = 640;
    desc.height = 480;
    desc.resizable = true;
    FluxionWindowHandle window = Fluxion_Window_Create(&desc);

    FLUXION_LOG_INFO("InputDemo", "Window created. Press keys, move the mouse, or use a gamepad.");

    bool running = true;
    while (running)
    {
        Fluxion_Input_BeginFrame();
        Fluxion_WindowSystem_PollEvents();

        FluxionEvent event;
        while (Fluxion_EventQueue_Pop(&queue, &event))
        {
            Fluxion_Input_ProcessEvent(&event);
            if (event.type == FLUXION_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                running = false;
            }
        }

        if (Fluxion_Input_IsKeyDown(FLUXION_KEY_ESCAPE))
        {
            running = false;
        }

        i32 mouseX = 0, mouseY = 0;
        Fluxion_Input_GetMousePosition(&mouseX, &mouseY);

        printf("\rKeys: ");
        for (usize i = 0; i < FLUXION_ARRAY_COUNT(s_watchedKeys); ++i)
        {
            if (Fluxion_Input_IsKeyDown(s_watchedKeys[i].key))
            {
                printf("%s ", s_watchedKeys[i].name);
            }
        }

        printf("| Mouse (%4d,%4d) L=%d R=%d M=%d Scroll=%.1f ",
            mouseX, mouseY,
            Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT),
            Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_RIGHT),
            Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_MIDDLE),
            (double)Fluxion_Input_GetMouseScrollDelta());

        for (u32 i = 0; i < FLUXION_MAX_GAMEPADS; ++i)
        {
            FluxionGamepadState gamepad;
            Fluxion_Input_GetGamepadState(i, &gamepad);
            if (gamepad.connected)
            {
                printf("| Gamepad%u A=%d B=%d X=%d Y=%d LX=%.2f LY=%.2f RT=%.2f ",
                    i,
                    gamepad.buttons[FLUXION_GAMEPAD_BUTTON_A],
                    gamepad.buttons[FLUXION_GAMEPAD_BUTTON_B],
                    gamepad.buttons[FLUXION_GAMEPAD_BUTTON_X],
                    gamepad.buttons[FLUXION_GAMEPAD_BUTTON_Y],
                    (double)gamepad.axes[FLUXION_GAMEPAD_AXIS_LEFT_X],
                    (double)gamepad.axes[FLUXION_GAMEPAD_AXIS_LEFT_Y],
                    (double)gamepad.axes[FLUXION_GAMEPAD_AXIS_RIGHT_TRIGGER]);
            }
        }

        printf("          "); // pad over any leftover characters from a longer previous line
        fflush(stdout);

        Fluxion_Platform_SleepMilliseconds(33); // ~30 Hz refresh, gentle on the console
    }

    printf("\n");
    FLUXION_LOG_INFO("InputDemo", "Closing.");

    Fluxion_Window_Destroy(window);
    Fluxion_Input_Shutdown();
    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
    return 0;
}
