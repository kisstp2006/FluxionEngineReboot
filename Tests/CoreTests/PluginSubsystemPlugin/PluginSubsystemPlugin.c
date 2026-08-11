#include <Fluxion/Core/Plugin/ABI.h>
#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Startup/StartupPhase.h>
#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Result.h>

static FluxionResult PluginSubsystem_Startup(void* userdata)
{
    FLUXION_UNUSED(userdata);
    return Fluxion_ResultOk();
}

static void PluginSubsystem_Shutdown(void* userdata)
{
    FLUXION_UNUSED(userdata);
}

typedef struct PluginTestServiceV1
{
    FluxionServiceHeader header;
    i32 magicValue;
} PluginTestServiceV1;

// The interface pointer handed to Fluxion_ServiceRegistry_Register must
// stay alive for as long as it's registered -- static storage, not a
// stack local, since it outlives the Fluxion_Plugin_Load call.
static PluginTestServiceV1 s_pluginService;

// Fluxion_Plugin_Unload doesn't receive the host pointer (see ABI.h), so
// Load stashes it here for Unload to reach unregisterService -- a real
// plugin caching the host interface across its own lifetime, not just a
// test convenience.
static const FluxionPluginHostAPI* s_host = NULL;

// Proves a plugin can register a subsystem and a service with the host's
// registries across the plugin boundary (via FluxionPluginHostAPI), not
// just call APIs the host already knew about at build time.
FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi)
{
    outApi->userData = NULL;
    s_host = host;

    if (!host->registerSubsystem || !host->registerService)
    {
        return false;
    }

    FluxionSubsystemDesc subsystemDesc;
    subsystemDesc.id = FLUXION_SUBSYSTEM_ID_OF(PluginSubsystem);
    subsystemDesc.name = "PluginSubsystem";
    subsystemDesc.phase = FLUXION_STARTUP_PHASE_RUNTIME;
    subsystemDesc.dependencies = NULL;
    subsystemDesc.dependencyCount = 0;
    subsystemDesc.startup = PluginSubsystem_Startup;
    subsystemDesc.shutdown = PluginSubsystem_Shutdown;
    subsystemDesc.userdata = NULL;

    if (!host->registerSubsystem(&subsystemDesc))
    {
        return false;
    }

    s_pluginService.header.serviceId = FLUXION_SERVICE_ID_OF(PluginTestService);
    s_pluginService.header.version = 1;
    s_pluginService.header.structSize = sizeof(s_pluginService);
    s_pluginService.magicValue = 42;

    return host->registerService(&s_pluginService);
}

FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api)
{
    FLUXION_UNUSED(api);

    // Must unregister before unload finishes -- s_pluginService lives in
    // this DLL/SO's own memory, so it would dangle in the host's registry
    // the moment the library is unmapped.
    if (s_host && s_host->unregisterService)
    {
        s_host->unregisterService(FLUXION_SERVICE_ID_OF(PluginTestService));
    }
}
