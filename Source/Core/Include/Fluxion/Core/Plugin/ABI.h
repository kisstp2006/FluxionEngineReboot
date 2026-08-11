#pragma once

#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLUXION_PLUGIN_HOST_API_VERSION 2u

typedef struct FluxionPluginHostAPI
{
    u32 apiVersion;
    FluxionAllocator* defaultAllocator;

    // Added at version 2. A plugin's Fluxion_Plugin_Load may call this to
    // register a subsystem with the host's Subsystem Registry -- the
    // plugin links against Core headers only, not the Core static lib, so
    // it cannot call Fluxion_SubsystemRegistry_Register directly; this
    // function pointer is how the host exposes that service across the
    // plugin boundary instead. A minimal, additive precursor to a full
    // Service Registry, not a replacement for one.
    bool (*registerSubsystem)(const FluxionSubsystemDesc* desc);
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
