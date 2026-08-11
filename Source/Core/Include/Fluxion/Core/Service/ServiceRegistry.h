#pragma once

#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

void Fluxion_ServiceRegistry_Init(void);
void Fluxion_ServiceRegistry_Shutdown(void);

// interfacePointer must point at a struct whose first member is a
// FluxionServiceHeader, already filled in (serviceId/version/structSize).
// The registry does not copy or own the pointed-to memory -- the caller
// must keep it alive (and call Unregister) for as long as it stays
// registered. Returns false if the registry is full or serviceId is
// already registered.
bool Fluxion_ServiceRegistry_Register(const void* interfacePointer);

void Fluxion_ServiceRegistry_Unregister(FluxionServiceId id);

// Returns the interface pointer last registered for id, or NULL if it
// isn't registered or its registered version is below minVersion.
const void* Fluxion_ServiceRegistry_Get(FluxionServiceId id, u32 minVersion);

#ifdef __cplusplus
}
#endif
