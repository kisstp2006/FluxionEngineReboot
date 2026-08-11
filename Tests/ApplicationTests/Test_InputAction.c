#include "TestFramework.h"

#include <Fluxion/Application/Input/Input.h>
#include <Fluxion/Application/Input/InputAction.h>

// Uses mouse-button and gamepad-button bindings (not keyboard) so this
// test stays fully portable — no OS-specific key code needed to exercise
// FluxionInputBinding/registration/lookup logic itself.
void Test_InputAction_Run(TestContext* ctx)
{
    Fluxion_Input_Init();
    Fluxion_Input_BeginFrame();

    FluxionInputBinding bindings[2];
    bindings[0].type = FLUXION_INPUT_SOURCE_MOUSE_BUTTON;
    bindings[0].source.mouseButton = FLUXION_MOUSE_BUTTON_LEFT;
    bindings[1].type = FLUXION_INPUT_SOURCE_GAMEPAD_BUTTON;
    bindings[1].source.gamepadButton.gamepadIndex = 0;
    bindings[1].source.gamepadButton.button = FLUXION_GAMEPAD_BUTTON_A;

    TEST_CHECK(ctx, Fluxion_InputAction_Register("Fire", bindings, 2));
    TEST_CHECK(ctx, !Fluxion_InputAction_IsDown("Fire"));
    TEST_CHECK(ctx, !Fluxion_InputAction_IsDown("DoesNotExist"));

    FluxionEvent buttonDown;
    buttonDown.type = FLUXION_EVENT_MOUSE_BUTTON_DOWN;
    buttonDown.window.index = 0;
    buttonDown.window.generation = 1;
    buttonDown.data.mouseButton.button = FLUXION_MOUSE_BUTTON_LEFT;
    buttonDown.data.mouseButton.x = 0;
    buttonDown.data.mouseButton.y = 0;
    Fluxion_Input_ProcessEvent(&buttonDown);

    TEST_CHECK(ctx, Fluxion_InputAction_IsDown("Fire"));

    FluxionEvent buttonUp = buttonDown;
    buttonUp.type = FLUXION_EVENT_MOUSE_BUTTON_UP;
    Fluxion_Input_ProcessEvent(&buttonUp);
    TEST_CHECK(ctx, !Fluxion_InputAction_IsDown("Fire"));

    // Re-registering the same name replaces its bindings entirely.
    FluxionInputBinding singleBinding;
    singleBinding.type = FLUXION_INPUT_SOURCE_MOUSE_BUTTON;
    singleBinding.source.mouseButton = FLUXION_MOUSE_BUTTON_RIGHT;
    TEST_CHECK(ctx, Fluxion_InputAction_Register("Fire", &singleBinding, 1));

    Fluxion_Input_ProcessEvent(&buttonDown); // left button — no longer bound
    TEST_CHECK(ctx, !Fluxion_InputAction_IsDown("Fire"));

    Fluxion_Input_Shutdown();
}
