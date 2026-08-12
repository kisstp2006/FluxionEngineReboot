// C++ (not C), same reasoning as the RHI backends' own Private/Vulkan and
// Private/D3D12 .cpp files: std::vector/std::set make a DFS-based
// dependency graph (topological sort + cycle detection + reachability)
// far less error-prone to write than the equivalent in raw C, while the
// rest of this module stays plain C.

extern "C"
{
#include "RenderGraphInternal.h"
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
}

#include <algorithm>
#include <set>
#include <vector>

namespace
{

// v1 simplification: this graph only distinguishes READ/WRITE, not the
// specific kind of write (render-target vs. compute-UAV vs. copy
// destination) or read (sampled vs. vertex/index/constant buffer) a pass
// actually performs -- so every WRITE maps to the same
// FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE and every READ to
// FLUXION_RHI_RESOURCE_STATE_SHADER_READ, regardless of what the pass is
// really doing with the resource. A future revision that lets a pass
// declare a more specific usage kind (e.g. WriteRenderTarget vs.
// WriteStorage) can refine this mapping without changing the barrier-
// generation algorithm itself.
FluxionRHIResourceState MapUsageToState(FluxionRenderGraphResourceUsage usage)
{
    switch (usage)
    {
        case FLUXION_RENDER_GRAPH_USAGE_CREATE: return FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
        case FLUXION_RENDER_GRAPH_USAGE_READ: return FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
        case FLUXION_RENDER_GRAPH_USAGE_WRITE: return FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE;
        case FLUXION_RENDER_GRAPH_USAGE_IMPORT: return FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    }
    return FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
}

struct UsageRef
{
    u32 declIndex;  // index into graph->usages[], used as a stable tie-break
    u32 nodeIndex;
    FluxionRenderGraphResourceUsage usage;
};

std::vector<UsageRef> GatherUsages(const FluxionRenderGraph* graph, FluxionRenderGraphResourceKind kind, u32 resourceIndex)
{
    std::vector<UsageRef> result;
    for (u32 i = 0; i < graph->usageCount; ++i)
    {
        const FluxionRenderGraphUsageRecord& record = graph->usages[i];
        if (record.kind == kind && record.resourceIndex == resourceIndex)
        {
            result.push_back(UsageRef{ i, record.nodeIndex, record.usage });
        }
    }
    return result;
}

bool IsProducerUsage(FluxionRenderGraphResourceUsage usage)
{
    return usage == FLUXION_RENDER_GRAPH_USAGE_CREATE || usage == FLUXION_RENDER_GRAPH_USAGE_WRITE;
}

// Deliberately NOT "READ || WRITE" -- a WRITE is already a producer (see
// IsProducerUsage above), and ordering between two producers of the same
// resource is handled separately, by the producer-chain loop below. If a
// WRITE usage were also treated as a "pure" consumer here, the producer x
// consumer cross-product two functions down would connect every pair of
// writers in BOTH directions (producer(A)->consumer(B) AND
// producer(B)->consumer(A), since a second writer B appears in both
// lists) -- i.e. any resource with two or more writers would always
// contain a manufactured 2-cycle between them and Compile would always
// (incorrectly) reject the graph. Only a READ is a "pure" consumer here.
bool IsPureConsumerUsage(FluxionRenderGraphResourceUsage usage)
{
    return usage == FLUXION_RENDER_GRAPH_USAGE_READ;
}

// Adds every (producer -> reader) edge implied by one resource's usage
// records, plus a chain among the resource's own producers (in node-
// addition order) to keep multiple writes to the same resource well
// ordered -- a documented v1 simplification: real "resource versioning"
// (each write introducing a fresh version downstream reads bind to) is not
// implemented, multiple writers are just serialized in addition order,
// and every reader conservatively depends on every producer instead of
// only the most recent one.
void AddResourceEdges(const std::vector<UsageRef>& usages, std::set<std::pair<u32, u32>>& edges)
{
    std::vector<u32> producers;
    std::vector<u32> readers;
    for (const UsageRef& usageRef : usages)
    {
        if (IsProducerUsage(usageRef.usage)) producers.push_back(usageRef.nodeIndex);
        if (IsPureConsumerUsage(usageRef.usage)) readers.push_back(usageRef.nodeIndex);
    }

    for (u32 producer : producers)
    {
        for (u32 reader : readers)
        {
            if (producer != reader)
            {
                edges.insert({ producer, reader });
            }
        }
    }

    for (usize i = 0; i + 1 < producers.size(); ++i)
    {
        if (producers[i] != producers[i + 1])
        {
            edges.insert({ producers[i], producers[i + 1] });
        }
    }
}

} // namespace

extern "C" bool Fluxion_RenderGraphInternal_Compile(FluxionRenderGraph* graph)
{
    FLUXION_ASSERT(graph != NULL);

    const u32 nodeCount = graph->nodeCount;

    // --- Reset per-compile state -------------------------------------------
    //
    // Compile is safe to call more than once on the same graph (e.g. once
    // per frame if the caller rebuilds topology first) -- every node's
    // Setup callback re-runs from scratch, so any state Setup itself
    // populates (usage records, a transient resource's "created" flag)
    // must be cleared first, or a second Compile call would immediately
    // fail with "already created". Imported resources are untouched here:
    // Import is a graph-level call outside Setup, made once and expected
    // to persist across recompiles.
    graph->usageCount = 0;
    for (u32 i = 0; i < graph->textureCount; ++i)
    {
        if (!graph->textures[i].imported) graph->textures[i].created = false;
        graph->textures[i].resolvedHandle = FluxionRHITextureHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    for (u32 i = 0; i < graph->bufferCount; ++i)
    {
        if (!graph->buffers[i].imported) graph->buffers[i].created = false;
        graph->buffers[i].resolvedHandle = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    for (u32 i = 0; i < nodeCount; ++i)
    {
        graph->nodes[i].culled = false;
    }
    graph->barrierCount = 0;
    graph->edgeCount = 0;
    for (u32 i = 0; i < nodeCount; ++i)
    {
        graph->barrierStartForNode[i] = 0;
        graph->barrierCountForNode[i] = 0;
    }
    for (u32 i = 0; i < graph->textureCount; ++i)
    {
        graph->createTextureAtNode[i] = FLUXION_RENDER_GRAPH_INVALID_NODE_INDEX;
        graph->destroyTextureAtNode[i] = FLUXION_RENDER_GRAPH_INVALID_NODE_INDEX;
    }
    for (u32 i = 0; i < graph->bufferCount; ++i)
    {
        graph->createBufferAtNode[i] = FLUXION_RENDER_GRAPH_INVALID_NODE_INDEX;
        graph->destroyBufferAtNode[i] = FLUXION_RENDER_GRAPH_INVALID_NODE_INDEX;
    }

    if (nodeCount == 0)
    {
        return true; // an empty graph trivially compiles to nothing
    }

    // --- Setup phase --------------------------------------------------------
    //
    // Every node's Setup callback declares its resource reads/writes/
    // creates here. A Read/Write may name a resource a later-added node
    // creates -- FluxionRenderGraphBuilder resolves names lazily (creating
    // an empty slot on first reference) rather than requiring create-
    // before-use in addition order, which is what lets the dependency
    // graph below actually reorder nodes relative to addition order, and
    // what lets a genuine cycle exist for the cycle-detection step to
    // catch (a naive "must already exist" check enforced strictly in
    // addition order could never observe a real A<->B cycle, since
    // Setup always runs in a fixed order).
    bool setupHadError = false;
    for (u32 i = 0; i < nodeCount; ++i)
    {
        FluxionRenderGraphBuilder builder;
        builder.graph = graph;
        builder.nodeIndex = i;
        builder.hadError = false;

        graph->nodes[i].passType->setup(&builder, graph->nodes[i].userData);

        if (builder.hadError) setupHadError = true;
    }

    if (setupHadError)
    {
        FLUXION_LOG_ERROR("RenderGraph", "Compile failed: a pass's Setup callback misused the builder (see prior assert).");
        return false;
    }

    // --- Validate every referenced resource has a producer -------------------
    for (u32 i = 0; i < graph->textureCount; ++i)
    {
        const FluxionRenderGraphTextureResource& resource = graph->textures[i];
        if (resource.registered && !resource.created && !resource.imported)
        {
            FLUXION_LOG_ERROR("RenderGraph", "Compile failed: texture resource '%s' is read or written but never created or imported.", resource.name);
            return false;
        }
    }
    for (u32 i = 0; i < graph->bufferCount; ++i)
    {
        const FluxionRenderGraphBufferResource& resource = graph->buffers[i];
        if (resource.registered && !resource.created && !resource.imported)
        {
            FLUXION_LOG_ERROR("RenderGraph", "Compile failed: buffer resource '%s' is read or written but never created or imported.", resource.name);
            return false;
        }
    }

    // --- Build the dependency graph ------------------------------------------
    std::set<std::pair<u32, u32>> edges;
    for (u32 i = 0; i < graph->textureCount; ++i)
    {
        AddResourceEdges(GatherUsages(graph, FLUXION_RENDER_GRAPH_RESOURCE_TEXTURE, i), edges);
    }
    for (u32 i = 0; i < graph->bufferCount; ++i)
    {
        AddResourceEdges(GatherUsages(graph, FLUXION_RENDER_GRAPH_RESOURCE_BUFFER, i), edges);
    }

    std::vector<std::vector<u32>> successors(nodeCount);
    std::vector<std::vector<u32>> predecessors(nodeCount);
    std::vector<u32> inDegree(nodeCount, 0);
    for (const std::pair<u32, u32>& edge : edges)
    {
        successors[edge.first].push_back(edge.second);
        predecessors[edge.second].push_back(edge.first);
        inDegree[edge.second]++;

        if (graph->edgeCount < FLUXION_RENDER_GRAPH_MAX_EDGES)
        {
            graph->edgeFrom[graph->edgeCount] = edge.first;
            graph->edgeTo[graph->edgeCount] = edge.second;
            graph->edgeCount++;
        }
    }

    // --- Topological sort (Kahn's algorithm) + cycle detection --------------
    //
    // Among every node currently eligible (in-degree 0), always pick the
    // lowest node index -- a deterministic tie-break so independent nodes
    // keep addition order rather than an arbitrary one.
    std::vector<bool> visited(nodeCount, false);
    std::vector<u32> order;
    order.reserve(nodeCount);
    while (order.size() < nodeCount)
    {
        u32 next = FLUXION_RENDER_GRAPH_INVALID_NODE_INDEX;
        for (u32 i = 0; i < nodeCount; ++i)
        {
            if (!visited[i] && inDegree[i] == 0)
            {
                next = i;
                break;
            }
        }

        if (next == FLUXION_RENDER_GRAPH_INVALID_NODE_INDEX)
        {
            // No eligible node remains but nodes are unvisited -- a cycle.
            // This is a normal, recoverable failure mode for a (possibly
            // JSON-loaded) graph, not a programming-logic violation, so it
            // is reported and returned as a compile failure rather than an
            // assert.
            FLUXION_LOG_ERROR("RenderGraph", "Compile failed: dependency cycle detected among render graph passes.");
            return false;
        }

        visited[next] = true;
        order.push_back(next);
        for (u32 successor : successors[next])
        {
            inDegree[successor]--;
        }
    }

    for (u32 i = 0; i < nodeCount; ++i)
    {
        graph->executionOrder[i] = order[i];
    }

    std::vector<u32> positionInOrder(nodeCount);
    for (u32 pos = 0; pos < nodeCount; ++pos)
    {
        positionInOrder[order[pos]] = pos;
    }

    // --- Pass culling ---------------------------------------------------------
    //
    // Reverse reachability from every node that writes an imported
    // resource (an imported resource is always considered externally
    // observed -- e.g. the swapchain backbuffer -- so anything that
    // transitively contributes to it must run). If nothing in the graph
    // is imported at all (a pathological/test-only case), every node is
    // treated as reachable instead of culling the entire graph.
    bool anyImported = false;
    for (u32 i = 0; i < graph->textureCount && !anyImported; ++i) anyImported = graph->textures[i].imported;
    for (u32 i = 0; i < graph->bufferCount && !anyImported; ++i) anyImported = graph->buffers[i].imported;

    std::vector<bool> reachable(nodeCount, false);
    if (!anyImported)
    {
        std::fill(reachable.begin(), reachable.end(), true);
    }
    else
    {
        std::vector<u32> stack;
        auto seedFromWriters = [&](FluxionRenderGraphResourceKind kind, u32 resourceIndex, bool imported)
        {
            if (!imported) return;
            for (const UsageRef& usageRef : GatherUsages(graph, kind, resourceIndex))
            {
                if (usageRef.usage == FLUXION_RENDER_GRAPH_USAGE_WRITE && !reachable[usageRef.nodeIndex])
                {
                    reachable[usageRef.nodeIndex] = true;
                    stack.push_back(usageRef.nodeIndex);
                }
            }
        };
        for (u32 i = 0; i < graph->textureCount; ++i) seedFromWriters(FLUXION_RENDER_GRAPH_RESOURCE_TEXTURE, i, graph->textures[i].imported);
        for (u32 i = 0; i < graph->bufferCount; ++i) seedFromWriters(FLUXION_RENDER_GRAPH_RESOURCE_BUFFER, i, graph->buffers[i].imported);

        while (!stack.empty())
        {
            u32 node = stack.back();
            stack.pop_back();
            for (u32 predecessor : predecessors[node])
            {
                if (!reachable[predecessor])
                {
                    reachable[predecessor] = true;
                    stack.push_back(predecessor);
                }
            }
        }
    }

    for (u32 i = 0; i < nodeCount; ++i)
    {
        graph->nodes[i].culled = !reachable[i];
    }

    // --- Barrier generation + resource lifetime --------------------------------
    //
    // For each resource, walk its usages restricted to surviving (non-
    // culled) nodes in compiled (topological) order, emitting a barrier
    // whenever the required state changes; the resource's lifetime is
    // [first surviving usage's node, last surviving usage's node] for a
    // transient resource (never created/destroyed at all if no surviving
    // node touches it), or left alone entirely for an imported one (the
    // executor never creates/destroys those).
    std::vector<std::vector<FluxionRenderGraphBarrierPlan>> perNodeBarriers(nodeCount);

    auto processResource = [&](FluxionRenderGraphResourceKind kind, u32 resourceIndex, bool imported, FluxionRHIResourceState importedInitialState, u32* createAtNode, u32* destroyAtNode)
    {
        std::vector<UsageRef> usages = GatherUsages(graph, kind, resourceIndex);
        std::vector<UsageRef> filtered;
        for (const UsageRef& usageRef : usages)
        {
            if (!graph->nodes[usageRef.nodeIndex].culled) filtered.push_back(usageRef);
        }
        if (filtered.empty()) return;

        std::stable_sort(filtered.begin(), filtered.end(), [&](const UsageRef& a, const UsageRef& b)
        {
            return positionInOrder[a.nodeIndex] < positionInOrder[b.nodeIndex];
        });

        if (!imported)
        {
            *createAtNode = filtered.front().nodeIndex;
            *destroyAtNode = filtered.back().nodeIndex;
        }

        FluxionRHIResourceState previousState = imported ? importedInitialState : FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
        for (const UsageRef& usageRef : filtered)
        {
            FluxionRHIResourceState currentState = MapUsageToState(usageRef.usage);
            if (currentState != previousState)
            {
                FluxionRenderGraphBarrierPlan plan;
                plan.kind = kind;
                plan.resourceIndex = resourceIndex;
                plan.before = previousState;
                plan.after = currentState;
                perNodeBarriers[usageRef.nodeIndex].push_back(plan);
            }
            previousState = currentState;
        }
    };

    for (u32 i = 0; i < graph->textureCount; ++i)
    {
        processResource(FLUXION_RENDER_GRAPH_RESOURCE_TEXTURE, i, graph->textures[i].imported, graph->textures[i].importedInitialState,
            &graph->createTextureAtNode[i], &graph->destroyTextureAtNode[i]);
    }
    for (u32 i = 0; i < graph->bufferCount; ++i)
    {
        processResource(FLUXION_RENDER_GRAPH_RESOURCE_BUFFER, i, graph->buffers[i].imported, graph->buffers[i].importedInitialState,
            &graph->createBufferAtNode[i], &graph->destroyBufferAtNode[i]);
    }

    u32 flatIndex = 0;
    for (u32 i = 0; i < nodeCount; ++i)
    {
        graph->barrierStartForNode[i] = flatIndex;
        graph->barrierCountForNode[i] = (u32)perNodeBarriers[i].size();
        for (const FluxionRenderGraphBarrierPlan& plan : perNodeBarriers[i])
        {
            FLUXION_ASSERT_MSG(flatIndex < FLUXION_RENDER_GRAPH_MAX_USAGES, "RenderGraph: exceeded FLUXION_RENDER_GRAPH_MAX_USAGES while flattening compiled barriers");
            if (flatIndex >= FLUXION_RENDER_GRAPH_MAX_USAGES) break;
            graph->barriers[flatIndex++] = plan;
        }
    }
    graph->barrierCount = flatIndex;

    return true;
}
