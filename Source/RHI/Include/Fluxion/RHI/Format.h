#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Starting set (color + depth-stencil + a couple of common
// vertex-attribute formats) -- purely additive to extend: new entries
// never renumber existing ones,
// since the enum values are never persisted (a shader/pipeline cache key
// hashes the whole descriptor, not this integer alone, so a reordering
// would only matter within a single process run).
typedef enum FluxionRHIFormat
{
    FLUXION_RHI_FORMAT_UNKNOWN = 0,

    FLUXION_RHI_FORMAT_R8G8B8A8_UNORM,
    FLUXION_RHI_FORMAT_R8G8B8A8_SRGB,
    FLUXION_RHI_FORMAT_B8G8R8A8_UNORM,
    FLUXION_RHI_FORMAT_B8G8R8A8_SRGB,

    FLUXION_RHI_FORMAT_R32_FLOAT,
    FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT,
    FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT,
    FLUXION_RHI_FORMAT_R32G32B32_FLOAT,
    FLUXION_RHI_FORMAT_R32G32_FLOAT,

    FLUXION_RHI_FORMAT_D32_FLOAT,
    FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT,
} FluxionRHIFormat;

#ifdef __cplusplus
}
#endif
