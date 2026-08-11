#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// XInput's own hardware limit; a reasonable cap for the Linux joystick
// backend too (opens up to this many /dev/input/jsN devices).
#define FLUXION_MAX_GAMEPADS 4

typedef enum FluxionGamepadButton
{
    FLUXION_GAMEPAD_BUTTON_A = 0,
    FLUXION_GAMEPAD_BUTTON_B,
    FLUXION_GAMEPAD_BUTTON_X,
    FLUXION_GAMEPAD_BUTTON_Y,
    FLUXION_GAMEPAD_BUTTON_LEFT_SHOULDER,
    FLUXION_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    FLUXION_GAMEPAD_BUTTON_BACK,
    FLUXION_GAMEPAD_BUTTON_START,
    FLUXION_GAMEPAD_BUTTON_LEFT_STICK,
    FLUXION_GAMEPAD_BUTTON_RIGHT_STICK,
    FLUXION_GAMEPAD_BUTTON_DPAD_UP,
    FLUXION_GAMEPAD_BUTTON_DPAD_DOWN,
    FLUXION_GAMEPAD_BUTTON_DPAD_LEFT,
    FLUXION_GAMEPAD_BUTTON_DPAD_RIGHT,
    FLUXION_GAMEPAD_BUTTON_COUNT,
} FluxionGamepadButton;

typedef enum FluxionGamepadAxis
{
    FLUXION_GAMEPAD_AXIS_LEFT_X = 0,
    FLUXION_GAMEPAD_AXIS_LEFT_Y,
    FLUXION_GAMEPAD_AXIS_RIGHT_X,
    FLUXION_GAMEPAD_AXIS_RIGHT_Y,
    FLUXION_GAMEPAD_AXIS_LEFT_TRIGGER,
    FLUXION_GAMEPAD_AXIS_RIGHT_TRIGGER,
    FLUXION_GAMEPAD_AXIS_COUNT,
} FluxionGamepadAxis;

// POD, fixed-size — FLUXION_MAX_GAMEPADS of these cost nothing to keep
// around, no allocation.
typedef struct FluxionGamepadState
{
    bool connected;
    bool buttons[FLUXION_GAMEPAD_BUTTON_COUNT];
    f32 axes[FLUXION_GAMEPAD_AXIS_COUNT]; // sticks: -1..1, triggers: 0..1
} FluxionGamepadState;

#ifdef __cplusplus
}
#endif
