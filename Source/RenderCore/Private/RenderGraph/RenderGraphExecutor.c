#include "RenderGraphInternal.h"

#include <Fluxion/Foundation/Assert.h>

void Fluxion_RenderGraph_Execute(FluxionRenderGraph* graph, FluxionRHICommandListHandle commandList)
{
    FLUXION_ASSERT(graph != NULL);
    FLUXION_ASSERT_MSG(graph->compiled, "Fluxion_RenderGraph_Execute called on a graph that has not been successfully Compiled");
    if (!graph->compiled) return;

    // An imported resource's RHI handle is already resolved -- it exists
    // for the whole graph's lifetime, not just one Execute call.
    for (u32 i = 0; i < graph->textureCount; ++i)
    {
        if (graph->textures[i].imported)
        {
            graph->textures[i].resolvedHandle = graph->textures[i].importedHandle;
        }
    }
    for (u32 i = 0; i < graph->bufferCount; ++i)
    {
        if (graph->buffers[i].imported)
        {
            graph->buffers[i].resolvedHandle = graph->buffers[i].importedHandle;
        }
    }

    for (u32 orderPos = 0; orderPos < graph->nodeCount; ++orderPos)
    {
        u32 nodeIndex = graph->executionOrder[orderPos];
        FluxionRenderGraphNode* node = &graph->nodes[nodeIndex];

        // A culled node never owns a resource's create/destroy point or
        // any barrier (both are only ever derived from a surviving,
        // non-culled usage) -- skipping it entirely here is safe.
        if (node->culled) continue;

        // Create this frame's transient resources whose lifetime starts here.
        for (u32 t = 0; t < graph->textureCount; ++t)
        {
            if (!graph->textures[t].imported && graph->createTextureAtNode[t] == nodeIndex)
            {
                graph->textures[t].resolvedHandle = Fluxion_RHI_CreateTexture(graph->device, &graph->textures[t].desc);
            }
        }
        for (u32 b = 0; b < graph->bufferCount; ++b)
        {
            if (!graph->buffers[b].imported && graph->createBufferAtNode[b] == nodeIndex)
            {
                graph->buffers[b].resolvedHandle = Fluxion_RHI_CreateBuffer(graph->device, &graph->buffers[b].desc);
            }
        }

        // Issue the barriers the compiler decided this node needs, one at
        // a time -- resolving each plan's graph resource reference to the
        // real, now-valid RHI handle right before use.
        FluxionRHITextureHandle invalidTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        FluxionRHIBufferHandle invalidBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        u32 barrierStart = graph->barrierStartForNode[nodeIndex];
        u32 barrierCount = graph->barrierCountForNode[nodeIndex];
        for (u32 i = 0; i < barrierCount; ++i)
        {
            const FluxionRenderGraphBarrierPlan* plan = &graph->barriers[barrierStart + i];

            FluxionRHIBarrier barrier;
            barrier.texture = invalidTexture;
            barrier.buffer = invalidBuffer;
            barrier.before = plan->before;
            barrier.after = plan->after;
            if (plan->kind == FLUXION_RENDER_GRAPH_RESOURCE_TEXTURE)
            {
                barrier.texture = graph->textures[plan->resourceIndex].resolvedHandle;
            }
            else
            {
                barrier.buffer = graph->buffers[plan->resourceIndex].resolvedHandle;
            }

            Fluxion_RHI_CommandList_Barrier(commandList, &barrier, 1);
        }

        node->passType->execute(commandList, node->userData);

        // Destroy this frame's transient resources whose lifetime ends here.
        for (u32 t = 0; t < graph->textureCount; ++t)
        {
            if (!graph->textures[t].imported && graph->destroyTextureAtNode[t] == nodeIndex)
            {
                Fluxion_RHI_DestroyTexture(graph->textures[t].resolvedHandle);
                graph->textures[t].resolvedHandle = invalidTexture;
            }
        }
        for (u32 b = 0; b < graph->bufferCount; ++b)
        {
            if (!graph->buffers[b].imported && graph->destroyBufferAtNode[b] == nodeIndex)
            {
                Fluxion_RHI_DestroyBuffer(graph->buffers[b].resolvedHandle);
                graph->buffers[b].resolvedHandle = invalidBuffer;
            }
        }
    }
}
