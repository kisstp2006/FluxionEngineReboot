#include "TestFramework.h"

#include <string.h>

#include <Fluxion/Core/Diagnostics/Profiler.h>
#include <Fluxion/Core/Plugin/Manager.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Service/ServiceRegistry.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Core/Startup/SubsystemRegistry.h>

#if FLUXION_PROFILING

static int s_zoneBeginCount = 0;
static int s_zoneEndCount = 0;
static int s_markerCount = 0;
static char s_lastZoneName[64];

// Bounds-safe copy into a fixed buffer, always NUL-terminated -- avoids
// strncpy's "may leave the destination unterminated" footgun without
// pulling in a non-portable *_s variant.
static void Test_CopyZoneName(char* dest, usize destSize, const char* source)
{
    usize length = strlen(source);
    if (length >= destSize) length = destSize - 1;
    memcpy(dest, source, length);
    dest[length] = '\0';
}

static void Test_MockZoneBegin(const FluxionSourceLocation* location, const char* name, void* userData)
{
    FLUXION_UNUSED(location);
    FLUXION_UNUSED(userData);
    ++s_zoneBeginCount;
    Test_CopyZoneName(s_lastZoneName, sizeof(s_lastZoneName), name);
}

static void Test_MockZoneEnd(void* userData)
{
    FLUXION_UNUSED(userData);
    ++s_zoneEndCount;
}

static void Test_MockMarker(const FluxionSourceLocation* location, const char* name, void* userData)
{
    FLUXION_UNUSED(location);
    FLUXION_UNUSED(name);
    FLUXION_UNUSED(userData);
    ++s_markerCount;
}

#endif

void Test_PluginSubsystem_Run(TestContext* ctx)
{
    Fluxion_SubsystemRegistry_Init();
    Fluxion_ServiceRegistry_Init();
    Fluxion_Reflection_Init();
    Fluxion_PluginManager_Init(NULL);

#if FLUXION_PROFILING
    s_zoneBeginCount = 0;
    s_zoneEndCount = 0;
    s_markerCount = 0;
    FluxionProfileBackend mockBackend = { 0 };
    mockBackend.zoneBegin = Test_MockZoneBegin;
    mockBackend.zoneEnd = Test_MockZoneEnd;
    mockBackend.marker = Test_MockMarker;
    Fluxion_Profiler_SetBackend(&mockBackend);
#endif

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

        // The plugin wrapped its type registration in a profiler zone via
        // profilerZoneBegin/profilerMarker/profilerZoneEnd (see
        // PluginSubsystemPlugin.c) -- these should have reached this
        // process's mock backend, proving the Host API's profiler
        // pointers work across the plugin boundary too, not just the
        // registries above.
#if FLUXION_PROFILING
        TEST_CHECK(ctx, s_zoneBeginCount == 1);
        TEST_CHECK(ctx, s_zoneEndCount == 1);
        TEST_CHECK(ctx, s_markerCount == 1);
        TEST_CHECK(ctx, strcmp(s_lastZoneName, "PluginSubsystemPlugin.RegisterReflectedType") == 0);
#endif

        Fluxion_SubsystemRegistry_ShutdownAll();
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(subsystemId));
    }
    Fluxion_PluginManager_Shutdown(); // unloads the plugin -> Fluxion_Plugin_Unload runs
    TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(FLUXION_SERVICE_ID_OF(PluginTestService), 1) == NULL);
    Fluxion_Reflection_Shutdown();
    Fluxion_ServiceRegistry_Shutdown();
    Fluxion_SubsystemRegistry_Shutdown();

#if FLUXION_PROFILING
    Fluxion_Profiler_ClearBackend();
#endif
}
