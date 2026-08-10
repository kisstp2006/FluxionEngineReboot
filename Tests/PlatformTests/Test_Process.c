#include "TestFramework.h"

#include <Fluxion/Platform/Process.h>

void Test_Process_Run(TestContext* ctx)
{
    u32 pid = Fluxion_Platform_GetCurrentProcessId();
    TEST_CHECK(ctx, pid != 0);
    // ExitProcess is intentionally not exercised — it would kill the test runner.
}
