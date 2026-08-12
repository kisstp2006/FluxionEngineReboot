#include "RenderGraphInternal.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FluxionRenderGraph* Fluxion_RenderGraph_Create(FluxionRHIDeviceHandle device)
{
    // Plain malloc, not the engine allocator -- same convention the RHI's
    // own Null backend uses for its host-side bookkeeping (no allocator
    // handle is threaded through this API, and the struct is a one-shot,
    // long-lived allocation, not a hot-path allocation).
    FluxionRenderGraph* graph = (FluxionRenderGraph*)malloc(sizeof(FluxionRenderGraph));
    FLUXION_ASSERT_MSG(graph != NULL, "RenderGraph: out of memory creating a FluxionRenderGraph");
    if (!graph) return NULL;

    memset(graph, 0, sizeof(*graph));
    graph->device = device;
    return graph;
}

void Fluxion_RenderGraph_Destroy(FluxionRenderGraph* graph)
{
    if (!graph) return;
    free(graph);
}

FluxionRenderGraphPassHandle Fluxion_RenderGraph_AddPassFromRegistry(FluxionRenderGraph* graph, const char* passTypeName, void* userData)
{
    FluxionRenderGraphPassHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL && passTypeName != NULL);

    const FluxionRenderGraphPassType* passType = Fluxion_RenderGraphPassRegistry_Find(passTypeName);
    if (!passType) return invalidHandle;

    FLUXION_ASSERT_MSG(graph->nodeCount < FLUXION_RENDER_GRAPH_MAX_NODES, "RenderGraph: exceeded FLUXION_RENDER_GRAPH_MAX_NODES");
    if (graph->nodeCount >= FLUXION_RENDER_GRAPH_MAX_NODES) return invalidHandle;

    u32 index = graph->nodeCount++;
    FluxionRenderGraphNode* node = &graph->nodes[index];
    memset(node, 0, sizeof(*node));
    node->passType = passType;
    node->userData = userData;

    // Uniquely-enough name for DOT/diagnostics when nothing more specific
    // (e.g. the JSON loader's per-node instance name) was provided.
    snprintf(node->name, sizeof(node->name), "%s#%u", passType->name, index);

    graph->compiled = false; // topology changed -- any prior Compile is stale

    FluxionRenderGraphPassHandle handle = { index, 0 };
    return handle;
}

FluxionRenderGraphTextureHandle Fluxion_RenderGraph_ImportTexture(FluxionRenderGraph* graph, const char* resourceName, FluxionRHITextureHandle texture, FluxionRHIResourceState currentState)
{
    FluxionRenderGraphTextureHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL && resourceName != NULL);

    i32 index = Fluxion_RenderGraphInternal_FindOrCreateTextureSlot(graph, resourceName);
    if (index < 0) return invalidHandle;

    FluxionRenderGraphTextureResource* resource = &graph->textures[(u32)index];
    FLUXION_ASSERT_MSG(!resource->created && !resource->imported, "RenderGraph: ImportTexture called with an already-created-or-imported resource name");
    if (resource->created || resource->imported) return invalidHandle;

    resource->imported = true;
    resource->importedHandle = texture;
    resource->importedInitialState = currentState;

    graph->compiled = false;

    FluxionRenderGraphTextureHandle handle = { (u32)index, 0 };
    return handle;
}

FluxionRenderGraphBufferHandle Fluxion_RenderGraph_ImportBuffer(FluxionRenderGraph* graph, const char* resourceName, FluxionRHIBufferHandle buffer, FluxionRHIResourceState currentState)
{
    FluxionRenderGraphBufferHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL && resourceName != NULL);

    i32 index = Fluxion_RenderGraphInternal_FindOrCreateBufferSlot(graph, resourceName);
    if (index < 0) return invalidHandle;

    FluxionRenderGraphBufferResource* resource = &graph->buffers[(u32)index];
    FLUXION_ASSERT_MSG(!resource->created && !resource->imported, "RenderGraph: ImportBuffer called with an already-created-or-imported resource name");
    if (resource->created || resource->imported) return invalidHandle;

    resource->imported = true;
    resource->importedHandle = buffer;
    resource->importedInitialState = currentState;

    graph->compiled = false;

    FluxionRenderGraphBufferHandle handle = { (u32)index, 0 };
    return handle;
}

bool Fluxion_RenderGraph_Compile(FluxionRenderGraph* graph)
{
    FLUXION_ASSERT(graph != NULL);
    graph->compiled = Fluxion_RenderGraphInternal_Compile(graph);
    return graph->compiled;
}

FluxionRHITextureHandle Fluxion_RenderGraph_ResolveTexture(FluxionRenderGraph* graph, FluxionRenderGraphTextureHandle handle)
{
    FluxionRHITextureHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL);
    if (!FLUXION_HANDLE_IS_VALID(handle) || handle.index >= graph->textureCount) return invalidHandle;
    return graph->textures[handle.index].resolvedHandle;
}

FluxionRHIBufferHandle Fluxion_RenderGraph_ResolveBuffer(FluxionRenderGraph* graph, FluxionRenderGraphBufferHandle handle)
{
    FluxionRHIBufferHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL);
    if (!FLUXION_HANDLE_IS_VALID(handle) || handle.index >= graph->bufferCount) return invalidHandle;
    return graph->buffers[handle.index].resolvedHandle;
}
