#include "TestFramework.h"

#include <Fluxion/Platform/Debug.h>

void Test_Debug_Run(TestContext* ctx)
{
    // CTest runs without a debugger attached, so this specific value is a
    // valid thing to assert, not just "doesn't crash".
    TEST_CHECK(ctx, Fluxion_Platform_IsDebuggerAttached() == false);

    Fluxion_Platform_DebugPrint("PlatformTests debug print smoke test\n");
}
