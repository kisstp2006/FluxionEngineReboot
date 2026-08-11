#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// A bitmask, not a struct-of-bools -- cheap to copy/compare/intersect
// (`caps & FLUXION_RHI_CAPABILITY_BINDLESS_RESOURCES`), and avoids the
// padding a bool-per-field struct would carry. u64 leaves room to grow
// past the current 18 flags without a breaking change.
typedef u64 FluxionRHICapabilityFlags;

#define FLUXION_RHI_CAPABILITY_NONE                    ((FluxionRHICapabilityFlags)0)
#define FLUXION_RHI_CAPABILITY_BINDLESS_RESOURCES       ((FluxionRHICapabilityFlags)(1ull << 0))
#define FLUXION_RHI_CAPABILITY_DESCRIPTOR_INDEXING      ((FluxionRHICapabilityFlags)(1ull << 1))
#define FLUXION_RHI_CAPABILITY_MESH_SHADERS             ((FluxionRHICapabilityFlags)(1ull << 2))
#define FLUXION_RHI_CAPABILITY_RAY_TRACING              ((FluxionRHICapabilityFlags)(1ull << 3))
#define FLUXION_RHI_CAPABILITY_RAY_QUERY                ((FluxionRHICapabilityFlags)(1ull << 4))
#define FLUXION_RHI_CAPABILITY_VARIABLE_RATE_SHADING    ((FluxionRHICapabilityFlags)(1ull << 5))
#define FLUXION_RHI_CAPABILITY_ASYNC_COMPUTE            ((FluxionRHICapabilityFlags)(1ull << 6))
#define FLUXION_RHI_CAPABILITY_ASYNC_TRANSFER           ((FluxionRHICapabilityFlags)(1ull << 7))
#define FLUXION_RHI_CAPABILITY_BUFFER_DEVICE_ADDRESS    ((FluxionRHICapabilityFlags)(1ull << 8))
#define FLUXION_RHI_CAPABILITY_INDIRECT_COUNT           ((FluxionRHICapabilityFlags)(1ull << 9))
#define FLUXION_RHI_CAPABILITY_TIMELINE_SYNC            ((FluxionRHICapabilityFlags)(1ull << 10))
#define FLUXION_RHI_CAPABILITY_COMPUTE_SHADERS          ((FluxionRHICapabilityFlags)(1ull << 11))
#define FLUXION_RHI_CAPABILITY_TESSELLATION             ((FluxionRHICapabilityFlags)(1ull << 12))
#define FLUXION_RHI_CAPABILITY_GEOMETRY_SHADERS         ((FluxionRHICapabilityFlags)(1ull << 13))
#define FLUXION_RHI_CAPABILITY_FP16                     ((FluxionRHICapabilityFlags)(1ull << 14))
#define FLUXION_RHI_CAPABILITY_INT64                    ((FluxionRHICapabilityFlags)(1ull << 15))
#define FLUXION_RHI_CAPABILITY_WAVE_OPERATIONS          ((FluxionRHICapabilityFlags)(1ull << 16))
#define FLUXION_RHI_CAPABILITY_SPARSE_RESOURCES         ((FluxionRHICapabilityFlags)(1ull << 17))

// Baseline numeric device limits -- queried once per device, read-only
// after that (same access pattern as FluxionRHIAdapterInfo).
typedef struct FluxionRHILimits
{
    u32 maxTextureDimension1D;
    u32 maxTextureDimension2D;
    u32 maxTextureDimension3D;
    u32 maxTextureArrayLayers;
    u32 maxColorAttachments;
    u32 maxBoundDescriptorSets;
    u32 maxPushConstantSize;
    u32 minUniformBufferOffsetAlignment;
    u32 maxComputeWorkGroupSize[3];
    u32 maxComputeWorkGroupInvocations;
} FluxionRHILimits;

#ifdef __cplusplus
}
#endif
