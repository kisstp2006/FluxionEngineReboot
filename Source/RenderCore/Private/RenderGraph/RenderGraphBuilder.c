#include "RenderGraphInternal.h"

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

static void Fluxion_RenderGraphInternal_CopyName(char* dest, usize destSize, const char* src)
{
    usize length = strlen(src);
    usize copyLength = length < destSize - 1 ? length : destSize - 1;
    memcpy(dest, src, copyLength);
    dest[copyLength] = '\0';
}

i32 Fluxion_RenderGraphInternal_FindOrCreateTextureSlot(FluxionRenderGraph* graph, const char* name)
{
    for (u32 i = 0; i < graph->textureCount; ++i)
    {
        if (strcmp(graph->textures[i].name, name) == 0)
        {
            return (i32)i;
        }
    }

    if (graph->textureCount >= FLUXION_RENDER_GRAPH_MAX_TEXTURES)
    {
        FLUXION_ASSERT_MSG(false, "RenderGraph: exceeded FLUXION_RENDER_GRAPH_MAX_TEXTURES");
        return -1;
    }

    u32 index = graph->textureCount++;
    FluxionRenderGraphTextureResource* resource = &graph->textures[index];
    memset(resource, 0, sizeof(*resource));
    Fluxion_RenderGraphInternal_CopyName(resource->name, sizeof(resource->name), name);
    resource->registered = true;
    return (i32)index;
}

i32 Fluxion_RenderGraphInternal_FindOrCreateBufferSlot(FluxionRenderGraph* graph, const char* name)
{
    for (u32 i = 0; i < graph->bufferCount; ++i)
    {
        if (strcmp(graph->buffers[i].name, name) == 0)
        {
            return (i32)i;
        }
    }

    if (graph->bufferCount >= FLUXION_RENDER_GRAPH_MAX_BUFFERS)
    {
        FLUXION_ASSERT_MSG(false, "RenderGraph: exceeded FLUXION_RENDER_GRAPH_MAX_BUFFERS");
        return -1;
    }

    u32 index = graph->bufferCount++;
    FluxionRenderGraphBufferResource* resource = &graph->buffers[index];
    memset(resource, 0, sizeof(*resource));
    Fluxion_RenderGraphInternal_CopyName(resource->name, sizeof(resource->name), name);
    resource->registered = true;
    return (i32)index;
}

void Fluxion_RenderGraphInternal_RecordUsage(FluxionRenderGraph* graph, u32 nodeIndex, FluxionRenderGraphResourceKind kind, u32 resourceIndex, FluxionRenderGraphResourceUsage usage)
{
    FLUXION_ASSERT_MSG(graph->usageCount < FLUXION_RENDER_GRAPH_MAX_USAGES, "RenderGraph: exceeded FLUXION_RENDER_GRAPH_MAX_USAGES");
    if (graph->usageCount >= FLUXION_RENDER_GRAPH_MAX_USAGES) return;

    FluxionRenderGraphUsageRecord* record = &graph->usages[graph->usageCount++];
    record->nodeIndex = nodeIndex;
    record->kind = kind;
    record->resourceIndex = resourceIndex;
    record->usage = usage;
}

FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_CreateTexture(FluxionRenderGraphBuilder* builder, const char* resourceName, const FluxionRHITextureDesc* desc)
{
    FluxionRenderGraphTextureHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(builder != NULL && resourceName != NULL && desc != NULL);

    i32 index = Fluxion_RenderGraphInternal_FindOrCreateTextureSlot(builder->graph, resourceName);
    if (index < 0) { builder->hadError = true; return invalidHandle; }

    FluxionRenderGraphTextureResource* resource = &builder->graph->textures[(u32)index];
    FLUXION_ASSERT_MSG(!resource->created && !resource->imported, "RenderGraph: CreateTexture called with an already-created-or-imported resource name");
    if (resource->created || resource->imported) { builder->hadError = true; return invalidHandle; }

    resource->created = true;
    resource->desc = *desc;

    Fluxion_RenderGraphInternal_RecordUsage(builder->graph, builder->nodeIndex, FLUXION_RENDER_GRAPH_RESOURCE_TEXTURE, (u32)index, FLUXION_RENDER_GRAPH_USAGE_CREATE);

    FluxionRenderGraphTextureHandle handle = { (u32)index, 0 };
    return handle;
}

FluxionRenderGraphBufferHandle Fluxion_RenderGraphBuilder_CreateBuffer(FluxionRenderGraphBuilder* builder, const char* resourceName, const FluxionRHIBufferDesc* desc)
{
    FluxionRenderGraphBufferHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(builder != NULL && resourceName != NULL && desc != NULL);

    i32 index = Fluxion_RenderGraphInternal_FindOrCreateBufferSlot(builder->graph, resourceName);
    if (index < 0) { builder->hadError = true; return invalidHandle; }

    FluxionRenderGraphBufferResource* resource = &builder->graph->buffers[(u32)index];
    FLUXION_ASSERT_MSG(!resource->created && !resource->imported, "RenderGraph: CreateBuffer called with an already-created-or-imported resource name");
    if (resource->created || resource->imported) { builder->hadError = true; return invalidHandle; }

    resource->created = true;
    resource->desc = *desc;

    Fluxion_RenderGraphInternal_RecordUsage(builder->graph, builder->nodeIndex, FLUXION_RENDER_GRAPH_RESOURCE_BUFFER, (u32)index, FLUXION_RENDER_GRAPH_USAGE_CREATE);

    FluxionRenderGraphBufferHandle handle = { (u32)index, 0 };
    return handle;
}

static FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_TextureUsage(FluxionRenderGraphBuilder* builder, const char* resourceName, FluxionRenderGraphResourceUsage usage)
{
    FluxionRenderGraphTextureHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(builder != NULL && resourceName != NULL);

    i32 index = Fluxion_RenderGraphInternal_FindOrCreateTextureSlot(builder->graph, resourceName);
    if (index < 0) { builder->hadError = true; return invalidHandle; }

    Fluxion_RenderGraphInternal_RecordUsage(builder->graph, builder->nodeIndex, FLUXION_RENDER_GRAPH_RESOURCE_TEXTURE, (u32)index, usage);

    FluxionRenderGraphTextureHandle handle = { (u32)index, 0 };
    return handle;
}

static FluxionRenderGraphBufferHandle Fluxion_RenderGraphBuilder_BufferUsage(FluxionRenderGraphBuilder* builder, const char* resourceName, FluxionRenderGraphResourceUsage usage)
{
    FluxionRenderGraphBufferHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(builder != NULL && resourceName != NULL);

    i32 index = Fluxion_RenderGraphInternal_FindOrCreateBufferSlot(builder->graph, resourceName);
    if (index < 0) { builder->hadError = true; return invalidHandle; }

    Fluxion_RenderGraphInternal_RecordUsage(builder->graph, builder->nodeIndex, FLUXION_RENDER_GRAPH_RESOURCE_BUFFER, (u32)index, usage);

    FluxionRenderGraphBufferHandle handle = { (u32)index, 0 };
    return handle;
}

FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_ReadTexture(FluxionRenderGraphBuilder* builder, const char* resourceName)
{
    return Fluxion_RenderGraphBuilder_TextureUsage(builder, resourceName, FLUXION_RENDER_GRAPH_USAGE_READ);
}

FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_WriteTexture(FluxionRenderGraphBuilder* builder, const char* resourceName)
{
    return Fluxion_RenderGraphBuilder_TextureUsage(builder, resourceName, FLUXION_RENDER_GRAPH_USAGE_WRITE);
}

FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_WriteColorTarget(FluxionRenderGraphBuilder* builder, const char* resourceName)
{
    return Fluxion_RenderGraphBuilder_TextureUsage(builder, resourceName, FLUXION_RENDER_GRAPH_USAGE_WRITE_COLOR_TARGET);
}

FluxionRenderGraphTextureHandle Fluxion_RenderGraphBuilder_WriteDepthTarget(FluxionRenderGraphBuilder* builder, const char* resourceName)
{
    return Fluxion_RenderGraphBuilder_TextureUsage(builder, resourceName, FLUXION_RENDER_GRAPH_USAGE_WRITE_DEPTH_TARGET);
}

FluxionRenderGraphBufferHandle Fluxion_RenderGraphBuilder_ReadBuffer(FluxionRenderGraphBuilder* builder, const char* resourceName)
{
    return Fluxion_RenderGraphBuilder_BufferUsage(builder, resourceName, FLUXION_RENDER_GRAPH_USAGE_READ);
}

FluxionRenderGraphBufferHandle Fluxion_RenderGraphBuilder_WriteBuffer(FluxionRenderGraphBuilder* builder, const char* resourceName)
{
    return Fluxion_RenderGraphBuilder_BufferUsage(builder, resourceName, FLUXION_RENDER_GRAPH_USAGE_WRITE);
}
