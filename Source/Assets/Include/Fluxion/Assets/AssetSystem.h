#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starting and stopping the whole of it, in the right order, once.
//
// The pieces below can be started separately, and the tests do exactly
// that so each can be examined on its own. Everything else should use
// this: the parts have an order, and half of them started is not a state
// anything handles.

// Starts the file system, the type registry, the database and the loader,
// and publishes the service a plugin uses to add its own types.
//
// THE SERVICE REGISTRY MUST ALREADY BE RUNNING. Publishing the service is
// not optional here -- skipping it quietly would leave a build where
// plugins simply cannot add asset types, and nothing would say so.
//
// False if any part refuses to start; nothing is left running in that
// case.
bool Fluxion_AssetSystem_Init(FluxionAllocator* allocator);

// Stops them in the reverse order, releasing every held asset and
// destroying every mounted source.
void Fluxion_AssetSystem_Shutdown(void);

bool Fluxion_AssetSystem_IsInitialized(void);

#ifdef __cplusplus
}
#endif
