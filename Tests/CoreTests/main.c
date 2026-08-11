#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_Reflection_Run(TestContext* ctx);
void Test_Plugin_Run(TestContext* ctx);
void Test_Startup_Run(TestContext* ctx);
void Test_StartupCpp_Run(TestContext* ctx);
void Test_PluginSubsystem_Run(TestContext* ctx);
void Test_Service_Run(TestContext* ctx);
void Test_ServiceCpp_Run(TestContext* ctx);
void Test_ReflectionCpp_Run(TestContext* ctx);
void Test_Jobs_Run(TestContext* ctx);
void Test_JobsCpp_Run(TestContext* ctx);
void Test_BinarySerializer_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("CoreTests", "Running CoreTests...");

    Test_Reflection_Run(&ctx);
    Test_Plugin_Run(&ctx);
    Test_Startup_Run(&ctx);
    Test_StartupCpp_Run(&ctx);
    Test_PluginSubsystem_Run(&ctx);
    Test_Service_Run(&ctx);
    Test_ServiceCpp_Run(&ctx);
    Test_ReflectionCpp_Run(&ctx);
    Test_Jobs_Run(&ctx);
    Test_JobsCpp_Run(&ctx);
    Test_BinarySerializer_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("CoreTests", "All CoreTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("CoreTests", "%d CoreTests check(s) failed.", ctx.failures);
    return 1;
}
