#pragma once

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Raised to 6 when FluxionTypeInfo grew its `methods` span: a reflected
// type now carries callable metadata as well as data members, and a host
// and a plugin that disagree about that disagree about the shape of every
// type they exchange.
#define FLUXION_PLUGIN_HOST_API_VERSION 6u

typedef struct FluxionPluginHostAPI
{
    u32 apiVersion;
    FluxionAllocator* defaultAllocator;

    // Added at version 2. A plugin's Fluxion_Plugin_Load may call this to
    // register a subsystem with the host's Subsystem Registry -- the
    // plugin links against Core headers only, not the Core static lib, so
    // it cannot call Fluxion_SubsystemRegistry_Register directly; this
    // function pointer is how the host exposes that service across the
    // plugin boundary instead.
    bool (*registerSubsystem)(const FluxionSubsystemDesc* desc);

    // Added at version 3, same reasoning as registerSubsystem, now for the
    // Service Registry. If a plugin registers a service, it must
    // unregister it in Fluxion_Plugin_Unload -- the interface pointer
    // lives inside the plugin's own DLL/SO, so it would dangle in the
    // registry once the library is unloaded.
    bool (*registerService)(const void* interfacePointer);
    void (*unregisterService)(FluxionServiceId id);
    const void* (*getService)(FluxionServiceId id, u32 minVersion);

    // Added at version 4, same reasoning again, now for the Reflection
    // Registry. Unlike subsystems/services, reflected types have no
    // per-type unregister (Fluxion_Reflection_Shutdown clears all of
    // them at once) -- a plugin that registers a type must stay loaded
    // for as long as anyone might look that type up.
    bool (*registerType)(const FluxionTypeInfo* typeInfo);
    const FluxionTypeInfo* (*findTypeById)(FluxionTypeId id);

    // Added at version 5. A plugin links against Core headers only, not
    // the Core static lib, so it cannot call Fluxion_Profiler_ZoneBegin/
    // ZoneEnd/Marker directly -- these proxy into whichever profiler
    // backend the host currently has attached (Profiler.h), same
    // reasoning as registerSubsystem/registerService/registerType above.
    void (*profilerZoneBegin)(const FluxionSourceLocation* location, const char* name);
    void (*profilerZoneEnd)(void);
    void (*profilerMarker)(const FluxionSourceLocation* location, const char* name);
} FluxionPluginHostAPI;

typedef struct FluxionPluginAPI
{
    void* userData;
} FluxionPluginAPI;

typedef bool (*FluxionPluginLoadFn)(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi);
typedef void (*FluxionPluginUnloadFn)(FluxionPluginAPI* api);

// Every plugin shared library must export exactly these two C-ABI
// functions (matching FluxionPluginLoadFn/FluxionPluginUnloadFn above),
// e.g.:
//
//   FLUXION_EXPORT bool Fluxion_Plugin_Load(const FluxionPluginHostAPI* host, FluxionPluginAPI* outApi) { ... }
//   FLUXION_EXPORT void Fluxion_Plugin_Unload(FluxionPluginAPI* api) { ... }
#define FLUXION_PLUGIN_LOAD_SYMBOL_NAME   "Fluxion_Plugin_Load"
#define FLUXION_PLUGIN_UNLOAD_SYMBOL_NAME "Fluxion_Plugin_Unload"

#ifdef __cplusplus
}
#endif
