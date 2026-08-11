#pragma once

#include <Fluxion/Core/Service/ServiceId.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bounds the registry's fixed-capacity storage (ServiceRegistry.c) --
// zero heap allocation, same rationale as FLUXION_MAX_SUBSYSTEMS.
#define FLUXION_MAX_SERVICES 64

// Every interface struct registered with the Service Registry must embed
// this as its first member -- Fluxion_ServiceRegistry_Register reads it
// straight out of *interfacePointer, so its offset must be 0. `version`
// lets a later engine build introduce e.g. MemoryService v2 without
// breaking plugins still asking for v1 (Fluxion_ServiceRegistry_Get takes
// a minimum version, not an exact one).
typedef struct FluxionServiceHeader
{
    FluxionServiceId serviceId;
    u32 version;
    u32 structSize;
} FluxionServiceHeader;

#ifdef __cplusplus
}
#endif
