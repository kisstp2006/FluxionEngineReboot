#include "TestFramework.h"

#include <Fluxion/Core/Plugin/Manager.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Service/ServiceRegistry.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Core/Startup/SubsystemRegistry.h>

void Test_PluginSubsystem_Run(TestContext* ctx)
{
    Fluxion_SubsystemRegistry_Init();
    Fluxion_ServiceRegistry_Init();
    Fluxion_Reflection_Init();
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_PLUGIN_SUBSYSTEM_PLUGIN_PATH };
        bool loaded = Fluxion_PluginManager_LoadAll(paths, 1);
        TEST_CHECK(ctx, loaded);

        const FluxionSubsystemId subsystemId = FLUXION_SUBSYSTEM_ID_OF(PluginSubsystem);
        FluxionResult startResult = Fluxion_SubsystemRegistry_StartupAll();
        TEST_CHECK(ctx, startResult.ok);
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_IsRunning(subsystemId));

        // The plugin registers these in Fluxion_Plugin_Load -- should be
        // visible to the host right away, no startup step needed (unlike
        // subsystems).
        const FluxionServiceId serviceId = FLUXION_SERVICE_ID_OF(PluginTestService);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(serviceId, 1) != NULL);

        // Cross-DLL stable TypeId: the id computed here, independently,
        // in the host's own compiled code must be the exact same u64 the
        // plugin computed in its own compiled code for the lookup to
        // succeed -- proving FluxionTypeId's hash-based identity (not a
        // pointer, not a compiler RTTI hash) is genuinely stable across
        // the plugin boundary, not just within one binary.
        const FluxionTypeId reflectedTypeId = FLUXION_TYPE_ID_OF(PluginTestReflectedType);
        const FluxionTypeInfo* reflectedType = Fluxion_Reflection_FindTypeById(reflectedTypeId);
        TEST_CHECK(ctx, reflectedType != NULL);
        TEST_CHECK(ctx, reflectedType != NULL && reflectedType->members.count == 1);

        Fluxion_SubsystemRegistry_ShutdownAll();
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(subsystemId));
    }
    Fluxion_PluginManager_Shutdown(); // unloads the plugin -> Fluxion_Plugin_Unload runs
    TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(FLUXION_SERVICE_ID_OF(PluginTestService), 1) == NULL);
    Fluxion_Reflection_Shutdown();
    Fluxion_ServiceRegistry_Shutdown();
    Fluxion_SubsystemRegistry_Shutdown();
}
