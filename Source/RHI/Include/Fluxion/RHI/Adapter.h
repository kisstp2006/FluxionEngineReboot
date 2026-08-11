#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/Capabilities.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionRHIAdapterType
{
    FLUXION_RHI_ADAPTER_TYPE_OTHER = 0,
    FLUXION_RHI_ADAPTER_TYPE_DISCRETE,
    FLUXION_RHI_ADAPTER_TYPE_INTEGRATED,
    FLUXION_RHI_ADAPTER_TYPE_VIRTUAL,
    FLUXION_RHI_ADAPTER_TYPE_CPU,
} FluxionRHIAdapterType;

#define FLUXION_RHI_ADAPTER_MAX_NAME_LENGTH 255

// Queried once via Fluxion_RHI_GetAdapterInfo and treated as read-only
// afterward -- the caller decides which adapter to open a device on
// based on this, rather than the engine assuming "adapter 0".
typedef struct FluxionRHIAdapterInfo
{
    char name[FLUXION_RHI_ADAPTER_MAX_NAME_LENGTH + 1];
    u32 vendorId;
    u32 deviceId;
    FluxionRHIAdapterType type;

    usize dedicatedVRAM;
    usize sharedMemory;

    u32 driverVersion;
    u32 apiVersion;

    FluxionRHICapabilityFlags capabilities;
    FluxionRHILimits limits;
} FluxionRHIAdapterInfo;

#ifdef __cplusplus
}
#endif
