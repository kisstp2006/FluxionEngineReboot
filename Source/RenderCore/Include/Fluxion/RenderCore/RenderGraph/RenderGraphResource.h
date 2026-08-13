#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/ResourceDesc.h>

#ifdef __cplusplus
extern "C" {
#endif

// How a pass's Setup callback declares it touches a resource. CREATE and
// IMPORT both introduce a resource into the graph, but only CREATE is
// something a pass declares for itself -- IMPORT is a graph-level
// operation (Fluxion_RenderGraph_ImportTexture/ImportBuffer, called before
// Compile, not from inside a Setup callback) since an imported resource
// (e.g. the swapchain backbuffer) already exists before any pass runs and
// is shared by the whole graph, not owned by one pass. This enum still
// carries FLUXION_RENDER_GRAPH_USAGE_IMPORT so the same type can describe
// "how was this resource introduced" uniformly wherever that's useful,
// even though no per-pass usage record is ever recorded with it.
typedef enum FluxionRenderGraphResourceUsage
{
    FLUXION_RENDER_GRAPH_USAGE_READ,
    FLUXION_RENDER_GRAPH_USAGE_WRITE,
    FLUXION_RENDER_GRAPH_USAGE_CREATE,
    FLUXION_RENDER_GRAPH_USAGE_IMPORT,
    // Texture-only refinements of WRITE, for a pass that writes a texture
    // by binding it as a color/depth attachment (Fluxion_RHI_RESOURCE_STATE_
    // RENDER_TARGET/DEPTH_WRITE) rather than as a compute/UAV storage image
    // (FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE, what plain WRITE still maps
    // to) -- the two need different VkImageLayouts/D3D12 resource states,
    // and a generic WRITE has no way to say which one a pass meant.
    FLUXION_RENDER_GRAPH_USAGE_WRITE_COLOR_TARGET,
    FLUXION_RENDER_GRAPH_USAGE_WRITE_DEPTH_TARGET,
} FluxionRenderGraphResourceUsage;

// Thin wrappers around the RHI descs -- kept distinct from
// FluxionRHITextureDesc/FluxionRHIBufferDesc so a future graph-only field
// (e.g. a size-relative-to-viewport policy) has somewhere to live without
// disturbing the RHI's own desc structs. Nothing beyond the RHI desc is
// needed yet, so these stay a one-member wrapper -- the graph-side
// resource name used for pin/edge matching and DOT labels is a separate
// `resourceName` argument on every builder call, not a struct field, since
// it's supplied once per Create/Read/Write call rather than living on the
// desc itself.
typedef struct FluxionRenderGraphTextureDesc
{
    FluxionRHITextureDesc rhiDesc;
} FluxionRenderGraphTextureDesc;

typedef struct FluxionRenderGraphBufferDesc
{
    FluxionRHIBufferDesc rhiDesc;
} FluxionRenderGraphBufferDesc;

#ifdef __cplusplus
}
#endif
