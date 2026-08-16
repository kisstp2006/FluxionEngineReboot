#include "TestFramework.h"

#include <Fluxion/Core/Service/ServiceRegistry.h>

#include <cstdio>

void Test_VirtualFileSystem_Run(TestContext& ctx);
void Test_AssetTypes_Run(TestContext& ctx);
void Test_AssetDatabase_Run(TestContext& ctx);
void Test_AssetLoading_Run(TestContext& ctx);
void Test_Package_Run(TestContext& ctx);
void Test_AssetImport_Run(TestContext& ctx);
void Test_AssetReload_Run(TestContext& ctx);

int main()
{
    TestContext ctx;

    std::fprintf(stderr, "Running AssetsTests...\n");

    // Brought up once for the whole run: the asset system publishes
    // itself as a service, which is how a plugin adds a type. That is not
    // something an individual case opts into -- it is part of what
    // starting the asset system means.
    Fluxion_ServiceRegistry_Init();

    Test_VirtualFileSystem_Run(ctx);
    Test_AssetTypes_Run(ctx);
    Test_AssetDatabase_Run(ctx);
    Test_AssetLoading_Run(ctx);
    Test_Package_Run(ctx);
    Test_AssetImport_Run(ctx);
    Test_AssetReload_Run(ctx);

    Fluxion_ServiceRegistry_Shutdown();

    if (ctx.failures == 0)
    {
        std::fprintf(stderr, "All AssetsTests passed.\n");
        return 0;
    }

    std::fprintf(stderr, "%d AssetsTests check(s) failed.\n", ctx.failures);
    return 1;
}
