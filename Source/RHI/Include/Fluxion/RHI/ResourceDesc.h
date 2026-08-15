#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/Format.h>

#ifdef __cplusplus
extern "C" {
#endif

// Which physical memory a resource should live in -- the backend maps
// this onto its own allocator (e.g. VMA usage flags for Vulkan). Kept
// separate from FluxionRHIResourceState: state is "what a resource is
// being used for right now", memory class is "where its bytes live" and
// doesn't change over the resource's lifetime.
typedef enum FluxionRHIMemoryClass
{
    FLUXION_RHI_MEMORY_CLASS_GPU_ONLY = 0,
    FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU,
    FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU,
    FLUXION_RHI_MEMORY_CLASS_TRANSIENT,
    FLUXION_RHI_MEMORY_CLASS_READBACK,
} FluxionRHIMemoryClass;

#define FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER  ((u32)(1u << 0))
#define FLUXION_RHI_BUFFER_USAGE_INDEX_BUFFER   ((u32)(1u << 1))
#define FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER ((u32)(1u << 2))
#define FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER ((u32)(1u << 3))
#define FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC   ((u32)(1u << 4))
#define FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST   ((u32)(1u << 5))
#define FLUXION_RHI_BUFFER_USAGE_INDIRECT       ((u32)(1u << 6))

typedef struct FluxionRHIBufferDesc
{
    usize size;
    u32 usageFlags; // FLUXION_RHI_BUFFER_USAGE_*
    FluxionRHIMemoryClass memoryClass;
    const char* debugName; // optional, may be NULL
} FluxionRHIBufferDesc;

#define FLUXION_RHI_TEXTURE_USAGE_SAMPLED       ((u32)(1u << 0))
#define FLUXION_RHI_TEXTURE_USAGE_STORAGE       ((u32)(1u << 1))
#define FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET ((u32)(1u << 2))
#define FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL ((u32)(1u << 3))
#define FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC  ((u32)(1u << 4))
#define FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST  ((u32)(1u << 5))

// What shape a texture is, which is not the same question as how big.
//
// A cube map is six square layers that a shader samples by DIRECTION
// rather than by coordinate, and that is a property of the texture, not
// of how many layers it happens to have: six layers of a wall atlas are
// not a cube. So it is said outright.
typedef enum FluxionRHITextureDimension
{
    // Zero, so every description written before this existed still means
    // what it meant.
    FLUXION_RHI_TEXTURE_DIMENSION_2D = 0,

    // Exactly six array layers, in the order every one of these backends
    // agrees on: +X, -X, +Y, -Y, +Z, -Z. One order, written down once,
    // because a face order that differed per backend would give a sky
    // that is merely rotated -- which looks like an artist's mistake
    // rather than a bug.
    FLUXION_RHI_TEXTURE_DIMENSION_CUBE,
} FluxionRHITextureDimension;

#define FLUXION_RHI_CUBE_FACE_COUNT 6

typedef struct FluxionRHITextureDesc
{
    u32 width;
    u32 height;
    u32 depth;        // 1 for a 2D texture
    u32 mipLevels;     // >= 1
    u32 arrayLayers;   // >= 1
    u32 sampleCount;   // 1 = no multisampling
    FluxionRHIFormat format;
    u32 usageFlags;    // FLUXION_RHI_TEXTURE_USAGE_*
    FluxionRHIMemoryClass memoryClass;
    const char* debugName; // optional, may be NULL

    // Added at the end on purpose: several callers build one of these
    // with a positional initializer, and a field in the middle would
    // silently shift every value after it.
    //
    // A CUBE description must have six array layers and equal width and
    // height. Anything else is refused rather than reinterpreted.
    FluxionRHITextureDimension dimension;
} FluxionRHITextureDesc;

typedef enum FluxionRHIFilter
{
    FLUXION_RHI_FILTER_NEAREST = 0,
    FLUXION_RHI_FILTER_LINEAR,
} FluxionRHIFilter;

typedef enum FluxionRHIAddressMode
{
    FLUXION_RHI_ADDRESS_MODE_REPEAT = 0,
    FLUXION_RHI_ADDRESS_MODE_MIRRORED_REPEAT,
    FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE,
    FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_BORDER,
} FluxionRHIAddressMode;

typedef struct FluxionRHISamplerDesc
{
    FluxionRHIFilter minFilter;
    FluxionRHIFilter magFilter;
    FluxionRHIFilter mipFilter;
    FluxionRHIAddressMode addressModeU;
    FluxionRHIAddressMode addressModeV;
    FluxionRHIAddressMode addressModeW;
    f32 maxAnisotropy; // 1.0 = disabled
    const char* debugName; // optional, may be NULL
} FluxionRHISamplerDesc;

#ifdef __cplusplus
}
#endif
