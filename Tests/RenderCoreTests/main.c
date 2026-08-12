#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_PassRegistry_Run(TestContext* ctx);
void Test_GraphCompile_Run(TestContext* ctx);
void Test_JsonLoader_Run(TestContext* ctx);
void Test_DumpDot_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("RenderCoreTests", "Running RenderCoreTests...");

    Test_PassRegistry_Run(&ctx);
    Test_GraphCompile_Run(&ctx);
    Test_JsonLoader_Run(&ctx);
    Test_DumpDot_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("RenderCoreTests", "All RenderCoreTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("RenderCoreTests", "%d RenderCoreTests check(s) failed.", ctx.failures);
    return 1;
}
