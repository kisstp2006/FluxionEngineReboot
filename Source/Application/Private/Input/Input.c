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

#include <Fluxion/Application/Input/Input.h>
#include <Fluxion/Application/Input/InputAction.h>

#include "InputPlatform.h"

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

static bool s_keyCurrent[FLUXION_KEY_COUNT];
static bool s_keyPrevious[FLUXION_KEY_COUNT];

static bool s_mouseCurrent[FLUXION_MOUSE_BUTTON_COUNT];
static bool s_mousePrevious[FLUXION_MOUSE_BUTTON_COUNT];

static i32 s_mouseX = 0;
static i32 s_mouseY = 0;
static i32 s_mouseDeltaX = 0; // accumulated since the last BeginFrame
static i32 s_mouseDeltaY = 0;
static f32 s_mouseScrollDelta = 0.0f;

static FluxionGamepadState s_gamepads[FLUXION_MAX_GAMEPADS];

typedef struct FluxionInputActionEntry
{
    char name[FLUXION_INPUT_ACTION_MAX_NAME_LENGTH + 1];
    FluxionInputBinding bindings[FLUXION_INPUT_ACTION_MAX_BINDINGS];
    u32 bindingCount;
} FluxionInputActionEntry;

static FluxionInputActionEntry s_actions[FLUXION_INPUT_ACTION_MAX_ACTIONS];
static u32 s_actionCount = 0;

static bool s_initialized = false;

void Fluxion_Input_Init(void)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_Input_Init called twice without a Shutdown in between");

    memset(s_keyCurrent, 0, sizeof(s_keyCurrent));
    memset(s_keyPrevious, 0, sizeof(s_keyPrevious));
    memset(s_mouseCurrent, 0, sizeof(s_mouseCurrent));
    memset(s_mousePrevious, 0, sizeof(s_mousePrevious));
    s_mouseX = 0;
    s_mouseY = 0;
    s_mouseDeltaX = 0;
    s_mouseDeltaY = 0;
    s_mouseScrollDelta = 0.0f;
    memset(s_gamepads, 0, sizeof(s_gamepads));
    s_actionCount = 0;

    Fluxion_Input_PlatformInit();

    s_initialized = true;
}

void Fluxion_Input_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_initialized, "Fluxion_Input_Shutdown called before Init");
    Fluxion_Input_PlatformShutdown();
    s_initialized = false;
}

void Fluxion_Input_BeginFrame(void)
{
    FLUXION_ASSERT(s_initialized);

    memcpy(s_keyPrevious, s_keyCurrent, sizeof(s_keyCurrent));
    memcpy(s_mousePrevious, s_mouseCurrent, sizeof(s_mouseCurrent));
    s_mouseDeltaX = 0;
    s_mouseDeltaY = 0;
    s_mouseScrollDelta = 0.0f;

    Fluxion_Input_PollGamepads(s_gamepads);
}

void Fluxion_Input_ProcessEvent(const FluxionEvent* event)
{
    FLUXION_ASSERT(s_initialized);
    FLUXION_ASSERT(event != NULL);

    switch (event->type)
    {
        case FLUXION_EVENT_KEY_DOWN:
        {
            FluxionKeyCode key = Fluxion_Input_TranslateKeyCode(event->data.key.keyCode);
            if (key != FLUXION_KEY_UNKNOWN)
            {
                s_keyCurrent[key] = true;
            }
            break;
        }

        case FLUXION_EVENT_KEY_UP:
        {
            FluxionKeyCode key = Fluxion_Input_TranslateKeyCode(event->data.key.keyCode);
            if (key != FLUXION_KEY_UNKNOWN)
            {
                s_keyCurrent[key] = false;
            }
            break;
        }

        case FLUXION_EVENT_MOUSE_MOVED:
        {
            i32 newX = event->data.mouseMoved.x;
            i32 newY = event->data.mouseMoved.y;
            s_mouseDeltaX += (newX - s_mouseX);
            s_mouseDeltaY += (newY - s_mouseY);
            s_mouseX = newX;
            s_mouseY = newY;
            break;
        }

        case FLUXION_EVENT_MOUSE_BUTTON_DOWN:
        {
            i32 button = event->data.mouseButton.button;
            if (button >= 0 && button < FLUXION_MOUSE_BUTTON_COUNT)
            {
                s_mouseCurrent[button] = true;
            }
            break;
        }

        case FLUXION_EVENT_MOUSE_BUTTON_UP:
        {
            i32 button = event->data.mouseButton.button;
            if (button >= 0 && button < FLUXION_MOUSE_BUTTON_COUNT)
            {
                s_mouseCurrent[button] = false;
            }
            break;
        }

        case FLUXION_EVENT_MOUSE_SCROLLED:
            s_mouseScrollDelta += event->data.mouseScroll.deltaY;
            break;

        default:
            break;
    }
}

bool Fluxion_Input_IsKeyDown(FluxionKeyCode key)
{
    FLUXION_ASSERT(key < FLUXION_KEY_COUNT);
    return s_keyCurrent[key];
}

bool Fluxion_Input_WasKeyPressed(FluxionKeyCode key)
{
    FLUXION_ASSERT(key < FLUXION_KEY_COUNT);
    return s_keyCurrent[key] && !s_keyPrevious[key];
}

bool Fluxion_Input_WasKeyReleased(FluxionKeyCode key)
{
    FLUXION_ASSERT(key < FLUXION_KEY_COUNT);
    return !s_keyCurrent[key] && s_keyPrevious[key];
}

void Fluxion_Input_GetMousePosition(i32* outX, i32* outY)
{
    if (outX) *outX = s_mouseX;
    if (outY) *outY = s_mouseY;
}

void Fluxion_Input_GetMouseDelta(i32* outDeltaX, i32* outDeltaY)
{
    if (outDeltaX) *outDeltaX = s_mouseDeltaX;
    if (outDeltaY) *outDeltaY = s_mouseDeltaY;
}

f32 Fluxion_Input_GetMouseScrollDelta(void)
{
    return s_mouseScrollDelta;
}

bool Fluxion_Input_IsMouseButtonDown(FluxionMouseButton button)
{
    FLUXION_ASSERT(button < FLUXION_MOUSE_BUTTON_COUNT);
    return s_mouseCurrent[button];
}

bool Fluxion_Input_WasMouseButtonPressed(FluxionMouseButton button)
{
    FLUXION_ASSERT(button < FLUXION_MOUSE_BUTTON_COUNT);
    return s_mouseCurrent[button] && !s_mousePrevious[button];
}

bool Fluxion_Input_WasMouseButtonReleased(FluxionMouseButton button)
{
    FLUXION_ASSERT(button < FLUXION_MOUSE_BUTTON_COUNT);
    return !s_mouseCurrent[button] && s_mousePrevious[button];
}

bool Fluxion_Input_GetGamepadState(u32 gamepadIndex, FluxionGamepadState* outState)
{
    if (gamepadIndex >= FLUXION_MAX_GAMEPADS)
    {
        return false;
    }
    *outState = s_gamepads[gamepadIndex];
    return true;
}

bool Fluxion_InputAction_Register(const char* name, const FluxionInputBinding* bindings, u32 bindingCount)
{
    FLUXION_ASSERT(s_initialized);
    FLUXION_ASSERT(name != NULL);

    if (bindingCount > FLUXION_INPUT_ACTION_MAX_BINDINGS)
    {
        return false;
    }

    usize nameLength = strlen(name);
    if (nameLength > FLUXION_INPUT_ACTION_MAX_NAME_LENGTH)
    {
        return false;
    }

    for (u32 i = 0; i < s_actionCount; ++i)
    {
        if (strcmp(s_actions[i].name, name) == 0)
        {
            memcpy(s_actions[i].bindings, bindings, bindingCount * sizeof(FluxionInputBinding));
            s_actions[i].bindingCount = bindingCount;
            return true;
        }
    }

    if (s_actionCount >= FLUXION_INPUT_ACTION_MAX_ACTIONS)
    {
        return false;
    }

    FluxionInputActionEntry* entry = &s_actions[s_actionCount++];
    memcpy(entry->name, name, nameLength);
    entry->name[nameLength] = '\0';
    memcpy(entry->bindings, bindings, bindingCount * sizeof(FluxionInputBinding));
    entry->bindingCount = bindingCount;
    return true;
}

static bool Fluxion_InputBinding_IsDown(const FluxionInputBinding* binding)
{
    switch (binding->type)
    {
        case FLUXION_INPUT_SOURCE_KEY:
            return Fluxion_Input_IsKeyDown(binding->source.key);

        case FLUXION_INPUT_SOURCE_MOUSE_BUTTON:
            return Fluxion_Input_IsMouseButtonDown(binding->source.mouseButton);

        case FLUXION_INPUT_SOURCE_GAMEPAD_BUTTON:
        {
            FluxionGamepadState state;
            if (!Fluxion_Input_GetGamepadState(binding->source.gamepadButton.gamepadIndex, &state) || !state.connected)
            {
                return false;
            }
            return state.buttons[binding->source.gamepadButton.button];
        }

        case FLUXION_INPUT_SOURCE_GAMEPAD_AXIS:
        {
            FluxionGamepadState state;
            if (!Fluxion_Input_GetGamepadState(binding->source.gamepadAxis.gamepadIndex, &state) || !state.connected)
            {
                return false;
            }
            f32 value = state.axes[binding->source.gamepadAxis.axis];
            f32 threshold = binding->source.gamepadAxis.threshold;
            return binding->source.gamepadAxis.positive ? (value >= threshold) : (value <= -threshold);
        }

        default:
            return false;
    }
}

bool Fluxion_InputAction_IsDown(const char* name)
{
    FLUXION_ASSERT(s_initialized);

    for (u32 i = 0; i < s_actionCount; ++i)
    {
        if (strcmp(s_actions[i].name, name) == 0)
        {
            for (u32 b = 0; b < s_actions[i].bindingCount; ++b)
            {
                if (Fluxion_InputBinding_IsDown(&s_actions[i].bindings[b]))
                {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}
