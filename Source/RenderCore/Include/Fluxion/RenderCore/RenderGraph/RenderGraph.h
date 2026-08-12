#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphHandles.h>

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque -- real definition is Private/RenderGraph/RenderGraphInternal.h,
// shared only by this module's own .c/.cpp translation units.
typedef struct FluxionRenderGraph FluxionRenderGraph;

FluxionRenderGraph* Fluxion_RenderGraph_Create(FluxionRHIDeviceHandle device);
void Fluxion_RenderGraph_Destroy(FluxionRenderGraph* graph);

// Adds one node instance of a registered pass type. userData is stored and
// passed to both the type's Setup and Execute callbacks for THIS node
// instance only (not shared across other instances of the same type in
// the same graph) -- this is how one registered pass type (e.g.
// "FullscreenBlit") is instantiated multiple times in one graph with
// different parameters. Returns an invalid handle if passTypeName isn't
// registered in the pass-type registry.
FluxionRenderGraphPassHandle Fluxion_RenderGraph_AddPassFromRegistry(FluxionRenderGraph* graph, const char* passTypeName, void* userData);

// Wraps an already-existing RHI texture/buffer as a graph resource under
// `resourceName`, so passes can Read/Write it via
// Fluxion_RenderGraphBuilder_Read/WriteTexture exactly like a Create'd
// one -- the executor never creates or destroys an imported resource's
// underlying RHI object, only transitions its state via barriers.
// `currentState` is the state the resource is already in when the graph
// starts executing (e.g. FLUXION_RHI_RESOURCE_STATE_PRESENT for a
// freshly-acquired swapchain image); it is used as the "before" state of
// the resource's first transition instead of FLUXION_RHI_RESOURCE_STATE_UNDEFINED.
// Must be called before Fluxion_RenderGraph_Compile.
FluxionRenderGraphTextureHandle Fluxion_RenderGraph_ImportTexture(FluxionRenderGraph* graph, const char* resourceName, FluxionRHITextureHandle texture, FluxionRHIResourceState currentState);
FluxionRenderGraphBufferHandle Fluxion_RenderGraph_ImportBuffer(FluxionRenderGraph* graph, const char* resourceName, FluxionRHIBufferHandle buffer, FluxionRHIResourceState currentState);

// Loads node topology from a JSON buffer (see the .rendergraph format
// documented alongside RenderGraphJsonLoader.c), adding passes via the
// exact same Fluxion_RenderGraph_AddPassFromRegistry path above -- a
// JSON-described graph and a code-described one are indistinguishable
// once built. Returns false on a malformed file or a reference to an
// unregistered pass type name (nothing is left partially added in that
// case: the whole file is parsed and validated before any node is added).
bool Fluxion_RenderGraph_LoadFromJSON(FluxionRenderGraph* graph, const char* jsonText, usize jsonLength);

// Setup -> topological sort -> cycle detection -> pass culling -> barrier
// generation -> resource lifetime derivation, in that order. Returns false
// (graph left uncompiled, Execute must not be called) on a cycle or an
// unresolved Read/Write reference. Safe to call every frame if the
// graph's topology is rebuilt each time (a static graph only needs to
// compile once).
bool Fluxion_RenderGraph_Compile(FluxionRenderGraph* graph);

// Creates this frame's transient resources, issues the compiled barrier
// list, and calls every (non-culled) pass's Execute callback in the
// compiled order, all recorded into `commandList` (already Begin'd by the
// caller). Destroys this frame's transient resources at the end -- a
// transient resource's RHI lifetime is exactly one Execute call, never
// carried across frames (no resource aliasing/pooling in this version --
// a deliberate v1 scope limit, not an oversight). Must not be called
// unless the most recent Fluxion_RenderGraph_Compile call returned true.
void Fluxion_RenderGraph_Execute(FluxionRenderGraph* graph, FluxionRHICommandListHandle commandList);

FluxionRHITextureHandle Fluxion_RenderGraph_ResolveTexture(FluxionRenderGraph* graph, FluxionRenderGraphTextureHandle handle);
FluxionRHIBufferHandle Fluxion_RenderGraph_ResolveBuffer(FluxionRenderGraph* graph, FluxionRenderGraphBufferHandle handle);

// Writes a Graphviz DOT representation of the compiled graph (nodes, plus
// the dependency edges the compiler derived) to `file` -- a debug aid, not
// a rendering-quality DOT file. Must be called after a successful
// Compile; returns false (no-op) if the graph hasn't been compiled yet.
bool Fluxion_RenderGraph_DumpDot(FluxionRenderGraph* graph, FILE* file);

#ifdef __cplusplus
}
#endif
