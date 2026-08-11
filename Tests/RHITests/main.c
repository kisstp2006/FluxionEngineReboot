#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_Handles_Run(TestContext* ctx);
void Test_Capabilities_Run(TestContext* ctx);
void Test_NullBackend_Run(TestContext* ctx);
void Test_NativeHandle_Run(TestContext* ctx);
void Test_VulkanBackend_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("RHITests", "Running RHITests...");

    Test_Handles_Run(&ctx);
    Test_Capabilities_Run(&ctx);
    Test_NullBackend_Run(&ctx);
    Test_NativeHandle_Run(&ctx);
    Test_VulkanBackend_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("RHITests", "All RHITests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("RHITests", "%d RHITests check(s) failed.", ctx.failures);
    return 1;
}
