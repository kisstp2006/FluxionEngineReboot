#pragma once

#include <Fluxion/Core/Plugin/ABI.h>
#include <Fluxion/Core/Plugin/Descriptor.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

void Fluxion_PluginManager_Init(FluxionAllocator* allocator);

// Unloads everything still loaded, in reverse load order.
void Fluxion_PluginManager_Shutdown(void);

// Reads each `.plugin` descriptor file (JSON — name, library, version,
// type, dependencies), resolves load order via a dependency-graph
// topological sort, then loads each plugin's shared library (expected
// next to its .plugin file) in that order and calls its
// Fluxion_Plugin_Load. Returns false — and loads nothing from this call —
// if a descriptor is malformed, a dependency is missing, or a dependency
// cycle is detected.
bool Fluxion_PluginManager_LoadAll(const char* const* pluginFilePaths, usize count);

usize Fluxion_PluginManager_GetLoadedCount(void);

// Both indexed in load order (index 0 loaded first, across all
// successful LoadAll calls so far).
const FluxionPluginDescriptor* Fluxion_PluginManager_GetLoadedDescriptor(usize index);
const FluxionPluginAPI* Fluxion_PluginManager_GetLoadedApi(usize index);

#ifdef __cplusplus
}
#endif
