#include "TestFramework.h"

#include <Fluxion/Core/Plugin/Manager.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Core/Startup/SubsystemRegistry.h>

void Test_PluginSubsystem_Run(TestContext* ctx)
{
    Fluxion_SubsystemRegistry_Init();
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_PLUGIN_SUBSYSTEM_PLUGIN_PATH };
        bool loaded = Fluxion_PluginManager_LoadAll(paths, 1);
        TEST_CHECK(ctx, loaded);

        FluxionResult startResult = Fluxion_SubsystemRegistry_StartupAll();
        TEST_CHECK(ctx, startResult.ok);

        const FluxionSubsystemId id = FLUXION_SUBSYSTEM_ID_OF(PluginSubsystem);
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_IsRunning(id));

        Fluxion_SubsystemRegistry_ShutdownAll();
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(id));
    }
    Fluxion_PluginManager_Shutdown();
    Fluxion_SubsystemRegistry_Shutdown();
}
