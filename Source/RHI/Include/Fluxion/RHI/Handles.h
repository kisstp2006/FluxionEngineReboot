#pragma once

#include <Fluxion/Foundation/Handle.h>

// One line per RHI object type -- every handle is a distinct nominal
// index+generation struct (FLUXION_DEFINE_HANDLE), so e.g. a
// FluxionRHIBufferHandle and a FluxionRHITextureHandle can never be
// accidentally interchanged even though both are structurally identical.
// Kept in one file rather than one-header-per-handle (the convention used
// for a single handle like FluxionJobHandle/FluxionWindowHandle) because
// these 15 all belong to the same module and are always used together.

FLUXION_DEFINE_HANDLE(FluxionRHIInstanceHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIAdapterHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIDeviceHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIQueueHandle);
FLUXION_DEFINE_HANDLE(FluxionRHICommandListHandle);
FLUXION_DEFINE_HANDLE(FluxionRHISwapchainHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIBufferHandle);
FLUXION_DEFINE_HANDLE(FluxionRHITextureHandle);
FLUXION_DEFINE_HANDLE(FluxionRHITextureViewHandle);
FLUXION_DEFINE_HANDLE(FluxionRHISamplerHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIShaderHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIPipelineHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIFenceHandle);
FLUXION_DEFINE_HANDLE(FluxionRHISemaphoreHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIQueryPoolHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIBindGroupLayoutHandle);
FLUXION_DEFINE_HANDLE(FluxionRHIBindGroupHandle);
