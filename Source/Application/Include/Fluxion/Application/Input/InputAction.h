#pragma once

#include <Fluxion/Application/Input/Gamepad.h>
#include <Fluxion/Application/Input/KeyCode.h>
#include <Fluxion/Application/Input/MouseButton.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLUXION_INPUT_ACTION_MAX_ACTIONS      32
#define FLUXION_INPUT_ACTION_MAX_BINDINGS     4
#define FLUXION_INPUT_ACTION_MAX_NAME_LENGTH  31

typedef enum FluxionInputSourceType
{
    FLUXION_INPUT_SOURCE_KEY = 0,
    FLUXION_INPUT_SOURCE_MOUSE_BUTTON,
    FLUXION_INPUT_SOURCE_GAMEPAD_BUTTON,
    FLUXION_INPUT_SOURCE_GAMEPAD_AXIS,
} FluxionInputSourceType;

typedef struct FluxionInputBinding
{
    FluxionInputSourceType type;
    union
    {
        FluxionKeyCode key;
        FluxionMouseButton mouseButton;
        struct
        {
            u32 gamepadIndex;
            FluxionGamepadButton button;
        } gamepadButton;
        struct
        {
            u32 gamepadIndex;
            FluxionGamepadAxis axis;
            f32 threshold;  // e.g. 0.5
            bool positive;  // true: axis >= threshold; false: axis <= -threshold
        } gamepadAxis;
    } source;
} FluxionInputBinding;

// Registers (or replaces, if `name` already exists) an action considered
// "down" when ANY of its bound physical inputs is down. Returns false if
// the action table is full (FLUXION_INPUT_ACTION_MAX_ACTIONS), the name
// is too long, or bindingCount exceeds FLUXION_INPUT_ACTION_MAX_BINDINGS.
bool Fluxion_InputAction_Register(const char* name, const FluxionInputBinding* bindings, u32 bindingCount);

bool Fluxion_InputAction_IsDown(const char* name);

#ifdef __cplusplus
}
#endif
