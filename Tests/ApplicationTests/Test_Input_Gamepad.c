#include "TestFramework.h"

#include <Fluxion/Application/Input/Input.h>

// Whether any slot reports connected depends on what the host exposes at
// /dev/input (Linux) or via XInput (Windows) -- some CI runner images
// expose a virtual joystick device even with no physical hardware
// plugged in, so this can't assert "nothing is ever connected" as a
// build-machine guarantee. What it does verify: the query itself is
// well-formed for every valid slot (doesn't crash, rejects an
// out-of-range index), and that a slot reporting NOT connected reports
// an all-zero state rather than stale/garbage data.
void Test_Input_Gamepad_Run(TestContext* ctx)
{
    Fluxion_Input_Init();
    Fluxion_Input_BeginFrame(); // polls gamepads

    for (u32 i = 0; i < FLUXION_MAX_GAMEPADS; ++i)
    {
        FluxionGamepadState state;
        TEST_CHECK(ctx, Fluxion_Input_GetGamepadState(i, &state));

        if (!state.connected)
        {
            for (u32 axis = 0; axis < FLUXION_GAMEPAD_AXIS_COUNT; ++axis)
            {
                TEST_CHECK(ctx, state.axes[axis] == 0.0f);
            }
            for (u32 button = 0; button < FLUXION_GAMEPAD_BUTTON_COUNT; ++button)
            {
                TEST_CHECK(ctx, state.buttons[button] == false);
            }
        }
    }

    FluxionGamepadState outOfRange;
    TEST_CHECK(ctx, Fluxion_Input_GetGamepadState(FLUXION_MAX_GAMEPADS, &outOfRange) == false);

    Fluxion_Input_Shutdown();
}
