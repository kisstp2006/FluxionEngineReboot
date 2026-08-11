#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_Reflection_Run(TestContext* ctx);
void Test_Plugin_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("CoreTests", "Running CoreTests...");

    Test_Reflection_Run(&ctx);
    Test_Plugin_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("CoreTests", "All CoreTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("CoreTests", "%d CoreTests check(s) failed.", ctx.failures);
    return 1;
}
