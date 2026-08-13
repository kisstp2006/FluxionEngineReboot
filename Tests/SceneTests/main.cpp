#include "TestFramework.h"

#include <cstdio>

void Test_Hierarchy_Run(TestContext& ctx);
void Test_Transform_Run(TestContext& ctx);
void Test_Components_Run(TestContext& ctx);
void Test_Attributes_Run(TestContext& ctx);
void Test_EngineApi_Run(TestContext& ctx);
void Test_Reload_Run(TestContext& ctx);

int main()
{
    TestContext ctx;

    std::fprintf(stderr, "Running SceneTests...\n");

    Test_Hierarchy_Run(ctx);
    Test_Transform_Run(ctx);
    Test_Components_Run(ctx);
    Test_Attributes_Run(ctx);
    Test_EngineApi_Run(ctx);
    Test_Reload_Run(ctx);

    if (ctx.failures == 0)
    {
        std::fprintf(stderr, "All SceneTests passed.\n");
        return 0;
    }

    std::fprintf(stderr, "%d SceneTests check(s) failed.\n", ctx.failures);
    return 1;
}
