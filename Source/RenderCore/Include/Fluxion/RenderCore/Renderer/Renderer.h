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

// --- What survives from one frame to the next -----------------------------
//
// A renderer keeps the frame before this one: where every pixel was (the
// motion vectors), and in time what it could see. Everything temporal --
// reprojection, anti-aliasing that accumulates, reflections that reuse
// last frame's colour -- reads from there.

// Whether last frame's contents may be read at all this frame. False on
// the first frame and after the frame changes size.
bool Fluxion_Renderer_IsHistoryValid(FluxionRendererHandle renderer);

// SAY SO WHEN THE CAMERA DID NOT MOVE BUT JUMPED -- a cut, a teleport, a
// level load. There is nothing behind a reprojection then, and no
// threshold in metres can tell a jump from a fast pan, so the engine does
// not guess: whoever moved the camera knows.
void Fluxion_Renderer_InvalidateHistory(FluxionRendererHandle renderer);

// Where each pixel of this frame was on the screen in the frame before
// it, as a texture a later pass samples: two signed channels, in the
// coordinates a texture is read by. Invalid until a frame has been begun.
FluxionRHITextureViewHandle Fluxion_Renderer_GetMotionVectorView(FluxionRendererHandle renderer);

// The same thing as a texture, which is what a render graph binds to the
// name a pass writes ("MotionVectorPass.Motion"). Made at the first
// Fluxion_Renderer_BeginFrame and remade whenever the frame changes size.
FluxionRHITextureHandle Fluxion_Renderer_GetMotionVectorTexture(FluxionRendererHandle renderer);

// The frame's depth, halved again and again -- every texel of every level
// the CLOSEST depth of what it covers. What a render graph binds to
// "DepthPyramidPass.Pyramid", and what the occlusion culling reads.
//
// THE PASS THAT FILLS IT SAMPLES THE FRAME'S DEPTH, so the depth texture
// a caller provides has to have been made with SAMPLED usage. Without it
// the frame has no pyramid and nothing is culled by what stands in front
// of it -- which is slower, and not wrong.
FluxionRHITextureHandle Fluxion_Renderer_GetDepthPyramidTexture(FluxionRendererHandle renderer);
u32 Fluxion_Renderer_GetDepthPyramidLevelCount(FluxionRendererHandle renderer);

// --- The chain that turns light into a picture ----------------------------
//
// OFF BY DEFAULT, AND THAT IS NOT A DEFAULT ABOUT QUALITY. With it on,
// every pass that draws the scene writes into a target of sixteen-bit
// light instead of into the target the caller gave the view -- so every
// pipeline that draws the scene has to have been built for THAT format,
// materials included. Switching it on under pipelines built for an
// eight-bit screen is not a worse picture, it is no picture: a pipeline
// cannot write into a target it was not built for.
//
// AND THE SAME IS TRUE TURNING IT OFF. Pipelines built for the scene's
// sixteen-bit target cannot write into the caller's screen either, so
// this is not a switch to put in front of a person at run time unless
// the application has built BOTH -- it is a decision about what a frame
// is made of, taken once, before anything that draws the scene exists.
//
// What it buys is everything that reads the picture as light rather than
// as a colour: what glows and by how much, how bright the frame is on
// average, what a reflection carries. None of that can be read once the
// values have been squashed into what a monitor shows, and squashing
// them is what a surface shader does when there is nothing after it.
//
// A pipeline asset says it (Pipeline/RenderPipelineAsset.h, "postfx"),
// and this is where that answer lands.
void Fluxion_Renderer_SetPostProcessEnabled(FluxionRendererHandle renderer, bool enabled);
bool Fluxion_Renderer_IsPostProcessEnabled(FluxionRendererHandle renderer);

// What the scene is drawn into with the chain on. Ask this before
// building anything that draws the scene -- it is the format those
// pipelines need, and it does not depend on a renderer existing yet.
FluxionRHIFormat Fluxion_Renderer_GetSceneColorFormat(void);

// The texture itself, which is what a render graph binds to the name the
// forward pass writes ("ForwardOpaquePass.Color0") once the chain is on.
// Made at the first Fluxion_Renderer_BeginFrame after it is switched on,
// and remade whenever the frame changes size.
FluxionRHITextureHandle Fluxion_Renderer_GetSceneColorTexture(FluxionRendererHandle renderer);

// The format of what the RESOLVE writes -- the screen's, not the scene's.
// Told rather than guessed, because only the caller knows what it asked
// its swapchain for.
void Fluxion_Renderer_SetOutputColorFormat(FluxionRendererHandle renderer, FluxionRHIFormat format);

// WHETHER WHAT IS BRIGHT SPREADS INTO WHAT IS BESIDE IT.
//
// Needs the chain above: the glow is built from the light the scene was
// drawn in, and with the chain off there is no such picture to build it
// from. How bright a thing must be and how much of the glow comes back
// are the view's to say (RenderView.h, bloomThreshold and the two beside
// it); this is only whether the pass runs at all.
//
// A pipeline asset says it (Pipeline/RenderPipelineAsset.h, "bloom").
// WHETHER THE LIGHTING KNOWS HOW MUCH OF THE SKY EACH PIXEL CAN SEE.
//
// Off by default, and NEEDS THE SURFACE PREPASS: what it searches is the
// frame's own depth, and which way each pixel faces is what says where
// the hemisphere above it is. With the prepass off this does nothing.
//
// What it changes is INDIRECT light only -- the flat ambient and the sky.
// A corner goes dim rather than black, and nothing a lamp shines on gets
// darker, because a lamp is not the sky.
//
// The view says how far it reaches and how finely -- see the description.
void Fluxion_Renderer_SetAmbientOcclusionEnabled(FluxionRendererHandle renderer, bool enabled);

// What it worked out. Valid only while the effect is on and succeeded.
FluxionRHITextureHandle Fluxion_Renderer_GetAmbientOcclusionTexture(FluxionRendererHandle renderer);

// WHETHER THE FRAME'S SURFACES ARE RECORDED BEFORE THEY ARE LIT.
//
// Off by default. With it on, the opaque geometry is drawn once more --
// first -- into a texture holding which way each pixel faces and how
// rough it is, and into the same depth buffer the lighting then tests
// against.
//
// THIS IS WHAT OCCLUSION AND REFLECTIONS ARE BUILT ON, and neither can be
// had without it. Occlusion darkens INDIRECT light, so it has to exist
// before the lighting runs; reflections need a direction to bounce off
// and a roughness to decide how sharp the bounce is, and a finished
// picture holds neither.
//
// The cost is one more pass over the geometry. What it gives back, beside
// the two effects: the forward pass finds its depth already written, so
// it shades nothing that turns out to be hidden.
//
// A material only appears here if its pipeline was given a program for
// the pass -- see Fluxion_RenderPipeline_SetPrepassProgram. One that was
// not leaves those pixels marked as having nothing recorded, and what
// reads them leaves them alone.
void Fluxion_Renderer_SetSurfacePrepassEnabled(FluxionRendererHandle renderer, bool enabled);

// Whether it is on AND succeeded, which is what the passes that read it
// go by.
bool Fluxion_Renderer_HasSurfacePrepass(FluxionRendererHandle renderer);

// What it recorded. Valid only while the above says yes.
FluxionRHITextureHandle Fluxion_Renderer_GetNormalRoughnessTexture(FluxionRendererHandle renderer);

// WHETHER THE FINISHED PICTURE IS SMOOTHED BEFORE IT IS SHOWN.
//
// Off by default. With it on, the scene reaches the screen through one
// more pass, which finds the sharp changes in brightness in the finished
// picture, works out which way each edge runs and blends along it -- the
// staircase a rasteriser leaves on any edge that is not along a row of
// pixels.
//
// SAFE TO CHANGE WHILE RUNNING, unlike the chain itself: what changes is
// which texture the resolve writes into, and both are the same format, so
// no pipeline anywhere is built for something different.
//
// It works on the picture rather than on the scene, so it cannot recover
// an edge thinner than a pixel and it softens fine detail that was never
// an edge. It costs one pass and needs nothing from anything else -- see
// the pass itself for the whole of that trade.
void Fluxion_Renderer_SetFXAAEnabled(FluxionRendererHandle renderer, bool enabled);

// WHETHER THE FRAME'S OWN BRIGHTNESS MOVES THE CAMERA.
//
// Off by default. With it on, the renderer measures what the scene came
// out at and multiplies the view's exposure by what it would take to put
// a middle grey where the view asked for one -- easing towards it over
// several frames rather than arriving at once, which is what an eye does
// and what a camera with automatic exposure does.
//
// UNLIKE THE CHAIN ITSELF, THIS IS SAFE TO CHANGE WHILE RUNNING: it adds
// passes that write their own small textures and changes nothing about
// what anything else is drawn into. Turning it off leaves the view's
// exposure as the whole answer; turning it on again starts the
// measurement over rather than from a setting belonging to whatever was
// on the screen last time.
//
// The view says how far it may go and how fast -- see the description.
void Fluxion_Renderer_SetAutoExposureEnabled(FluxionRendererHandle renderer, bool enabled);

void Fluxion_Renderer_SetBloomEnabled(FluxionRendererHandle renderer, bool enabled);
bool Fluxion_Renderer_IsBloomEnabled(FluxionRendererHandle renderer);

// --- Where the culling happens --------------------------------------------
//
// Not a rendering decision the renderer makes on its own: a pipeline
// asset says it (Pipeline/RenderPipelineAsset.h, "culling"), and this is
// where that answer lands. A renderer nobody tells culls on the host,
// which is what every frame did before there was a choice.

typedef enum FluxionRendererCullMode
{
    FLUXION_RENDERER_CULL_ON_HOST = 0,
    FLUXION_RENDERER_CULL_ON_DEVICE,
} FluxionRendererCullMode;

// Takes effect at the next Fluxion_Renderer_UploadScene, so a frame is
// never half culled one way and half the other.
//
// Asking for the device is a request, not a guarantee: if the cull pass
// cannot be built on this device, the frame falls back to the host and
// says so once. What is drawn is the same either way -- that is the
// whole point of the two paths sharing one visible list.
void Fluxion_Renderer_SetCullMode(FluxionRendererHandle renderer, FluxionRendererCullMode mode);
FluxionRendererCullMode Fluxion_Renderer_GetCullMode(FluxionRendererHandle renderer);

#ifdef __cplusplus
}
#endif
