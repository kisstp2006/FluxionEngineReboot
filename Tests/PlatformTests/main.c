#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_Memory_Run(TestContext* ctx);
void Test_Time_Run(TestContext* ctx);
void Test_CPU_Run(TestContext* ctx);
void Test_Debug_Run(TestContext* ctx);
void Test_Environment_Run(TestContext* ctx);
void Test_Process_Run(TestContext* ctx);
void Test_File_Run(TestContext* ctx);
void Test_DynamicLibrary_Run(TestContext* ctx);
void Test_Thread_Run(TestContext* ctx);
void Test_Synchronization_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("PlatformTests", "Running PlatformTests...");

    Test_Memory_Run(&ctx);
    Test_Time_Run(&ctx);
    Test_CPU_Run(&ctx);
    Test_Debug_Run(&ctx);
    Test_Environment_Run(&ctx);
    Test_Process_Run(&ctx);
    Test_File_Run(&ctx);
    Test_DynamicLibrary_Run(&ctx);
    Test_Thread_Run(&ctx);
    Test_Synchronization_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("PlatformTests", "All PlatformTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("PlatformTests", "%d PlatformTests check(s) failed.", ctx.failures);
    return 1;
}
