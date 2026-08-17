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

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>

// Only named in a signature below, never dereferenced by this header --
// see Scene/RenderWorld.h for what it is.
struct FluxionRenderWorld;

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

// Works the view's environment out into the nine coefficients a surface
// reads, if it changed since the last time this was asked.
//
// OUTSIDE a render pass, like the light upload it sits beside: what it
// records is a compute dispatch, and a dispatch cannot happen between a
// BeginRendering and its EndRendering.
//
// Cheap to call every frame and meant to be: it does nothing at all
// unless the environment changed, and a sky that did not move cannot have
// a different answer.
void Fluxion_Renderer_UpdateEnvironment(FluxionRendererHandle renderer, FluxionRenderViewHandle view,
                                        FluxionRHICommandListHandle commandList);

void Fluxion_Renderer_BeginFrame(FluxionRendererHandle renderer, FluxionRenderViewHandle view);

// Adds one thing to what this frame draws. It does NOT reach a device by
// itself -- see Fluxion_Renderer_UploadScene, which is the step that
// does, and which has to come after the last of these.
//
// A typical frame is: BeginFrame, any number of DrawMesh calls,
// UploadScene, then the caller compiles and executes
// (Fluxion_RenderGraph_Compile/Execute) whatever render graph it built
// containing this renderer's "ForwardOpaquePass" node, and only then
// calls EndFrame (with the same command list) to close out the frame.
void Fluxion_Renderer_DrawMesh(FluxionRendererHandle renderer, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material, FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform);

// Everything a render world holds, handed to this frame in one call.
//
// The same thing DrawMesh does, once per object -- and the reason it is
// its own call is that a render world is where a step BETWEEN the scene
// and the frame will live: what is visible, which level of detail, what
// the last frame decided. An object marked not visible is not submitted,
// which is the whole of that step for now.
void Fluxion_Renderer_SubmitRenderWorld(FluxionRendererHandle renderer, const struct FluxionRenderWorld* world);

// Works out what this frame's draws have in common, lays them out in
// that order, and records the copy that puts them where a shader can
// read them.
//
// AFTER THE LAST DrawMesh AND BEFORE ANYTHING DRAWS. Not inside DrawMesh,
// because the order objects end up in depends on all of them: what makes
// a thousand objects one draw call is that the ones sharing a pipeline,
// a material and a mesh sit next to each other, and that cannot be known
// while they are still arriving.
//
// The same rule, and the same reason, as Fluxion_RenderView_UploadLighting
// -- and the same place in a frame.
void Fluxion_Renderer_UploadScene(FluxionRendererHandle renderer, FluxionRHICommandListHandle commandList);

// Draws this frame's accumulated Fluxion_DebugDraw_* geometry (if any)
// directly into `commandList`, then resets per-frame state. Call after
// the render graph containing "ForwardOpaquePass" has already executed
// (see Fluxion_Renderer_DrawMesh's comment) -- EndFrame does not touch
// the render graph itself.
void Fluxion_Renderer_EndFrame(FluxionRendererHandle renderer, FluxionRHICommandListHandle commandList);

// The colour format the built-in debug-draw pipeline is built against.
// It must match the attachment EndFrame draws into, and nothing in
// RenderCore can find that out -- a texture view carries no queryable
// format -- so the caller who made the target says it. R8G8B8A8_UNORM
// until told otherwise; a different value rebuilds the pipeline, so call
// it at setup, never between BeginFrame and EndFrame. Programs that
// never debug-draw need not call it.
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

// How many of the frame's objects survived the cull.
//
// Beside the draw-call count rather than instead of it, because the two
// answer different questions: this one says how much the culling threw
// away, that one says how much the batching saved. A frame that drew
// everything in one call and a frame that culled everything both have a
// draw-call count of one.
u32 Fluxion_Renderer_GetVisibleObjectCount(FluxionRendererHandle renderer);

#ifdef __cplusplus
}
#endif
