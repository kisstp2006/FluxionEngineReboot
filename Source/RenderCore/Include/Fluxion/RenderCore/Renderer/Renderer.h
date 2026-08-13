#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_HANDLE(FluxionRendererHandle);

// Registers this instance's "ForwardOpaquePass" render graph pass type
// (Fluxion_RenderGraphPassRegistry_Init must already have been called).
// Owns a per-frame draw-packet list and a growing OBJECT-frequency
// storage buffer of per-draw world transforms; never allocates, submits,
// or presents a command list itself -- the caller supplies one to
// Fluxion_Renderer_EndFrame, the same caller-owns-the-command-list
// boundary Fluxion_RenderGraph_Execute already respects.
FluxionRendererHandle Fluxion_Renderer_Create(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue);
void Fluxion_Renderer_Destroy(FluxionRendererHandle renderer);

void Fluxion_Renderer_BeginFrame(FluxionRendererHandle renderer, FluxionRenderViewHandle view);

// Every DrawMesh call is immediately visible to "ForwardOpaquePass" --
// there is no separate flush step, so a typical frame is: BeginFrame,
// any number of DrawMesh calls, then the caller compiles and executes
// (Fluxion_RenderGraph_Compile/Execute) whatever render graph it built
// containing this renderer's "ForwardOpaquePass" node, and only then
// calls EndFrame (with the same command list) to close out the frame.
void Fluxion_Renderer_DrawMesh(FluxionRendererHandle renderer, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material, FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform);

// Draws this frame's accumulated Fluxion_DebugDraw_* geometry (if any)
// directly into `commandList`, then resets per-frame state. Call after
// the render graph containing "ForwardOpaquePass" has already executed
// (see Fluxion_Renderer_DrawMesh's comment) -- EndFrame does not touch
// the render graph itself.
void Fluxion_Renderer_EndFrame(FluxionRendererHandle renderer, FluxionRHICommandListHandle commandList);

// The colour format the renderer's own built-in debug-draw pipeline is
// built against. It has to be the format of the colour attachment
// EndFrame draws that geometry into, or the draw is rejected outright by
// the backend -- and nothing reachable from inside RenderCore says what
// that format is, since a FluxionRHITextureViewHandle carries no
// queryable format (the same reason
// FluxionRendererInternal_RenderView_GetViewport exists for its extent).
// So the caller, which created the swapchain or the target and therefore
// knows, says it.
//
// FLUXION_RHI_FORMAT_R8G8B8A8_UNORM until told otherwise. Saying the
// format already in force does nothing; saying a different one rebuilds
// the pipeline, so this belongs with the rest of a program's setup and
// must not be called between BeginFrame and EndFrame. A program that
// never calls Fluxion_DebugDraw_* need not call this at all.
void Fluxion_Renderer_SetDebugDrawColorFormat(FluxionRendererHandle renderer, FluxionRHIFormat format);

// The depth target debug geometry is tested against, for the same reason
// the colour format has to be said: a texture view carries no queryable
// format, so the caller who made it is the one who knows.
//
// Unknown until told otherwise, which means no depth attachment and no
// test -- every line drawn over the top of everything. Given a real
// format, a line behind an object is hidden by it. Depth is never
// written either way. Saying the format already in force does nothing;
// saying a different one rebuilds the pipeline, so this belongs with the
// rest of a program's setup and must not be called between BeginFrame and
// EndFrame.
void Fluxion_Renderer_SetDebugDrawDepthFormat(FluxionRendererHandle renderer, FluxionRHIFormat format);

// Not in the original sketch: "ForwardOpaquePass" is registered once per
// FluxionRenderer instance, but a render graph node's userData is
// supplied by whoever calls Fluxion_RenderGraph_AddPassFromRegistry --
// the graph owner, outside this module. This is the only way for that
// caller to hand the pass back a pointer to this renderer's own
// accumulated per-frame state without reaching into RenderCore internals.
// Typical use: Fluxion_RenderGraph_AddPassFromRegistry(graph,
// "ForwardOpaquePass", Fluxion_Renderer_GetForwardOpaquePassUserData(renderer)).
void* Fluxion_Renderer_GetForwardOpaquePassUserData(FluxionRendererHandle renderer);

// Also not in the original sketch: how many draw calls "ForwardOpaquePass"
// actually issued during the most recent render graph Execute this frame
// (0 before it has run, or if every packet was skipped -- e.g. an
// unresolvable mesh/pipeline). Exists so a caller (or a test) can observe
// that a registered pass instance genuinely ran and drew what was
// expected, since neither the render graph nor the RHI otherwise exposes
// that from outside a pass's own Execute callback.
u32 Fluxion_Renderer_GetLastDrawCallCount(FluxionRendererHandle renderer);

#ifdef __cplusplus
}
#endif
