#include <Fluxion/Core/Plugin/ABI.h>
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

// Proves a plugin can register a subsystem with the host's Subsystem
// Registry across the plugin boundary (via FluxionPluginHostAPI::
// registerSubsystem), not just call APIs the host already knew about at
// build time.
FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi)
{
    outApi->userData = NULL;

    if (!host->registerSubsystem)
    {
        return false;
    }

    FluxionSubsystemDesc desc;
    desc.id = FLUXION_SUBSYSTEM_ID_OF(PluginSubsystem);
    desc.name = "PluginSubsystem";
    desc.phase = FLUXION_STARTUP_PHASE_RUNTIME;
    desc.dependencies = NULL;
    desc.dependencyCount = 0;
    desc.startup = PluginSubsystem_Startup;
    desc.shutdown = PluginSubsystem_Shutdown;
    desc.userdata = NULL;

    return host->registerSubsystem(&desc);
}

FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api)
{
    FLUXION_UNUSED(api);
}
