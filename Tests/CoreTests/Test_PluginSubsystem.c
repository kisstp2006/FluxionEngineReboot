#include "TestFramework.h"

#include <Fluxion/Core/Plugin/Manager.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Service/ServiceRegistry.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Core/Startup/SubsystemRegistry.h>

void Test_PluginSubsystem_Run(TestContext* ctx)
{
    Fluxion_SubsystemRegistry_Init();
    Fluxion_ServiceRegistry_Init();
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_PLUGIN_SUBSYSTEM_PLUGIN_PATH };
        bool loaded = Fluxion_PluginManager_LoadAll(paths, 1);
        TEST_CHECK(ctx, loaded);

        const FluxionSubsystemId subsystemId = FLUXION_SUBSYSTEM_ID_OF(PluginSubsystem);
        FluxionResult startResult = Fluxion_SubsystemRegistry_StartupAll();
        TEST_CHECK(ctx, startResult.ok);
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_IsRunning(subsystemId));

        // The plugin registers this in Fluxion_Plugin_Load -- should be
        // visible to the host right away, no startup step needed for
        // services (unlike subsystems).
        const FluxionServiceId serviceId = FLUXION_SERVICE_ID_OF(PluginTestService);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(serviceId, 1) != NULL);

        Fluxion_SubsystemRegistry_ShutdownAll();
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(subsystemId));
    }
    Fluxion_PluginManager_Shutdown(); // unloads the plugin -> Fluxion_Plugin_Unload runs
    TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(FLUXION_SERVICE_ID_OF(PluginTestService), 1) == NULL);
    Fluxion_ServiceRegistry_Shutdown();
    Fluxion_SubsystemRegistry_Shutdown();
}
