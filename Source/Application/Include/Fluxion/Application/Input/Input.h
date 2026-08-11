#pragma once

#include <Fluxion/Application/Events/Event.h>
#include <Fluxion/Application/Input/Gamepad.h>
#include <Fluxion/Application/Input/KeyCode.h>
#include <Fluxion/Application/Input/MouseButton.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

void Fluxion_Input_Init(void);
void Fluxion_Input_Shutdown(void);

// Call once per frame, before draining the event queue: resets the
// "pressed/released this frame" transient state and polls connected
// gamepads (gamepads are polled, not event-driven, unlike keyboard/mouse).
void Fluxion_Input_BeginFrame(void);

// Call for every event popped from a FluxionEventQueue this frame,
// alongside whatever else the caller does with that same event (e.g.
// handling FLUXION_EVENT_WINDOW_CLOSE_REQUESTED itself). Events Input
// doesn't care about are ignored.
void Fluxion_Input_ProcessEvent(const FluxionEvent* event);

bool Fluxion_Input_IsKeyDown(FluxionKeyCode key);
bool Fluxion_Input_WasKeyPressed(FluxionKeyCode key);
bool Fluxion_Input_WasKeyReleased(FluxionKeyCode key);

void Fluxion_Input_GetMousePosition(i32* outX, i32* outY);
void Fluxion_Input_GetMouseDelta(i32* outDeltaX, i32* outDeltaY);
f32  Fluxion_Input_GetMouseScrollDelta(void);

bool Fluxion_Input_IsMouseButtonDown(FluxionMouseButton button);
bool Fluxion_Input_WasMouseButtonPressed(FluxionMouseButton button);
bool Fluxion_Input_WasMouseButtonReleased(FluxionMouseButton button);

// Returns false (and leaves *outState untouched) if gamepadIndex is out
// of range ([0, FLUXION_MAX_GAMEPADS)).
bool Fluxion_Input_GetGamepadState(u32 gamepadIndex, FluxionGamepadState* outState);

#ifdef __cplusplus
}
#endif
