#include "../Input/InputPlatform.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <fcntl.h>
#include <linux/joystick.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

FluxionKeyCode Fluxion_Input_TranslateKeyCode(i32 osKeyCode)
{
    // Window_Linux.c stores a KeySym here (via XLookupKeysym), not the raw
    // hardware keycode -- XK_* constants are stable/documented, unlike
    // raw scancodes which are layout/device dependent.
    KeySym keysym = (KeySym)osKeyCode;

    if (keysym >= XK_a && keysym <= XK_z)
    {
        return (FluxionKeyCode)(FLUXION_KEY_A + (keysym - XK_a));
    }
    if (keysym >= XK_A && keysym <= XK_Z)
    {
        return (FluxionKeyCode)(FLUXION_KEY_A + (keysym - XK_A));
    }
    if (keysym >= XK_0 && keysym <= XK_9)
    {
        return (FluxionKeyCode)(FLUXION_KEY_0 + (keysym - XK_0));
    }
    if (keysym >= XK_F1 && keysym <= XK_F12)
    {
        return (FluxionKeyCode)(FLUXION_KEY_F1 + (keysym - XK_F1));
    }
    if (keysym >= XK_KP_0 && keysym <= XK_KP_9)
    {
        return (FluxionKeyCode)(FLUXION_KEY_NUMPAD_0 + (keysym - XK_KP_0));
    }

    switch (keysym)
    {
        case XK_Escape:    return FLUXION_KEY_ESCAPE;
        case XK_Tab:       return FLUXION_KEY_TAB;
        case XK_Caps_Lock: return FLUXION_KEY_CAPS_LOCK;
        case XK_Shift_L:   return FLUXION_KEY_LEFT_SHIFT;
        case XK_Shift_R:   return FLUXION_KEY_RIGHT_SHIFT;
        case XK_Control_L: return FLUXION_KEY_LEFT_CONTROL;
        case XK_Control_R: return FLUXION_KEY_RIGHT_CONTROL;
        case XK_Alt_L:     return FLUXION_KEY_LEFT_ALT;
        case XK_Alt_R:     return FLUXION_KEY_RIGHT_ALT;
        case XK_space:     return FLUXION_KEY_SPACE;
        case XK_Return:    return FLUXION_KEY_ENTER;
        case XK_BackSpace: return FLUXION_KEY_BACKSPACE;
        case XK_Delete:    return FLUXION_KEY_DELETE;
        case XK_Insert:    return FLUXION_KEY_INSERT;
        case XK_Home:      return FLUXION_KEY_HOME;
        case XK_End:       return FLUXION_KEY_END;
        case XK_Prior:     return FLUXION_KEY_PAGE_UP;
        case XK_Next:      return FLUXION_KEY_PAGE_DOWN;
        case XK_Up:        return FLUXION_KEY_UP;
        case XK_Down:      return FLUXION_KEY_DOWN;
        case XK_Left:      return FLUXION_KEY_LEFT;
        case XK_Right:     return FLUXION_KEY_RIGHT;
        default:           return FLUXION_KEY_UNKNOWN;
    }
}

#define FLUXION_JOYSTICK_MAX_AXES    8
#define FLUXION_JOYSTICK_MAX_BUTTONS 16

typedef struct FluxionJoystickDevice
{
    int fd;
    bool opened;
    i16 axisValues[FLUXION_JOYSTICK_MAX_AXES];
    bool buttonValues[FLUXION_JOYSTICK_MAX_BUTTONS];
} FluxionJoystickDevice;

static FluxionJoystickDevice s_joysticks[FLUXION_MAX_GAMEPADS];

void Fluxion_Input_PlatformInit(void)
{
    memset(s_joysticks, 0, sizeof(s_joysticks));
    for (u32 i = 0; i < FLUXION_MAX_GAMEPADS; ++i)
    {
        s_joysticks[i].fd = -1;
    }
}

void Fluxion_Input_PlatformShutdown(void)
{
    for (u32 i = 0; i < FLUXION_MAX_GAMEPADS; ++i)
    {
        if (s_joysticks[i].opened)
        {
            close(s_joysticks[i].fd);
        }
    }
    memset(s_joysticks, 0, sizeof(s_joysticks));
}

static void Fluxion_TryOpenJoystick(u32 index)
{
    char path[32];
    snprintf(path, sizeof(path), "/dev/input/js%u", index);

    int fd = open(path, O_RDONLY | O_NONBLOCK);
    s_joysticks[index].fd = fd;
    s_joysticks[index].opened = (fd >= 0);
    memset(s_joysticks[index].axisValues, 0, sizeof(s_joysticks[index].axisValues));
    memset(s_joysticks[index].buttonValues, 0, sizeof(s_joysticks[index].buttonValues));
}

static f32 Fluxion_NormalizeJoystickAxis(i16 value)
{
    return value < 0 ? (f32)value / 32768.0f : (f32)value / 32767.0f;
}

void Fluxion_Input_PollGamepads(FluxionGamepadState outStates[FLUXION_MAX_GAMEPADS])
{
    for (u32 i = 0; i < FLUXION_MAX_GAMEPADS; ++i)
    {
        FluxionGamepadState* out = &outStates[i];
        FluxionJoystickDevice* device = &s_joysticks[i];

        if (!device->opened)
        {
            // Retry in case a controller was plugged in after startup.
            Fluxion_TryOpenJoystick(i);
        }

        if (!device->opened)
        {
            memset(out, 0, sizeof(*out));
            out->connected = false;
            continue;
        }

        struct js_event event;
        while (read(device->fd, &event, sizeof(event)) == (ssize_t)sizeof(event))
        {
            u8 type = (u8)(event.type & ~JS_EVENT_INIT);
            if (type == JS_EVENT_BUTTON && event.number < FLUXION_JOYSTICK_MAX_BUTTONS)
            {
                device->buttonValues[event.number] = (event.value != 0);
            }
            else if (type == JS_EVENT_AXIS && event.number < FLUXION_JOYSTICK_MAX_AXES)
            {
                device->axisValues[event.number] = event.value;
            }
        }
        // A negative return here just means "no more events pending" on
        // this non-blocking fd — expected, not an error.

        out->connected = true;

        // Best-effort default layout (common for Xbox-style pads on
        // Linux via the kernel's xpad/hid-generic drivers): buttons 0-3 =
        // A/B/X/Y, 4-5 = shoulders, 6-7 = back/start, 8-9 = stick clicks;
        // axes 0-1 = left stick, 2-3 = right stick. Unlike XInput on
        // Windows this isn't standardized across every controller/driver
        // combination, and hasn't been verified against real hardware —
        // a reasonable default, not a guarantee. D-pad is often reported
        // as extra hat axes rather than buttons depending on the driver,
        // so it's left unmapped here rather than guessed at.
        out->buttons[FLUXION_GAMEPAD_BUTTON_A]              = device->buttonValues[0];
        out->buttons[FLUXION_GAMEPAD_BUTTON_B]              = device->buttonValues[1];
        out->buttons[FLUXION_GAMEPAD_BUTTON_X]              = device->buttonValues[2];
        out->buttons[FLUXION_GAMEPAD_BUTTON_Y]              = device->buttonValues[3];
        out->buttons[FLUXION_GAMEPAD_BUTTON_LEFT_SHOULDER]  = device->buttonValues[4];
        out->buttons[FLUXION_GAMEPAD_BUTTON_RIGHT_SHOULDER] = device->buttonValues[5];
        out->buttons[FLUXION_GAMEPAD_BUTTON_BACK]           = device->buttonValues[6];
        out->buttons[FLUXION_GAMEPAD_BUTTON_START]          = device->buttonValues[7];
        out->buttons[FLUXION_GAMEPAD_BUTTON_LEFT_STICK]     = device->buttonValues[8];
        out->buttons[FLUXION_GAMEPAD_BUTTON_RIGHT_STICK]    = device->buttonValues[9];
        out->buttons[FLUXION_GAMEPAD_BUTTON_DPAD_UP]        = false;
        out->buttons[FLUXION_GAMEPAD_BUTTON_DPAD_DOWN]      = false;
        out->buttons[FLUXION_GAMEPAD_BUTTON_DPAD_LEFT]      = false;
        out->buttons[FLUXION_GAMEPAD_BUTTON_DPAD_RIGHT]     = false;

        out->axes[FLUXION_GAMEPAD_AXIS_LEFT_X]        = Fluxion_NormalizeJoystickAxis(device->axisValues[0]);
        out->axes[FLUXION_GAMEPAD_AXIS_LEFT_Y]        = Fluxion_NormalizeJoystickAxis(device->axisValues[1]);
        out->axes[FLUXION_GAMEPAD_AXIS_RIGHT_X]       = Fluxion_NormalizeJoystickAxis(device->axisValues[2]);
        out->axes[FLUXION_GAMEPAD_AXIS_RIGHT_Y]       = Fluxion_NormalizeJoystickAxis(device->axisValues[3]);
        out->axes[FLUXION_GAMEPAD_AXIS_LEFT_TRIGGER]  = 0.0f;
        out->axes[FLUXION_GAMEPAD_AXIS_RIGHT_TRIGGER] = 0.0f;
    }
}
