#pragma once

#include <Fluxion/Application/Input/Gamepad.h>
#include <Fluxion/Application/Input/KeyCode.h>
#include <Fluxion/Foundation/Types.h>

// Internal contract between the platform-independent Input.c and each
// platform's Input_Windows.c / Input_Linux.c backend. Not a public header.

// Windows' XInput needs neither (stateless per-call API); Linux's
// joystick backend uses PlatformShutdown to close any open device file
// descriptors.
void Fluxion_Input_PlatformInit(void);
void Fluxion_Input_PlatformShutdown(void);

FluxionKeyCode Fluxion_Input_TranslateKeyCode(i32 osKeyCode);
void Fluxion_Input_PollGamepads(FluxionGamepadState outStates[FLUXION_MAX_GAMEPADS]);
