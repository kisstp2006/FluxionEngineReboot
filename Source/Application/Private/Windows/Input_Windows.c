#include "../Input/InputPlatform.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Xinput.h>

#include <string.h>

// XInput is a stateless per-call API — nothing to set up or tear down.
void Fluxion_Input_PlatformInit(void)
{
}

void Fluxion_Input_PlatformShutdown(void)
{
}

FluxionKeyCode Fluxion_Input_TranslateKeyCode(i32 osKeyCode)
{
    // VK_A..VK_Z and VK_0..VK_9 conveniently match ASCII.
    if (osKeyCode >= 'A' && osKeyCode <= 'Z')
    {
        return (FluxionKeyCode)(FLUXION_KEY_A + (osKeyCode - 'A'));
    }
    if (osKeyCode >= '0' && osKeyCode <= '9')
    {
        return (FluxionKeyCode)(FLUXION_KEY_0 + (osKeyCode - '0'));
    }
    if (osKeyCode >= VK_F1 && osKeyCode <= VK_F12)
    {
        return (FluxionKeyCode)(FLUXION_KEY_F1 + (osKeyCode - VK_F1));
    }
    if (osKeyCode >= VK_NUMPAD0 && osKeyCode <= VK_NUMPAD9)
    {
        return (FluxionKeyCode)(FLUXION_KEY_NUMPAD_0 + (osKeyCode - VK_NUMPAD0));
    }

    switch (osKeyCode)
    {
        case VK_ESCAPE:   return FLUXION_KEY_ESCAPE;
        case VK_TAB:      return FLUXION_KEY_TAB;
        case VK_CAPITAL:  return FLUXION_KEY_CAPS_LOCK;
        case VK_LSHIFT:   return FLUXION_KEY_LEFT_SHIFT;
        case VK_RSHIFT:   return FLUXION_KEY_RIGHT_SHIFT;
        // WM_KEYDOWN often reports the generic VK_SHIFT/VK_CONTROL/VK_MENU
        // rather than the L/R-specific code unless the scan code is
        // decoded separately; default to the left variant as a documented
        // v1 simplification.
        case VK_SHIFT:    return FLUXION_KEY_LEFT_SHIFT;
        case VK_LCONTROL: return FLUXION_KEY_LEFT_CONTROL;
        case VK_RCONTROL: return FLUXION_KEY_RIGHT_CONTROL;
        case VK_CONTROL:  return FLUXION_KEY_LEFT_CONTROL;
        case VK_LMENU:    return FLUXION_KEY_LEFT_ALT;
        case VK_RMENU:    return FLUXION_KEY_RIGHT_ALT;
        case VK_MENU:     return FLUXION_KEY_LEFT_ALT;
        case VK_SPACE:    return FLUXION_KEY_SPACE;
        case VK_RETURN:   return FLUXION_KEY_ENTER;
        case VK_BACK:     return FLUXION_KEY_BACKSPACE;
        case VK_DELETE:   return FLUXION_KEY_DELETE;
        case VK_INSERT:   return FLUXION_KEY_INSERT;
        case VK_HOME:     return FLUXION_KEY_HOME;
        case VK_END:      return FLUXION_KEY_END;
        case VK_PRIOR:    return FLUXION_KEY_PAGE_UP;
        case VK_NEXT:     return FLUXION_KEY_PAGE_DOWN;
        case VK_UP:       return FLUXION_KEY_UP;
        case VK_DOWN:     return FLUXION_KEY_DOWN;
        case VK_LEFT:     return FLUXION_KEY_LEFT;
        case VK_RIGHT:    return FLUXION_KEY_RIGHT;
        default:          return FLUXION_KEY_UNKNOWN;
    }
}

static f32 Fluxion_NormalizeStickAxis(SHORT value)
{
    return value < 0 ? (f32)value / 32768.0f : (f32)value / 32767.0f;
}

void Fluxion_Input_PollGamepads(FluxionGamepadState outStates[FLUXION_MAX_GAMEPADS])
{
    for (DWORD i = 0; i < FLUXION_MAX_GAMEPADS; ++i)
    {
        FluxionGamepadState* out = &outStates[i];

        XINPUT_STATE state;
        memset(&state, 0, sizeof(state));

        if (XInputGetState(i, &state) != ERROR_SUCCESS)
        {
            memset(out, 0, sizeof(*out));
            out->connected = false;
            continue;
        }

        out->connected = true;

        WORD buttons = state.Gamepad.wButtons;
        out->buttons[FLUXION_GAMEPAD_BUTTON_A]              = (buttons & XINPUT_GAMEPAD_A) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_B]              = (buttons & XINPUT_GAMEPAD_B) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_X]              = (buttons & XINPUT_GAMEPAD_X) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_Y]              = (buttons & XINPUT_GAMEPAD_Y) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_LEFT_SHOULDER]  = (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_RIGHT_SHOULDER] = (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_BACK]           = (buttons & XINPUT_GAMEPAD_BACK) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_START]          = (buttons & XINPUT_GAMEPAD_START) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_LEFT_STICK]     = (buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_RIGHT_STICK]    = (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_DPAD_UP]        = (buttons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_DPAD_DOWN]      = (buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_DPAD_LEFT]      = (buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        out->buttons[FLUXION_GAMEPAD_BUTTON_DPAD_RIGHT]     = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

        out->axes[FLUXION_GAMEPAD_AXIS_LEFT_X]        = Fluxion_NormalizeStickAxis(state.Gamepad.sThumbLX);
        out->axes[FLUXION_GAMEPAD_AXIS_LEFT_Y]        = Fluxion_NormalizeStickAxis(state.Gamepad.sThumbLY);
        out->axes[FLUXION_GAMEPAD_AXIS_RIGHT_X]       = Fluxion_NormalizeStickAxis(state.Gamepad.sThumbRX);
        out->axes[FLUXION_GAMEPAD_AXIS_RIGHT_Y]       = Fluxion_NormalizeStickAxis(state.Gamepad.sThumbRY);
        out->axes[FLUXION_GAMEPAD_AXIS_LEFT_TRIGGER]  = (f32)state.Gamepad.bLeftTrigger / 255.0f;
        out->axes[FLUXION_GAMEPAD_AXIS_RIGHT_TRIGGER] = (f32)state.Gamepad.bRightTrigger / 255.0f;
    }
}
