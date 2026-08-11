#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Portable, engine-defined resource states -- a backend maps these onto
// its own native concept (e.g. Vulkan image layouts / D3D12 resource
// states), so no backend-specific value ever appears here.
typedef enum FluxionRHIResourceState
{
    FLUXION_RHI_RESOURCE_STATE_UNDEFINED = 0,
    FLUXION_RHI_RESOURCE_STATE_COMMON,

    FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE,
    FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION,

    FLUXION_RHI_RESOURCE_STATE_VERTEX_BUFFER,
    FLUXION_RHI_RESOURCE_STATE_INDEX_BUFFER,
    FLUXION_RHI_RESOURCE_STATE_CONSTANT_BUFFER,

    FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
    FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE,

    FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
    FLUXION_RHI_RESOURCE_STATE_DEPTH_READ,
    FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE,

    FLUXION_RHI_RESOURCE_STATE_PRESENT,
} FluxionRHIResourceState;

#ifdef __cplusplus
}
#endif
