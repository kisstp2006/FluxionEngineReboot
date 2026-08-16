// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

#pragma once

// Real (non-opaque) definitions for FluxionRenderGraph and
// FluxionRenderGraphBuilder, plus the fixed-capacity limits and helper
// functions shared across this module's own .c/.cpp translation units.
// Never included from a public header -- callers only ever see the
// opaque forward declarations in RenderGraph.h/RenderGraphPass.h.

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPass.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphResource.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed-capacity pools, same "documented fixed pool" convention as the
// RHI backends' own FLUXION_RHI_VULKAN_MAX_* constants -- no dynamic
// per-frame heap allocation in the hot Compile/Execute path.
#define FLUXION_RENDER_GRAPH_MAX_NODES 64
#define FLUXION_RENDER_GRAPH_MAX_TEXTURES 128
#define FLUXION_RENDER_GRAPH_MAX_BUFFERS 128
// Upper bound on total resource-usage declarations across every node's
// Setup call in one graph -- generous relative to MAX_NODES so a node
// with many resource touches (a handful of reads/writes/creates) never
// gets close to the limit in practice.
#define FLUXION_RENDER_GRAPH_MAX_USAGES 1024
#define FLUXION_RENDER_GRAPH_MAX_NAME_LENGTH 63
// Upper bound on distinct derived dependency edges -- a dense-but-still-
// bounded worst case (every node pair), kept only for
// Fluxion_RenderGraph_DumpDot's benefit (a debug aid), so headroom well
// past what any real graph needs is fine.
#define FLUXION_RENDER_GRAPH_MAX_EDGES 2048

#define FLUXION_RENDER_GRAPH_INVALID_NODE_INDEX ((u32)0xFFFFFFFFu)

typedef struct FluxionRenderGraphTextureResource
{
    char name[FLUXION_RENDER_GRAPH_MAX_NAME_LENGTH + 1];
    bool registered; // slot in use (referenced by name at least once)
    bool imported;
    bool created; // a CREATE usage has been declared for it (transient only)
    FluxionRHITextureDesc desc; // valid when !imported
    FluxionRHITextureHandle importedHandle; // valid when imported
    FluxionRHIResourceState importedInitialState; // valid when imported
    FluxionRHITextureHandle resolvedHandle; // valid only during Execute
} FluxionRenderGraphTextureResource;

typedef struct FluxionRenderGraphBufferResource
{
    char name[FLUXION_RENDER_GRAPH_MAX_NAME_LENGTH + 1];
    bool registered;
    bool imported;
    bool created;
    FluxionRHIBufferDesc desc;
    FluxionRHIBufferHandle importedHandle;
    FluxionRHIResourceState importedInitialState;
    FluxionRHIBufferHandle resolvedHandle;
} FluxionRenderGraphBufferResource;

typedef enum FluxionRenderGraphResourceKind
{
    FLUXION_RENDER_GRAPH_RESOURCE_TEXTURE,
    FLUXION_RENDER_GRAPH_RESOURCE_BUFFER,
} FluxionRenderGraphResourceKind;

// One entry per Read/Write/Create call made from inside a Setup callback.
// nodeIndex is the declaring node's index in FluxionRenderGraph::nodes --
// NOT necessarily its position in the compiled execution order.
typedef struct FluxionRenderGraphUsageRecord
{
    u32 nodeIndex;
    FluxionRenderGraphResourceKind kind;
    u32 resourceIndex;
    FluxionRenderGraphResourceUsage usage; // READ, WRITE, or CREATE
} FluxionRenderGraphUsageRecord;

typedef struct FluxionRenderGraphNode
{
    const FluxionRenderGraphPassType* passType;
    void* userData;
    char name[FLUXION_RENDER_GRAPH_MAX_NAME_LENGTH + 1]; // instance name, for DOT/diagnostics; may be empty
    bool culled; // set during Compile's pass-culling step
} FluxionRenderGraphNode;

// A barrier the compiler decided must be issued immediately before a given
// node's Execute call -- see FluxionRenderGraph::barriers for why this
// stores a graph resource reference rather than a concrete FluxionRHIBarrier.
typedef struct FluxionRenderGraphBarrierPlan
{
    FluxionRenderGraphResourceKind kind;
    u32 resourceIndex;
    FluxionRHIResourceState before;
    FluxionRHIResourceState after;
} FluxionRenderGraphBarrierPlan;

struct FluxionRenderGraph
{
    FluxionRHIDeviceHandle device;

    FluxionRenderGraphNode nodes[FLUXION_RENDER_GRAPH_MAX_NODES];
    u32 nodeCount;

    FluxionRenderGraphTextureResource textures[FLUXION_RENDER_GRAPH_MAX_TEXTURES];
    u32 textureCount;
    FluxionRenderGraphBufferResource buffers[FLUXION_RENDER_GRAPH_MAX_BUFFERS];
    u32 bufferCount;

    FluxionRenderGraphUsageRecord usages[FLUXION_RENDER_GRAPH_MAX_USAGES];
    u32 usageCount;

    bool compiled;

    // Compiled (topologically sorted) node order -- always holds exactly
    // nodeCount entries after a successful Compile, culled nodes included
    // (Execute skips a node whose nodes[executionOrder[i]].culled is true).
    u32 executionOrder[FLUXION_RENDER_GRAPH_MAX_NODES];

    // Barriers to issue immediately before a given node's Execute call,
    // stored as a [start, start+count) slice into `barriers`, indexed by
    // node index (not execution-order position). A transient resource's
    // FluxionRHITextureHandle/FluxionRHIBufferHandle does not exist yet at
    // Compile time (it is only created during Execute, fresh every call --
    // no cross-frame pooling), so a planned barrier records which graph
    // resource it targets rather than a concrete RHI handle; the executor
    // resolves `kind`+`resourceIndex` to the resource's current
    // resolvedHandle right before issuing it.
    FluxionRenderGraphBarrierPlan barriers[FLUXION_RENDER_GRAPH_MAX_USAGES];
    u32 barrierCount;
    u32 barrierStartForNode[FLUXION_RENDER_GRAPH_MAX_NODES];
    u32 barrierCountForNode[FLUXION_RENDER_GRAPH_MAX_NODES];

    // Resource lifetime -- the node index (not execution-order position)
    // at which a transient resource should be created/destroyed;
    // FLUXION_RENDER_GRAPH_INVALID_NODE_INDEX for an imported resource, or
    // a transient one no surviving (non-culled) node ever touches.
    u32 createTextureAtNode[FLUXION_RENDER_GRAPH_MAX_TEXTURES];
    u32 destroyTextureAtNode[FLUXION_RENDER_GRAPH_MAX_TEXTURES];
    u32 createBufferAtNode[FLUXION_RENDER_GRAPH_MAX_BUFFERS];
    u32 destroyBufferAtNode[FLUXION_RENDER_GRAPH_MAX_BUFFERS];

    // The dependency edges Compile derived (producer node -> consumer
    // node), kept only so Fluxion_RenderGraph_DumpDot can render them
    // without re-deriving the whole dependency graph itself.
    u32 edgeFrom[FLUXION_RENDER_GRAPH_MAX_EDGES];
    u32 edgeTo[FLUXION_RENDER_GRAPH_MAX_EDGES];
    u32 edgeCount;
};

struct FluxionRenderGraphBuilder
{
    FluxionRenderGraph* graph;
    u32 nodeIndex; // the node currently being Setup, i.e. the declarer of any usage recorded through this builder
    bool hadError; // sticky -- set the moment a Setup call misuses the builder (unregistered graph slot overflow, etc.)
};

// --- Shared helpers (RenderGraphBuilder.c) ----------------------------------

// Finds a texture/buffer resource slot by name, creating an empty
// (unregistered-desc) one on first reference if none exists yet -- this is
// what allows a Read/Write to forward-reference a resource a
// later-added node creates; Fluxion_RenderGraph_Compile validates
// afterwards that every referenced slot ended up created or imported by
// someone. Returns -1 if the resource table is full.
i32 Fluxion_RenderGraphInternal_FindOrCreateTextureSlot(FluxionRenderGraph* graph, const char* name);
i32 Fluxion_RenderGraphInternal_FindOrCreateBufferSlot(FluxionRenderGraph* graph, const char* name);

// Appends a usage record; asserts (does not silently drop) if the fixed
// usage table is full, matching this codebase's convention of asserting
// on fixed-pool exhaustion rather than failing quietly.
void Fluxion_RenderGraphInternal_RecordUsage(FluxionRenderGraph* graph, u32 nodeIndex, FluxionRenderGraphResourceKind kind, u32 resourceIndex, FluxionRenderGraphResourceUsage usage);

// --- Compiler (RenderGraphCompiler.cpp) -------------------------------------

// Runs Setup on every node, then topological sort + cycle detection +
// pass culling + barrier generation + resource lifetime derivation, in
// that order, populating every field above from executionOrder onward.
// Returns false (graph->compiled left false) on an unresolved
// Read/Write reference or a dependency cycle.
bool Fluxion_RenderGraphInternal_Compile(FluxionRenderGraph* graph);

#ifdef __cplusplus
}
#endif
