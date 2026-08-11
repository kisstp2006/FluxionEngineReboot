#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLUXION_PLUGIN_HOST_API_VERSION 1u

typedef struct FluxionPluginHostAPI
{
    u32 apiVersion;
    FluxionAllocator* defaultAllocator;
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
