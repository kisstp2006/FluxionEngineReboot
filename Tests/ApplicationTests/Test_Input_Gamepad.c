#include "TestFramework.h"

#include <Fluxion/Application/Input/Input.h>

// No physical gamepad exists on either build machine, so this can only
// verify the "nothing connected" path doesn't crash and reports cleanly —
// not the actual button/axis mapping, which needs real hardware.
void Test_Input_Gamepad_Run(TestContext* ctx)
{
    Fluxion_Input_Init();
    Fluxion_Input_BeginFrame(); // polls gamepads

    for (u32 i = 0; i < FLUXION_MAX_GAMEPADS; ++i)
    {
        FluxionGamepadState state;
        TEST_CHECK(ctx, Fluxion_Input_GetGamepadState(i, &state));
        TEST_CHECK(ctx, state.connected == false);
    }

    FluxionGamepadState outOfRange;
    TEST_CHECK(ctx, Fluxion_Input_GetGamepadState(FLUXION_MAX_GAMEPADS, &outOfRange) == false);

    Fluxion_Input_Shutdown();
}
