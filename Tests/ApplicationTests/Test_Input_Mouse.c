#include "TestFramework.h"

#include <Fluxion/Application/Input/Input.h>

void Test_Input_Mouse_Run(TestContext* ctx)
{
    Fluxion_Input_Init();
    Fluxion_Input_BeginFrame();

    FluxionEvent moveEvent;
    moveEvent.type = FLUXION_EVENT_MOUSE_MOVED;
    moveEvent.window.index = 0;
    moveEvent.window.generation = 1;
    moveEvent.data.mouseMoved.x = 100;
    moveEvent.data.mouseMoved.y = 50;
    Fluxion_Input_ProcessEvent(&moveEvent);

    i32 x = 0, y = 0;
    Fluxion_Input_GetMousePosition(&x, &y);
    TEST_CHECK(ctx, x == 100 && y == 50);

    i32 dx = 0, dy = 0;
    Fluxion_Input_GetMouseDelta(&dx, &dy);
    TEST_CHECK(ctx, dx == 100 && dy == 50); // moved from (0,0) since Init

    moveEvent.data.mouseMoved.x = 110;
    moveEvent.data.mouseMoved.y = 45;
    Fluxion_Input_ProcessEvent(&moveEvent);
    Fluxion_Input_GetMouseDelta(&dx, &dy);
    TEST_CHECK(ctx, dx == 110 && dy == 45); // still accumulating since the last BeginFrame

    Fluxion_Input_BeginFrame(); // resets the accumulated delta
    Fluxion_Input_GetMouseDelta(&dx, &dy);
    TEST_CHECK(ctx, dx == 0 && dy == 0);

    TEST_CHECK(ctx, !Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT));

    FluxionEvent buttonDown;
    buttonDown.type = FLUXION_EVENT_MOUSE_BUTTON_DOWN;
    buttonDown.window.index = 0;
    buttonDown.window.generation = 1;
    buttonDown.data.mouseButton.button = FLUXION_MOUSE_BUTTON_LEFT;
    buttonDown.data.mouseButton.x = 110;
    buttonDown.data.mouseButton.y = 45;
    Fluxion_Input_ProcessEvent(&buttonDown);

    TEST_CHECK(ctx, Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT));
    TEST_CHECK(ctx, Fluxion_Input_WasMouseButtonPressed(FLUXION_MOUSE_BUTTON_LEFT));

    Fluxion_Input_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT));
    TEST_CHECK(ctx, !Fluxion_Input_WasMouseButtonPressed(FLUXION_MOUSE_BUTTON_LEFT));

    FluxionEvent buttonUp = buttonDown;
    buttonUp.type = FLUXION_EVENT_MOUSE_BUTTON_UP;
    Fluxion_Input_ProcessEvent(&buttonUp);
    TEST_CHECK(ctx, !Fluxion_Input_IsMouseButtonDown(FLUXION_MOUSE_BUTTON_LEFT));
    TEST_CHECK(ctx, Fluxion_Input_WasMouseButtonReleased(FLUXION_MOUSE_BUTTON_LEFT));

    TEST_CHECK(ctx, Fluxion_Input_GetMouseScrollDelta() == 0.0f);

    FluxionEvent scroll;
    scroll.type = FLUXION_EVENT_MOUSE_SCROLLED;
    scroll.window.index = 0;
    scroll.window.generation = 1;
    scroll.data.mouseScroll.deltaX = 0.0f;
    scroll.data.mouseScroll.deltaY = 1.0f;
    Fluxion_Input_ProcessEvent(&scroll);
    TEST_CHECK(ctx, Fluxion_Input_GetMouseScrollDelta() == 1.0f);

    Fluxion_Input_Shutdown();
}
