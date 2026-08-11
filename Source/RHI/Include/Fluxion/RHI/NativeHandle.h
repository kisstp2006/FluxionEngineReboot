#pragma once

#include <Fluxion/RHI/Handles.h>

#ifdef __cplusplus
extern "C" {
#endif

// Escape hatch for advanced/plugin code that needs the real backend
// object (e.g. a raw VkDevice) -- deliberately opaque and untyped here.
// What `value` actually points to/holds is backend-specific and NOT part
// of any stable ABI: a plugin using this must know which backend is
// active and accept that the meaning can change between backends or
// engine versions. Ordinary game code never needs this.
typedef struct FluxionRHINativeHandle
{
    void* value;
} FluxionRHINativeHandle;

// Only a representative subset is exposed for now (the objects a real
// backend actually needs to hand out a native handle for so far) -- the
// same one-getter-per-type pattern extends to any other handle type
// exactly when something needs it, rather than pre-generating all 15.
FluxionRHINativeHandle Fluxion_RHI_GetNativeDeviceHandle(FluxionRHIDeviceHandle device);
FluxionRHINativeHandle Fluxion_RHI_GetNativeBufferHandle(FluxionRHIBufferHandle buffer);
FluxionRHINativeHandle Fluxion_RHI_GetNativeTextureHandle(FluxionRHITextureHandle texture);

#ifdef __cplusplus
}
#endif
