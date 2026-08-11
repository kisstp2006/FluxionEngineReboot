#include "TestFramework.h"

#include <Fluxion/Core/Plugin/Manager.h>

#include <string.h>

void Test_Plugin_Run(TestContext* ctx)
{
    // Basic load + dependency ordering.
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_HELLO_PLUGIN_PATH, FLUXION_TEST_HELLO_PLUGIN_DEPENDENT_PATH };
        bool loaded = Fluxion_PluginManager_LoadAll(paths, 2);
        TEST_CHECK(ctx, loaded);

        if (loaded)
        {
            TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedCount() == 2);
            TEST_CHECK(ctx, strcmp(Fluxion_PluginManager_GetLoadedDescriptor(0)->name, "HelloPlugin") == 0);
            TEST_CHECK(ctx, strcmp(Fluxion_PluginManager_GetLoadedDescriptor(1)->name, "HelloPluginDependent") == 0);
            TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedApi(0)->userData == (void*)1);
            TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedApi(1)->userData == (void*)2);
        }
    }
    Fluxion_PluginManager_Shutdown();

    // Paths given in reverse dependency order still resolve correctly —
    // load order comes from the declared dependencies, not array order.
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_HELLO_PLUGIN_DEPENDENT_PATH, FLUXION_TEST_HELLO_PLUGIN_PATH };
        bool loaded = Fluxion_PluginManager_LoadAll(paths, 2);
        TEST_CHECK(ctx, loaded);

        if (loaded)
        {
            TEST_CHECK(ctx, strcmp(Fluxion_PluginManager_GetLoadedDescriptor(0)->name, "HelloPlugin") == 0);
            TEST_CHECK(ctx, strcmp(Fluxion_PluginManager_GetLoadedDescriptor(1)->name, "HelloPluginDependent") == 0);
        }
    }
    Fluxion_PluginManager_Shutdown();

    // Circular dependency: LoadAll must fail and load nothing.
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_CYCLIC_PLUGIN_A_PATH, FLUXION_TEST_CYCLIC_PLUGIN_B_PATH };
        TEST_CHECK(ctx, Fluxion_PluginManager_LoadAll(paths, 2) == false);
        TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedCount() == 0);
    }
    Fluxion_PluginManager_Shutdown();

    // Malformed .plugin JSON: LoadAll must fail and load nothing.
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_BAD_PLUGIN_PATH };
        TEST_CHECK(ctx, Fluxion_PluginManager_LoadAll(paths, 1) == false);
        TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedCount() == 0);
    }
    Fluxion_PluginManager_Shutdown();
}
