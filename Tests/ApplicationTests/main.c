#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_EventQueue_Run(TestContext* ctx);
void Test_Window_Run(TestContext* ctx);
void Test_WindowEvents_Run(TestContext* ctx);
void Test_Display_Run(TestContext* ctx);
void Test_Clipboard_Run(TestContext* ctx);
void Test_Input_Keyboard_Run(TestContext* ctx);
void Test_Input_Mouse_Run(TestContext* ctx);
void Test_Input_Gamepad_Run(TestContext* ctx);
void Test_InputAction_Run(TestContext* ctx);
void Test_Time_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("ApplicationTests", "Running ApplicationTests...");

    Test_EventQueue_Run(&ctx);
    Test_Window_Run(&ctx);
    Test_WindowEvents_Run(&ctx);
    Test_Display_Run(&ctx);
    Test_Clipboard_Run(&ctx);
    Test_Input_Keyboard_Run(&ctx);
    Test_Input_Mouse_Run(&ctx);
    Test_Input_Gamepad_Run(&ctx);
    Test_InputAction_Run(&ctx);
    Test_Time_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("ApplicationTests", "All ApplicationTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("ApplicationTests", "%d ApplicationTests check(s) failed.", ctx.failures);
    return 1;
}
