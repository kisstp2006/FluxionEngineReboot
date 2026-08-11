#include <Fluxion/Core/Plugin/ABI.h>
#include <Fluxion/Core/Reflection/PropertyFlags.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Startup/StartupPhase.h>
#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Containers/StringView.h>
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

typedef struct PluginTestReflectedType
{
    f32 value;
} PluginTestReflectedType;

// Same lifetime requirement as s_pluginService -- the Reflection Registry
// only stores the pointer (Registry.h), and has no per-type unregister,
// so this must outlive the whole time the plugin stays loaded.
static FluxionPropertyInfo s_pluginTypeProperties[1];
static FluxionTypeInfo s_pluginTypeInfo;

// Fluxion_Plugin_Unload doesn't receive the host pointer (see ABI.h), so
// Load stashes it here for Unload to reach unregisterService -- a real
// plugin caching the host interface across its own lifetime, not just a
// test convenience.
static const FluxionPluginHostAPI* s_host = NULL;

// Proves a plugin can register a subsystem, a service, and a reflected
// type with the host's registries across the plugin boundary (via
// FluxionPluginHostAPI), not just call APIs the host already knew about
// at build time.
FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi)
{
    outApi->userData = NULL;
    s_host = host;

    if (!host->registerSubsystem || !host->registerService || !host->registerType)
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

    if (!host->registerService(&s_pluginService))
    {
        return false;
    }

    // FLUXION_TYPE_ID_OF hashes at runtime, so this can't be a file-scope
    // static initializer -- assigned here instead, as a C99 compound
    // literal (this file is always compiled as C, so unlike the other
    // FLUXION_REFLECT_PROPERTY call sites this doesn't need to also stay
    // valid C++ syntax) into static storage, since the Reflection
    // Registry only stores the pointer and must still be able to read it
    // long after Fluxion_Plugin_Load returns.
    s_pluginTypeProperties[0] = (FluxionPropertyInfo)FLUXION_REFLECT_PROPERTY(PluginTestReflectedType, value, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE);

    s_pluginTypeInfo.name = Fluxion_StringView_FromCStr("PluginTestReflectedType");
    s_pluginTypeInfo.id = FLUXION_TYPE_ID_OF(PluginTestReflectedType);
    s_pluginTypeInfo.kind = FLUXION_TYPE_KIND_STRUCT;
    s_pluginTypeInfo.size = sizeof(PluginTestReflectedType);
    s_pluginTypeInfo.version = 1;
    s_pluginTypeInfo.members = Fluxion_Span_Make(s_pluginTypeProperties, FLUXION_ARRAY_COUNT(s_pluginTypeProperties), sizeof(FluxionPropertyInfo));

    return host->registerType(&s_pluginTypeInfo);
}

FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api)
{
    FLUXION_UNUSED(api);

    // Must unregister before unload finishes -- s_pluginService lives in
    // this DLL/SO's own memory, so it would dangle in the host's registry
    // the moment the library is unmapped. The reflected type has no
    // per-type unregister (see ABI.h) -- the test that loads this plugin
    // only looks types up while it's still loaded.
    if (s_host && s_host->unregisterService)
    {
        s_host->unregisterService(FLUXION_SERVICE_ID_OF(PluginTestService));
    }
}
