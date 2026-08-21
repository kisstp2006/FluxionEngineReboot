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

// Real (non-opaque) definitions and cross-file accessors shared only by
// this module's own Private/Renderer/*.c/*.cpp translation units -- never
// included from a public Renderer/*.h header, same convention as
// RenderGraphInternal.h. Every object kind other than FluxionRenderer
// itself (ShaderProgram, Material, MeshBuffer, RenderTarget, RenderView,
// RenderPipeline) keeps its real record fully private to its own
// .c/.cpp file and reachable only through the accessor functions
// declared below -- ShaderProgram/Material/RenderPipeline in particular
// carry members (Fluxion::ShaderCompiler's ShaderIRModule, std::vector)
// that a plain-C translation unit like ForwardOpaquePass.c cannot even
// parse, so this boundary is load-bearing, not just style.

#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPass.h>
#include <Fluxion/RenderCore/Renderer/DrawPacket.h>
#include <Fluxion/RenderCore/Renderer/GPUScene.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/ShadowAtlas.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed-capacity pools, same "documented fixed pool" convention as
// RenderGraphInternal.h's own FLUXION_RENDER_GRAPH_MAX_* constants.
#define FLUXION_RENDERER_MAX_SHADER_PROGRAMS 64
#define FLUXION_RENDERER_MAX_MATERIALS 256
#define FLUXION_RENDERER_MAX_MESH_BUFFERS 512
#define FLUXION_RENDERER_MAX_RENDER_TARGETS 32
#define FLUXION_RENDERER_MAX_RENDER_VIEWS 16
#define FLUXION_RENDERER_MAX_RENDER_PIPELINES 64
#define FLUXION_RENDERER_MAX_PIPELINE_VARIANTS 8 // vertex-layout cache slots per RenderPipeline
#define FLUXION_RENDERER_MAX_MATERIAL_PARAMETERS 32
#define FLUXION_RENDERER_MAX_DRAW_PACKETS_PER_FRAME 4096
#define FLUXION_RENDERER_MAX_INSTANCES 8 // an application rarely needs more than one or two live FluxionRenderer objects at once
#define FLUXION_RENDERER_MAX_DEBUG_VERTICES 8192
// D3D12 requires every constant-buffer-view offset/size to land on a
// 256-byte boundary -- the OBJECT buffer is bound as a uniform buffer
// (see FluxionRendererInternal_MakeObjectLayoutDesc), so each draw's
// slice is padded out to this stride even though FluxionMat4 itself is
// only 64 bytes. Applied on every backend, not just D3D12, so the byte
// layout callers observe doesn't depend on which one is active.
#define FLUXION_RENDERER_OBJECT_BUFFER_STRIDE 256

// The prefiltered environment chain: face width at the sharpest mip, and
// how many mips down to the fully rough one. THE MIP COUNT IS ALSO IN
// Fluxion/Frame.jsl as FLUXION_PREFILTERED_MIP_COUNT -- the shader turns
// a roughness into a mip level with it, and the two must agree or every
// roughness reads a blur it did not mean. 128 for the size: reflections
// blurrier than mirror-sharp stop showing texel detail almost at once,
// and the sharp end is mip zero either way.
#define FLUXION_RENDERER_PREFILTERED_SIZE 128
#define FLUXION_RENDERER_PREFILTERED_MIPS 8

// The split-sum table's width and height. The function it stores is
// smooth in both directions, which is what lets a modest table and a
// bilinear read answer for every surface.
#define FLUXION_RENDERER_DFG_SIZE 128

// One depth texture for every shadow in the frame, cut into equal
// tiles. Four by four at these numbers: the sun's cascades no longer
// have the atlas to themselves, and a single point light wants six
// tiles of it on its own.
//
// The tile got smaller rather than the atlas bigger, because the atlas
// is what costs memory and a tile is what costs sharpness -- and the
// sharpness lost is mostly in the far cascades, where a texel already
// covers more than anyone can make out.
//
// WHAT A VIEW GETS WHEN NOTHING ASKS FOR ANYTHING ELSE. A view is made
// with the pair its description names (see FluxionRenderViewDesc), and
// carries that pair around with it; these two are only the answer for a
// description that left the question open.
#define FLUXION_RENDERER_SHADOW_ATLAS_SIZE 2048
#define FLUXION_RENDERER_SHADOW_TILE_SIZE 512

// One canonical, engine-owned bind-group-layout shape per FRAME/OBJECT
// frequency, independent of any particular shader -- every RenderView
// (FRAME) and the Renderer's own per-frame object buffer (OBJECT) build
// a layout from these same descs, and so does every RenderPipeline, so
// they stay shape-compatible without needing one shared singleton
// object (Fluxion_RHI_CreateBindGroupLayout is itself free to dedupe
// identical descs -- see RHI.h).
static inline FluxionRHIBindGroupLayoutDesc FluxionRendererInternal_MakeFrameLayoutDesc(void)
{
    FluxionRHIBindGroupLayoutDesc desc = { };
    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX | FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // The rest of the numbers are NOT free to choose. The shader compiler
    // gives a group's uniform buffer binding 0, then hands out a pair to
    // every texture -- the texture and its sampler -- and only then the
    // storage buffers. So the order here is the compiler's order, not the
    // order Fluxion/Frame.jsl happens to declare things in, and a texture
    // added to that file shifts the light list along whether or not it is
    // written above it.
    desc.entries[1].binding = 1;
    desc.entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[2].binding = 2;
    desc.entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[2].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // The environment pre-blurred per roughness, and its pair.
    desc.entries[3].binding = 3;
    desc.entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[3].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[4].binding = 4;
    desc.entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[4].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // The split-sum table, and its pair.
    desc.entries[5].binding = 5;
    desc.entries[5].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[5].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[6].binding = 6;
    desc.entries[6].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[6].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // Every shadow in the frame, and the comparison sampler that turns a
    // read of it into an answer rather than a depth.
    desc.entries[7].binding = 7;
    desc.entries[7].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[7].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[8].binding = 8;
    desc.entries[8].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[8].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // THE OCCLUSION, AND THE PLAIN SAMPLER THAT READS IT.
    //
    // A texture rather than a number because it is a different answer per
    // pixel, and in this group rather than one of its own because every
    // surface that gets lit needs it -- and the frame group is what every
    // surface already has.
    desc.entries[9].binding = 9;
    desc.entries[9].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[9].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[10].binding = 10;
    desc.entries[10].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[10].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // A storage buffer rather than an array in the uniform block above.
    // An array would need a maximum written into the shader, and that
    // maximum would be a number somebody has to raise -- and raising it
    // costs every frame that does not use it, because a uniform block is
    // paid for whether it is full or not.
    //
    // THE NUMBERS ARE THE COMPILER'S, NOT THIS FILE'S: a group's uniform
    // buffer is binding zero, then every texture takes a pair, and
    // storage buffers come last. Adding a texture above therefore MOVED
    // these three, and the bind group that fills them moved with it.
    desc.entries[11].binding = 11;
    desc.entries[11].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[11].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // The sky, as nine coefficients. A second storage buffer rather than
    // more fields in the block at binding 0, because a compute pass is
    // what fills it: a uniform buffer would have to be written from the
    // processor, and by then the numbers are already on the device.
    desc.entries[12].binding = 12;
    desc.entries[12].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[12].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // Where each shadow's light looks from, and which tile holds it.
    desc.entries[13].binding = 13;
    desc.entries[13].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[13].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entryCount = 14;
    desc.debugName = "Fluxion.Renderer.FrameBindGroupLayout";
    return desc;
}

// OBJECT is two bindings now: a tiny uniform buffer saying where in the
// object list this draw starts, and the object list itself.
//
// The list is a STORAGE buffer because it is one entry per object in the
// whole frame rather than one small struct per draw -- and a storage
// buffer's SRV+UAV pair (see D3D12Pipeline.cpp's dual-range comment)
// requires the backing resource to allow unordered access, which D3D12
// forbids on CPU-visible memory. That is why the list is device memory
// written by a copy (GPUScene.c) rather than mapped and written
// directly, while the small uniform beside it, which is neither, can
// still be mapped.
static inline FluxionRHIBindGroupLayoutDesc FluxionRendererInternal_MakeObjectLayoutDesc(void)
{
    FluxionRHIBindGroupLayoutDesc desc = { };
    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX | FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[1].binding = 1;
    desc.entries[1].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX;

    // Which of the objects this frame draws -- see Fluxion/Object.jsl.
    // The compiler hands storage buffers their bindings in declaration
    // order after the uniform block, so this is the second one declared
    // there and nothing is free to reorder either side alone.
    desc.entries[2].binding = 2;
    desc.entries[2].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[2].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX;

    desc.entryCount = 3;
    desc.debugName = "Fluxion.Renderer.ObjectBindGroupLayout";
    return desc;
}

// --- What survives from one frame to the next (RenderHistory.c) -------------
//
// Everything temporal reads from here: the motion vectors this frame
// wrote, and (once the depth pyramid lands beside them) what the frame
// before this one could see.
// HOW MANY HALVINGS THE GLOW IS SPREAD OVER. Eight takes a 1080p frame
// down to about four pixels across, which is wider than any glow needs to
// reach; going further spends draws on a picture that is already one
// colour.
#define FLUXION_RENDERER_MAX_BLOOM_LEVELS 8

// A step per level down, and one per level but the last on the way up.
#define FLUXION_RENDERER_MAX_BLOOM_STEPS (FLUXION_RENDERER_MAX_BLOOM_LEVELS * 2 - 1)

// HOW BIG THE PICTURE THE FRAME'S BRIGHTNESS IS MEASURED FROM IS -- and
// it is a fixed square rather than a fraction of the frame, which is the
// decision worth writing down.
//
// A chain that started at half the window would have a different number
// of levels at every window size, an odd level size wherever the window
// was odd, and a cost that grew with the resolution -- all of it to
// produce ONE NUMBER. A fixed square makes every one of those constant:
// the same nine halvings, all of them exact, on every machine and at
// every window size, for a quarter of a megabyte.
//
// Two hundred and fifty six across is far more than a single average
// needs. It is this big because the same chain is what a histogram would
// later be read from, and because the first step's four taps then cover
// a whole 1080p frame rather than skipping most of it.
// HOW MANY TIMES THE DISTANCES ARE HALVED for the occlusion search to
// read from. Four takes a step of eight texels down to one, which is as
// far out as a radius of a metre or so reaches at arm's length -- and
// past that the answer is being read from an average of an average, which
// says less about the surface than it does about the neighbourhood.
#define FLUXION_RENDERER_VIEW_DEPTH_LEVELS 4

// Two: what the search produced, and what the denoise made of it. Not a
// history -- both belong to this frame, and a pass cannot read the
// texture it is writing.
#define FLUXION_RENDERER_OCCLUSION_TEXTURES 2

#define FLUXION_RENDERER_LUMINANCE_SIZE 256

// Nine, because two hundred and fifty six halves to one in eight steps
// and the first level is one of them. Written out rather than worked out
// so that the array below has a size a reader can see.
#define FLUXION_RENDERER_LUMINANCE_LEVELS 9

// The two the adapted exposure lives in, alternating: a pass cannot read
// the texture it is writing, and what this frame's exposure is made from
// is what last frame's exposure was.
#define FLUXION_RENDERER_EXPOSURE_HISTORY 2

// How many levels a depth pyramid may have. Sixteen halvings take the
// largest screen anybody draws down to a single texel, and the array is
// fixed rather than allocated because a frame's size does not change
// between one frame and the next often enough to be worth a heap.
#define FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS 16

typedef struct FluxionRenderHistory
{
    // Where each pixel of this frame was on the screen in the frame
    // before it. Two channels, signed, in the coordinates a texture is
    // sampled by -- see Fluxion/Pass/MotionVectorWrite.jsl.
    FluxionRHITextureHandle motionTexture;
    FluxionRHITextureViewHandle motionView;

    // THE FRAME'S DEPTH, HALVED AGAIN AND AGAIN -- see DepthPyramidPass.c.
    //
    // Every texel of every level holds the FARTHEST depth of the area it
    // covers, so a reader can ask "was everything drawn in this rectangle
    // nearer than this" by looking at one or two texels instead of
    // thousands. The occlusion culling asks exactly that.
    FluxionRHITextureHandle pyramidTexture;

    // Two views per level, because a level is written and read at
    // different moments and one view cannot be both at once: one to
    // attach, one to sample.
    FluxionRHITextureViewHandle pyramidTargetViews[FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS];
    FluxionRHITextureViewHandle pyramidSampleViews[FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS];

    // And one more that covers every level at once, for the reader that
    // chooses its level per object rather than per pass -- the occlusion
    // culling, which picks the level a given object's rectangle fits in.
    FluxionRHITextureViewHandle pyramidWholeView;
    u32 pyramidLevels;

    // WHETHER IT HAS EVER BEEN MADE READABLE. A texture nobody has drawn
    // into is in no state at all, and a dispatch that BINDS it says it is
    // readable whether or not the shader reads it -- so a new pyramid is
    // moved into that state once, before anything binds it.
    bool pyramidNeedsFirstTransition;

    // Whether the pyramid holds the frame just gone. False until one has
    // been built, and false again whenever the history itself is not
    // readable -- what it holds then is a screen that no longer exists.
    bool pyramidValid;

    // What the textures above were made for. A frame of a different size
    // makes them again, and makes the history unusable for that frame.
    u32 width;
    u32 height;

    // The camera of the frame before this one, and whether there was
    // one. Handed to the view each frame so that a shader can put this
    // frame's geometry where it was.
    FluxionMat4 previousViewProjection;
    bool hasPreviousViewProjection;

    // WHETHER LAST FRAME'S CONTENTS MAY BE READ AT ALL.
    //
    // False on the first frame and after a resize, because there is
    // nothing behind the reprojection then. A camera that TELEPORTED is
    // also nothing to reproject from -- but no threshold in metres can
    // tell a teleport from a fast pan, so the engine does not guess:
    // whoever moved the camera says so (Fluxion_Renderer_InvalidateHistory).
    bool valid;
} FluxionRenderHistory;

// --- FluxionRenderer (Renderer.cpp) -- shared for real, not opaque -------
//
// Unlike the other object kinds in this module, FluxionRenderer carries
// no C++-only members, and ForwardOpaquePass.c genuinely needs to read
// most of its fields (the accumulated draw list, the object buffer, the
// active view) every Execute call -- so this one is a real shared struct,
// same as FluxionRenderGraph itself in RenderGraphInternal.h.

typedef struct FluxionDebugDrawVertex
{
    FluxionVec3 position;
    FluxionVec4 color;
} FluxionDebugDrawVertex;

typedef struct FluxionRenderer
{
    bool alive;
    u32 generation;

    FluxionRHIDeviceHandle device;
    FluxionRHIQueueHandle queue;

    // --- current-frame state, valid between BeginFrame and EndFrame ------
    bool inFrame;
    FluxionRenderViewHandle currentView;

    // Everything this frame draws, and the grouping of it -- see
    // GPUScene.h. The renderer used to keep a packet per draw and a
    // matrix beside it; the scene keeps both, in the order the device
    // wants them, which is what lets a group of objects be one call.
    FluxionGPUSceneHandle gpuScene;
    FluxionRendererCullMode cullMode;

    // --- WHAT SURVIVES FROM ONE FRAME TO THE NEXT (RenderHistory.c) ------
    //
    // Owned by the renderer and not by the view, because a view is made
    // fresh every frame in this engine -- a history kept by something
    // that is rebuilt each frame is not a history.
    //
    // ONE HISTORY PER RENDERER, which is one camera. Two cameras drawn
    // through one renderer would reproject each other's frames; when
    // there is a second camera, this becomes a table keyed by view.
    FluxionRenderHistory history;

    // Incremented by ForwardOpaquePass.c's Execute for each draw call it
    // actually issues -- see Fluxion_Renderer_GetLastDrawCallCount.
    u32 lastDrawCallCount;

    FluxionRHIBindGroupLayoutHandle objectBindGroupLayout;

    // --- built-in debug-draw pipeline (see DebugDraw.c) -------------------
    //
    // A fixed FLUXION_RENDERER_MAX_DEBUG_VERTICES capacity, not grown by
    // doubling like the OBJECT buffer -- debug geometry is inherently
    // scratch, per-frame, and best-effort, so a hard cap (Fluxion_DebugDraw_*
    // silently drops anything past it) is simpler and good enough, unlike
    // the OBJECT buffer where dropping a real draw would be a visible bug.
    FluxionDebugDrawVertex debugVertices[FLUXION_RENDERER_MAX_DEBUG_VERTICES]; // CPU-side accumulation
    u32 debugVertexCount;                                                     // written so far this frame
    FluxionRHIBufferHandle debugVertexBuffer;                                 // GPU-visible mirror, fixed at FLUXION_RENDERER_MAX_DEBUG_VERTICES, created once
    FluxionRHIShaderHandle debugVertexShader;
    FluxionRHIShaderHandle debugFragmentShader;
    FluxionRHIPipelineHandle debugPipeline;
    FluxionRHIBindGroupLayoutHandle debugFrameBindGroupLayout;

    // The colour format debugPipeline was built against. It has to match
    // the colour attachment a frame is actually drawn into, and nothing
    // reachable from here says what that is -- a FluxionRHITextureViewHandle
    // carries no queryable format any more than it carries an extent --
    // so the caller says it (Fluxion_Renderer_SetDebugDrawColorFormat) and
    // this is what it last said.
    FluxionRHIFormat attachmentColorFormat;

    // The depth target the debug geometry is tested against. Unknown means
    // no depth attachment and no test, so it all draws over the top --
    // which is what a caller that never says otherwise gets.
    FluxionRHIFormat attachmentDepthFormat;

    // --- The sky ---------------------------------------------------------
    //
    // Built on first use rather than at startup: a renderer that never
    // draws a frame should not compile a shader, and the formats the
    // pipeline needs are not known until a pass says what it is drawing
    // into.
    FluxionShaderProgramHandle skyboxProgram;
    FluxionRHIBufferHandle skyboxVertexBuffer;
    FluxionRHIPipelineHandle skyboxPipeline;

    // Kept alongside the pipeline, and destroyed with it. The pipeline is
    // built from this layout, and a layout dropped the moment the
    // pipeline exists is an object nothing gives back -- which a device
    // says out loud at shutdown and nowhere earlier.
    FluxionRHIBindGroupLayoutHandle skyboxFrameLayout;

    // Turning an environment into nine coefficients. Owned here rather
    // than by a view, for the ordinary reason: a view may be made and
    // thrown away every frame, and building a shader program that often
    // is not something anyone would do on purpose.
    FluxionShaderProgramHandle irradianceProgram;
    FluxionRHIPipelineHandle irradiancePipeline;
    FluxionRHIBindGroupLayoutHandle irradianceLayout;
    FluxionRHIBindGroupHandle irradianceBindGroup;
    bool irradianceFailed;

    // Blurring the environment per roughness, and integrating the
    // split-sum table. Owned here for the same reason as the irradiance
    // pass above; the textures they fill belong to the view.
    FluxionShaderProgramHandle prefilterProgram;
    FluxionRHIPipelineHandle prefilterPipeline;
    FluxionRHIBindGroupLayoutHandle prefilterLayout;
    FluxionRHIBindGroupHandle prefilterBindGroups[FLUXION_RENDERER_PREFILTERED_MIPS];

    FluxionShaderProgramHandle dfgProgram;
    FluxionRHIPipelineHandle dfgPipeline;
    FluxionRHIBindGroupLayoutHandle dfgLayout;
    FluxionRHIBindGroupHandle dfgBindGroup;

    // One slot per prefiltered mip and a last one for the table pass,
    // written once: which mip a dispatch works on cannot come from a
    // buffer updated between dispatches on one command list -- every
    // dispatch would read the final value.
    FluxionRHIBufferHandle environmentParamsBuffer;

    // Where the compute passes write before the result is copied into a
    // texture: a compute shader here writes buffers, textures are what
    // draws read, and a recorded copy joins them.
    FluxionRHIBufferHandle environmentScratchBuffer;

    bool prefilterFailed;

    // Drawing the world from a light. Its own program rather than the
    // material's: this pass wants where a surface is and nothing about
    // what it looks like.
    FluxionShaderProgramHandle shadowProgram;
    FluxionRHIPipelineHandle shadowPipeline;
    FluxionRHIBindGroupLayoutHandle shadowGlobalLayout;

    // One per slice of the buffer below, so which shadow a draw uses is
    // settled by which group is bound rather than by rewriting the buffer
    // between draws that share a command list.
    FluxionRHIBindGroupHandle shadowGlobalBindGroups[FLUXION_RENDER_VIEW_MAX_SHADOWS];

    // The light's matrix, written from the processor once per pass. Small
    // enough to be a uniform buffer, and it has to be one: a shader reads
    // it every vertex.
    FluxionRHIBufferHandle shadowMatrixBuffer;

    // The vertex layout the pipeline above was built for. A pipeline is
    // built against one layout, so a mesh shaped differently needs
    // another -- kept rather than rebuilt every draw.
    FluxionRHIVertexLayout shadowVertexLayout;
    bool shadowPipelineBuilt;

    bool shadowFailed;

    // Drawing where every pixel WAS. Its own program for the same reason
    // the shadow pass has one: it wants two placements of a vertex and
    // nothing about what the surface looks like.
    FluxionShaderProgramHandle motionProgram;
    FluxionRHIPipelineHandle motionPipeline;

    // The frame layout this pass built its pipeline against. Kept because
    // it has to be given back: a layout made and forgotten is an object
    // the device still holds at shutdown, which one backend reports and
    // the others do not.
    FluxionRHIBindGroupLayoutHandle motionFrameLayout;

    // Filling the depth pyramid: one small full-screen draw per level.
    // Its own program, pipeline and three-vertex buffer, kept because
    // rebuilding any of them per frame is what exhausts a descriptor
    // pool -- measured, on the cull pass, in this same milestone.
    FluxionShaderProgramHandle pyramidProgram;
    FluxionRHIPipelineHandle pyramidPipeline;
    FluxionRHIBindGroupLayoutHandle pyramidLayout;
    FluxionRHISamplerHandle pyramidSampler;
    FluxionRHIBufferHandle pyramidVertexBuffer;
    FluxionRHIBufferHandle pyramidUniformBuffer;
    FluxionRHIBindGroupHandle pyramidBindGroups[FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS];

    // --- what the scene is drawn into, and what turns it into a picture ---
    //
    // Sixteen bits a channel, because what the passes write is an amount
    // of light and light has no upper bound. The screen's own target
    // comes later, from the resolve -- see PostProcessPass.c.
    FluxionRHITextureHandle sceneColorTexture;
    FluxionRHITextureViewHandle sceneColorView;       // to attach
    FluxionRHITextureViewHandle sceneColorSampleView; // to read
    u32 sceneColorWidth;
    u32 sceneColorHeight;

    // A texture nobody has drawn into is in no state at all: the first
    // barrier of its life must say so rather than claim it was already a
    // render target.
    bool sceneColorIsUndefined;

    FluxionShaderProgramHandle postProgram;
    FluxionRHIPipelineHandle postPipeline;
    FluxionRHIBindGroupLayoutHandle postLayout;
    FluxionRHISamplerHandle postSampler;
    FluxionRHIBufferHandle postVertexBuffer;
    FluxionRHIBufferHandle postUniformBuffer;
    FluxionRHIBindGroupHandle postBindGroup;
    FluxionRHITextureViewHandle postBoundSceneColor;
    FluxionRHITextureViewHandle postBoundGlow;
    FluxionRHITextureViewHandle postBoundExposure;

    // The format of what the resolve writes -- the screen's, not the
    // scene's. Told by the application, because only it knows what it
    // asked its swapchain for.
    FluxionRHIFormat postOutputFormat;
    bool postFailed;

    // --- what glows, and how far it spreads -----------------------------
    //
    // A chain of ever smaller pictures: the first holds only what is
    // bright enough to glow, and each one after it is the one before,
    // halved and blurred a little. Small blurs on small pictures are
    // wide blurs on the frame, and summing them on the way back up is
    // what makes the falloff smooth rather than stepped.
    FluxionRHITextureHandle bloomTexture;
    FluxionRHITextureViewHandle bloomTargetViews[FLUXION_RENDERER_MAX_BLOOM_LEVELS];
    FluxionRHITextureViewHandle bloomSampleViews[FLUXION_RENDERER_MAX_BLOOM_LEVELS];
    u32 bloomLevels;
    u32 bloomWidth;
    u32 bloomHeight;
    bool bloomNeedsFirstTransition;

    FluxionShaderProgramHandle bloomDownProgram;
    FluxionShaderProgramHandle bloomUpProgram;
    FluxionRHIPipelineHandle bloomDownPipeline;
    FluxionRHIPipelineHandle bloomUpPipeline;
    FluxionRHIBindGroupLayoutHandle bloomLayout;
    FluxionRHIBufferHandle bloomUniformBuffer;

    // One per step of the chain: every level on the way down, then every
    // level but the last on the way up.
    FluxionRHIBindGroupHandle bloomBindGroups[FLUXION_RENDERER_MAX_BLOOM_STEPS];
    FluxionRHITextureViewHandle bloomBoundSceneColor;

    bool bloomEnabled;
    bool bloomFailed;

    // --- WHAT THE FRAME'S OWN BRIGHTNESS TURNED OUT TO BE ----------------
    //
    // The chain measures it and the pair below remembers it. Both are the
    // RENDERER'S rather than a view's: the whole point of an adapting
    // exposure is that it carries from one frame to the next, and a view
    // is made fresh every frame in the ordinary way of writing a loop.
    FluxionRHITextureHandle luminanceTexture;
    FluxionRHITextureViewHandle luminanceTargetViews[FLUXION_RENDERER_LUMINANCE_LEVELS];
    FluxionRHITextureViewHandle luminanceSampleViews[FLUXION_RENDERER_LUMINANCE_LEVELS];
    bool luminanceNeedsFirstTransition;

    // Two one-texel textures, taking it in turns. exposureCurrent names
    // the one holding the answer the resolve should read; the other is
    // what the next frame writes into.
    FluxionRHITextureHandle exposureTextures[FLUXION_RENDERER_EXPOSURE_HISTORY];
    FluxionRHITextureViewHandle exposureTargetViews[FLUXION_RENDERER_EXPOSURE_HISTORY];
    FluxionRHITextureViewHandle exposureSampleViews[FLUXION_RENDERER_EXPOSURE_HISTORY];
    u32 exposureCurrent;
    bool exposureNeedsFirstTransition;

    // Whether there is a previous exposure to move away from at all. The
    // first frame has none -- what is in the texture is whatever the
    // allocator handed over -- so it takes the measured answer whole.
    bool exposureHasHistory;

    FluxionShaderProgramHandle luminanceProgram;
    FluxionShaderProgramHandle exposureAdaptProgram;
    FluxionRHIPipelineHandle luminancePipeline;
    FluxionRHIPipelineHandle exposureAdaptPipeline;
    FluxionRHIBindGroupLayoutHandle luminanceLayout;
    FluxionRHIBindGroupLayoutHandle exposureAdaptLayout;
    FluxionRHIBufferHandle luminanceUniformBuffer;
    FluxionRHIBufferHandle exposureAdaptUniformBuffer;
    FluxionRHIBindGroupHandle luminanceBindGroups[FLUXION_RENDERER_LUMINANCE_LEVELS];
    FluxionRHIBindGroupHandle exposureAdaptBindGroups[FLUXION_RENDERER_EXPOSURE_HISTORY];
    FluxionRHITextureViewHandle luminanceBoundSceneColor;

    bool autoExposureEnabled;
    bool autoExposureFailed;

    // --- WHICH WAY EVERY PIXEL FACES, AND HOW ROUGH IT IS -----------------
    //
    // Written before anything is lit, by the same materials that will be
    // lit -- see Pass/NormalRoughness.jsl for why "before" rather than
    // "beside". Occlusion and reflections both read it.
    FluxionRHITextureHandle prepassTexture;
    FluxionRHITextureViewHandle prepassTargetView;
    FluxionRHITextureViewHandle prepassSampleView;
    u32 prepassWidth;
    u32 prepassHeight;
    bool prepassNeedsFirstTransition;

    bool prepassEnabled;
    bool prepassFailed;

    // --- HOW MUCH OF THE SKY REACHES EACH PIXEL --------------------------
    //
    // The distances, halved a few times so a far sample can be read from
    // a level where far is a texel; then the search itself; then the
    // noise taken back out. All of it the renderer's, because all of it
    // is the size of the window rather than the size of anything a view
    // describes.
    FluxionRHITextureHandle viewDepthTexture;
    FluxionRHITextureViewHandle viewDepthTargetViews[FLUXION_RENDERER_VIEW_DEPTH_LEVELS];
    FluxionRHITextureViewHandle viewDepthLevelViews[FLUXION_RENDERER_VIEW_DEPTH_LEVELS];

    // One view over every level, because the search names the level it
    // wants per sample and a view of one level cannot reach the others.
    FluxionRHITextureViewHandle viewDepthChainView;
    FluxionRHIBindGroupHandle viewDepthBindGroups[FLUXION_RENDERER_VIEW_DEPTH_LEVELS];
    bool viewDepthNeedsFirstTransition;

    FluxionRHITextureHandle occlusionTextures[FLUXION_RENDERER_OCCLUSION_TEXTURES];
    FluxionRHITextureViewHandle occlusionTargetViews[FLUXION_RENDERER_OCCLUSION_TEXTURES];
    FluxionRHITextureViewHandle occlusionSampleViews[FLUXION_RENDERER_OCCLUSION_TEXTURES];
    u32 occlusionWidth;
    u32 occlusionHeight;
    bool occlusionNeedsFirstTransition;

    FluxionShaderProgramHandle viewDepthProgram;
    FluxionShaderProgramHandle occlusionProgram;
    FluxionShaderProgramHandle denoiseProgram;
    FluxionRHIPipelineHandle viewDepthPipeline;
    FluxionRHIPipelineHandle occlusionPipeline;
    FluxionRHIPipelineHandle denoisePipeline;
    FluxionRHIBindGroupLayoutHandle viewDepthLayout;
    FluxionRHIBindGroupLayoutHandle occlusionLayout;
    FluxionRHIBindGroupLayoutHandle denoiseLayout;
    FluxionRHIBufferHandle viewDepthUniformBuffer;
    FluxionRHIBufferHandle occlusionUniformBuffer;
    FluxionRHIBufferHandle denoiseUniformBuffer;
    FluxionRHIBindGroupHandle occlusionBindGroup;
    FluxionRHIBindGroupHandle denoiseBindGroup;
    FluxionRHISamplerHandle occlusionSampler;
    FluxionRHITextureViewHandle occlusionBoundDepthView;

    bool occlusionEnabled;
    bool occlusionFailed;

    // --- THE PASS THAT SMOOTHS THE STAIRCASE -----------------------------
    //
    // With it on, the resolve writes into fxaaTexture instead of the
    // target the view named, and this pass writes that target -- so the
    // scene reaches the screen through one more step, and the step is
    // the one that reads the finished picture.
    //
    // Both are the same format, which is what makes turning this on and
    // off safe while running: a pipeline is built for a format, and the
    // resolve's does not change when its destination does.
    FluxionRHITextureHandle fxaaTexture;
    FluxionRHITextureViewHandle fxaaTargetView;
    FluxionRHITextureViewHandle fxaaSampleView;
    u32 fxaaWidth;
    u32 fxaaHeight;
    bool fxaaNeedsFirstTransition;

    FluxionShaderProgramHandle fxaaProgram;
    FluxionRHIPipelineHandle fxaaPipeline;
    FluxionRHIBindGroupLayoutHandle fxaaLayout;
    FluxionRHIBufferHandle fxaaUniformBuffer;
    FluxionRHIBindGroupHandle fxaaBindGroup;
    FluxionRHITextureViewHandle fxaaBoundSource;
    FluxionRHIFormat fxaaBuiltForFormat;

    bool fxaaEnabled;
    bool fxaaFailed;

    // WHETHER THE SCENE GOES THROUGH THE CHAIN AT ALL. Off by default,
    // because it changes what every pipeline drawing the scene must be
    // built for: with the chain on, the scene lands in a target of
    // sixteen-bit light, and a pipeline built for an eight-bit screen
    // cannot write into it.
    bool postEnabled;

    // The depth view the groups above were built against. When the frame
    // is resized its depth is a different texture, and a group still
    // pointing at the old one reads a texture nobody draws into.
    FluxionRHITextureViewHandle pyramidBoundDepthView;
    bool pyramidFailed;
    FluxionRHIVertexLayout motionVertexLayout;
    bool motionPipelineBuilt;
    bool motionFailed;

    FluxionRHIFormat skyboxColorFormat;
    FluxionRHIFormat skyboxDepthFormat;

    // Remembered so the attempt is made once. A shader that failed to
    // build will fail again, and saying so once a frame buries whatever
    // said it first.
    bool skyboxFailed;

    // Said once, not once a frame, when the renderer has not been told
    // what it draws into.
    bool skyboxFormatsReported;
} FluxionRenderer;

// --- The sky ---------------------------------------------------------------

bool FluxionRendererInternal_Skybox_EnsureResources(FluxionRenderer* renderer);

// Draws it, building the pipeline if the formats have changed. Called
// from inside a pass that has already begun rendering.
void FluxionRendererInternal_Skybox_Draw(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                         FluxionRHIBindGroupHandle frameBindGroup,
                                         FluxionRHIFormat colorFormat, FluxionRHIFormat depthFormat, bool hasDepthAttachment);

void FluxionRendererInternal_Skybox_Destroy(FluxionRenderer* renderer);

// The pipeline a sky wants, which is not the one a surface wants: it
// never writes depth, it keeps what is equal or nearer -- so it fills
// exactly where nothing else drew -- and it culls nothing, because a
// triangle covering the screen has no meaningful facing.
FluxionRHIPipelineHandle FluxionRendererInternal_ShaderProgram_CreateSkyboxPipeline(
    FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program, const FluxionRHIVertexLayout* vertexLayout,
    FluxionRHIFormat colorFormat, FluxionRHIFormat depthFormat, FluxionRHIBindGroupLayoutHandle* outFrameLayout);

// --- The environment, turned into the nine numbers a surface reads -------

bool FluxionRendererInternal_Irradiance_EnsureResources(FluxionRenderer* renderer);

// Records the dispatch that fills the view's coefficients from its
// environment. UpdateEnvironment decides WHEN -- it holds the dirty
// flag, because more than one pass answers to it.
void FluxionRendererInternal_Irradiance_Project(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                                FluxionRenderViewHandle view);

void FluxionRendererInternal_Irradiance_Destroy(FluxionRenderer* renderer);

// Records the dispatches and copies that fill the view's prefiltered
// chain from its environment, one mip per roughness. Same contract as
// the projection above: the caller says when.
void FluxionRendererInternal_Prefilter_Project(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                               FluxionRenderViewHandle view);

// Fills the view's split-sum table the first time it is asked, and does
// nothing after: the table depends on no sky, so it cannot go stale.
void FluxionRendererInternal_Dfg_Compute(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                         FluxionRenderViewHandle view);

void FluxionRendererInternal_Prefilter_Destroy(FluxionRenderer* renderer);

// What a view holds for this. The renderer owns the programs; what they
// fill belongs to whichever view is being drawn.
FluxionRHIBufferHandle FluxionRendererInternal_RenderView_GetIrradianceBuffer(FluxionRenderViewHandle view);
FluxionRHITextureViewHandle FluxionRendererInternal_RenderView_GetEnvironmentView(FluxionRenderViewHandle view);
FluxionRHISamplerHandle FluxionRendererInternal_RenderView_GetEnvironmentSampler(FluxionRenderViewHandle view);
FluxionRHITextureHandle FluxionRendererInternal_RenderView_GetPrefilteredTexture(FluxionRenderViewHandle view);
FluxionRHITextureHandle FluxionRendererInternal_RenderView_GetDfgTexture(FluxionRenderViewHandle view);

// True exactly once per view. Asking marks the table as filled, so ask
// only on the way to filling it.
bool FluxionRendererInternal_RenderView_TakeDfgWanted(FluxionRenderViewHandle view);

// Whether the prefiltered chain was already filled once, and marks it
// filled either way -- ask only on the way to filling it. The refill's
// barriers must name the texture's real previous state.
bool FluxionRendererInternal_RenderView_MarkPrefilteredFilled(FluxionRenderViewHandle view);

// --- Drawing the world from a light -----------------------------------------

// The registered "ShadowPass" render graph pass type; userData is the
// same FluxionRenderer* the forward pass takes. It writes the depth
// target named "ShadowPass.Atlas", which the caller imports from
// FluxionRendererInternal_RenderView_GetShadowAtlasTexture.
void FluxionShadowPass_Setup(FluxionRenderGraphBuilder* builder, void* userData);
void FluxionShadowPass_Execute(FluxionRHICommandListHandle commandList, void* userData);
void FluxionRendererInternal_Shadow_Destroy(FluxionRenderer* renderer);

// What the pass draws into. The texture itself is public -- a caller
// building a graph has to import it -- but the view over it is this
// module's own business.
FluxionRHITextureViewHandle FluxionRendererInternal_RenderView_GetShadowAtlasView(FluxionRenderViewHandle view);

// How many shadows this view draws this frame, and where each one goes.
// False from the second when the index is past the count, in which case
// nothing is written.
u32 FluxionRendererInternal_RenderView_GetShadowCount(FluxionRenderViewHandle view);
bool FluxionRendererInternal_RenderView_GetShadow(FluxionRenderViewHandle view, u32 index,
                                                  FluxionMat4* outLightViewProjection, FluxionShadowAtlasTile* outTile);

// True once, after the environment changed. Asking CLEARS it: the
// projection is recorded in answer to this, and a flag left set would
// have it recorded again on every frame that followed.
bool FluxionRendererInternal_RenderView_TakeEnvironmentDirty(FluxionRenderViewHandle view);

// --- What survives from one frame to the next (RenderHistory.c) -------------

// Makes or remakes the history's textures for a frame of this size.
// Called once per frame, before anything reads or writes them.
bool FluxionRendererInternal_History_Begin(FluxionRenderer* renderer, u32 width, u32 height);
void FluxionRendererInternal_History_Destroy(FluxionRenderer* renderer);

// --- Cross-file internal accessors ------------------------------------------
//
// ShaderProgram/Material/RenderPipeline keep their real records private
// to their own .cpp files; these are the only way another file in this
// module reaches into them.

// A shader's MATERIAL-group uniform member or texture, in the shape
// Material.cpp needs to build its own CPU-side parameter layout --
// deliberately POD (no ShaderIRModule/std::string in sight) so a plain-C
// translation unit could use it too, even though only Material.cpp does
// today.
typedef enum FluxionMaterialParameterKind
{
    FLUXION_MATERIAL_PARAMETER_FLOAT,
    FLUXION_MATERIAL_PARAMETER_VEC3,
    FLUXION_MATERIAL_PARAMETER_VEC4,
    FLUXION_MATERIAL_PARAMETER_OTHER,   // reflected but not settable through Material.h yet (e.g. int, mat4)
    FLUXION_MATERIAL_PARAMETER_TEXTURE,
} FluxionMaterialParameterKind;

#define FLUXION_MATERIAL_PARAMETER_NAME_LENGTH 63

typedef struct FluxionMaterialParameterInfo
{
    char name[FLUXION_MATERIAL_PARAMETER_NAME_LENGTH + 1];
    FluxionMaterialParameterKind kind;
    u32 offset; // byte offset into the CPU parameter buffer -- uniform kinds only
    u32 size;   // byte size within the CPU parameter buffer -- uniform kinds only
    u32 binding;         // texture binding -- FLUXION_MATERIAL_PARAMETER_TEXTURE only
    u32 samplerBinding;  // sampler binding -- FLUXION_MATERIAL_PARAMETER_TEXTURE only
} FluxionMaterialParameterInfo;

// Giving a loaded texture to a device.
//
// Exposed inside this module so the built-in one-pixel textures go
// through the very same upload every other texture does -- a second way
// to make a texture would be tested only by whether the picture looked
// right.
struct FluxionTextureAsset;
bool FluxionRendererInternal_TextureAsset_Upload(struct FluxionTextureAsset* asset, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue);

bool FluxionRendererInternal_ShaderProgram_IsCompute(FluxionShaderProgramHandle program);
FluxionRHIShaderHandle FluxionRendererInternal_ShaderProgram_GetVertexShader(FluxionShaderProgramHandle program);
FluxionRHIShaderHandle FluxionRendererInternal_ShaderProgram_GetFragmentShader(FluxionShaderProgramHandle program);
FluxionRHIShaderHandle FluxionRendererInternal_ShaderProgram_GetComputeShader(FluxionShaderProgramHandle program);
FluxionRHIBindGroupLayoutHandle FluxionRendererInternal_ShaderProgram_GetMaterialBindGroupLayout(FluxionShaderProgramHandle program);

// The name this program was created with, or "" if it was created
// without one. Never NULL. Used to build a pipeline name that means the
// same thing in the next run of the same build -- a pool index would not,
// and a driver's pipeline library keyed on one would never find anything
// again. A reload deliberately does not change it: the program is still
// the same program, which is the whole point of reloading in place.
const char* FluxionRendererInternal_ShaderProgram_GetDebugName(FluxionShaderProgramHandle program);

// --- Memory domains ---------------------------------------------------------
//
// Two domains, registered lazily and only when a host actually turned
// the memory tracker on: "Renderer" (the module's own long-lived GPU
// buffers -- object buffer, debug vertices, mesh vertex/index data) and
// "GPUUpload" under it (staging traffic: bytes that exist only to carry
// data to the GPU and are freed the moment the copy lands). The bytes
// recorded are GPU allocations, attributed explicitly -- there is no CPU
// allocator in that path for a tracking allocator to wrap.
void FluxionRendererInternal_EnsureMemoryDomains(void);
void FluxionRendererInternal_RecordGpuAlloc(bool upload, usize bytes);
void FluxionRendererInternal_RecordGpuFree(bool upload, usize bytes);

// Copies up to maxParams entries of this program's MATERIAL-group
// uniform members + textures into outParams, returning the number
// written; *outUniformBufferSize receives the merged Material uniform
// buffer's total byte size (0 if it has none).
u32 FluxionRendererInternal_ShaderProgram_GetMaterialParameters(FluxionShaderProgramHandle program, FluxionMaterialParameterInfo* outParams, u32 maxParams, u32* outUniformBufferSize);

FluxionRHIBindGroupHandle FluxionRendererInternal_Material_GetBindGroup(FluxionMaterialHandle material);

bool FluxionRendererInternal_MeshBuffer_Get(FluxionMeshBufferHandle mesh, FluxionRHIBufferHandle* outVertexBuffer, FluxionRHIBufferHandle* outIndexBuffer, u32* outVertexCount, u32* outIndexCount, bool* outUse16BitIndices, FluxionRHIVertexLayout* outVertexLayout);

// What the mesh occupies, in its own space. Asked by whatever decides
// whether a draw is worth issuing at all.
bool FluxionRendererInternal_MeshBuffer_GetBounds(FluxionMeshBufferHandle mesh, FluxionAABB* outBounds);

bool FluxionRendererInternal_RenderTarget_Get(FluxionRenderTargetHandle target, FluxionRHITextureViewHandle* outColorViews, u32* outColorViewCount, FluxionRHITextureViewHandle* outDepthView);

bool FluxionRendererInternal_RenderView_Get(FluxionRenderViewHandle view, FluxionRenderTargetHandle* outRenderTarget, u32* outLayerMask, FluxionRHIBindGroupHandle* outFrameBindGroup);

// Where this view looks from and how far, in the shape a cull wants:
// the view-projection AS THIS SIDE WRITES IT (not the transposed copy
// the shaders read), the eye in world space, and the distance beyond
// which nothing is drawn.
bool FluxionRendererInternal_RenderView_GetCamera(FluxionRenderViewHandle view, FluxionMat4* outViewProjection,
                                                  FluxionVec3* outCameraPosition, f32* outCullDistance);

// The view's own viewport rect, in pixels -- ForwardOpaquePass.c and
// Renderer.cpp's debug-draw pass both need this for
// FluxionRHIRenderingDesc::width/height (BeginRendering's render area),
// which neither can otherwise derive from a RenderTarget alone (a
// FluxionRHITextureViewHandle carries no queryable extent of its own).
bool FluxionRendererInternal_RenderView_GetViewport(FluxionRenderViewHandle view, FluxionViewport* outViewport);

// Resolves (building + caching on first use for this vertex layout) the
// real graphics pipeline for a draw using this vertex layout. Builds its
// own FRAME/OBJECT bind group layouts from the canonical descs above
// (a backend is free to dedupe identical-shape layouts -- see RHI.h --
// so this doesn't need the exact same layout handle a RenderView/
// Renderer created for itself, only the same shape) and combines them
// with the program's own MATERIAL layout.
FluxionRHIPipelineHandle FluxionRendererInternal_RenderPipeline_Resolve(FluxionRenderPipelineHandle pipeline, FluxionRHIDeviceHandle device, const FluxionRHIVertexLayout* vertexLayout);

// The same, for the pass that records the surface before it is lit.
// Invalid when this pipeline was never given a program for that pass --
// which is how a draw is left out of it rather than drawn wrongly.
FluxionRHIPipelineHandle FluxionRendererInternal_RenderPipeline_ResolvePrepass(FluxionRenderPipelineHandle pipeline, FluxionRHIDeviceHandle device, const FluxionRHIVertexLayout* vertexLayout, FluxionRHIFormat colorFormat);

// Throws away every pipeline built from this program, across every
// FluxionRenderPipeline that names it. Resolve above bakes the program's
// shaders into a native pipeline object once and never looks at them
// again, so a program whose shaders have been replaced leaves every one
// of those objects describing shaders that no longer exist. They are
// rebuilt on next use, at the cost of one build each.
//
// Called by Fluxion_ShaderProgram_Reload, which is the only thing that
// can replace a live program's shaders.
void FluxionRendererInternal_RenderPipeline_InvalidateVariantsUsingProgram(FluxionShaderProgramHandle program);

// --- "DepthPyramidPass" registered pass type (DepthPyramidPass.c) ----------

void FluxionDepthPyramidPass_Setup(FluxionRenderGraphBuilder* builder, void* userData);
void FluxionDepthPyramidPass_Execute(FluxionRHICommandListHandle commandList, void* userData);
void FluxionRendererInternal_DepthPyramid_Destroy(FluxionRenderer* renderer);

// Makes the pyramid's texture and its per-level views for a frame of this
// size, and answers whether there is one to draw into.
bool FluxionRendererInternal_History_EnsurePyramid(FluxionRenderer* renderer, u32 width, u32 height);

// --- The scene's own target, and the pass that resolves it (PostProcessPass.c)
//
// THE SCENE IS NEVER DRAWN STRAIGHT TO THE SCREEN. Every pass that draws
// the world writes light into this texture, and one pass at the end turns
// that into what a monitor shows. Anything that wants to read the picture
// as light -- what glows, how bright the frame is -- reads this.
#define FLUXION_RENDERER_SCENE_COLOR_FORMAT FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT
bool FluxionRendererInternal_PostProcess_EnsureSceneColor(FluxionRenderer* renderer, u32 width, u32 height);
void FluxionRendererInternal_PostProcess_ReleaseSceneColor(FluxionRenderer* renderer);
void FluxionRendererInternal_PostProcess_Shutdown(FluxionRenderer* renderer);

// The three vertices every fullscreen pass draws, made once for the whole
// renderer. Any pass that draws one has to ask for it before it does --
// see the definition for what happened when only the resolve did.
void FluxionRendererInternal_EnsureFullscreenTriangle(FluxionRenderer* renderer);

// --- what the frame's own brightness turned out to be (AutoExposure.c) ---

// Measures this frame and moves the camera part of the way towards what
// it asks for. Returns whether there is an answer for the resolve to
// read; false is the ordinary case of the whole thing being switched off,
// and then the exposure the view asked for is all there is.
//
// CALLED BEFORE THE RESOLVE AND AFTER THE SCENE, because it reads the
// scene and the resolve reads it.
bool FluxionRendererInternal_AutoExposure_Build(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList);

// The one texel holding the answer, for the resolve to bind.
FluxionRHITextureViewHandle FluxionRendererInternal_AutoExposure_GetView(const FluxionRenderer* renderer);

void FluxionRendererInternal_AutoExposure_Release(FluxionRenderer* renderer);

// --- the surface, recorded before it is lit (NormalRoughnessPass.c) ------

// Draws the frame's opaque geometry into the normal-roughness texture and
// into the view's own depth attachment. Returns whether there is anything
// recorded for later passes to read.
//
// CALLED BEFORE THE FORWARD PASS AND AFTER THE SCENE IS UPLOADED. The
// depth it leaves behind is the depth the forward pass tests against.
bool FluxionRendererInternal_NormalRoughness_Build(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList);

FluxionRHITextureViewHandle FluxionRendererInternal_NormalRoughness_GetView(const FluxionRenderer* renderer);
FluxionRHITextureHandle FluxionRendererInternal_NormalRoughness_GetTexture(const FluxionRenderer* renderer);
void FluxionRendererInternal_NormalRoughness_Release(FluxionRenderer* renderer);

// --- how much of the sky reaches each pixel (AmbientOcclusionPass.c) -----

// Turns the frame's depth into distances, searches the horizon around
// every pixel, and takes the noise back out. Returns whether there is an
// answer for the lighting to read.
//
// CALLED AFTER THE SURFACES ARE RECORDED AND BEFORE ANYTHING IS LIT --
// it reads the first and the second reads it.
bool FluxionRendererInternal_Occlusion_Build(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                             FluxionRHITextureViewHandle depthView);

FluxionRHITextureViewHandle FluxionRendererInternal_Occlusion_GetView(const FluxionRenderer* renderer);
FluxionRHITextureHandle FluxionRendererInternal_Occlusion_GetTexture(const FluxionRenderer* renderer);
void FluxionRendererInternal_Occlusion_Release(FluxionRenderer* renderer);
void FluxionRendererInternal_Occlusion_Destroy(FluxionRenderer* renderer);

// What the view says the search should do, and the two matrices it needs
// to turn a place on the screen into a place in front of the eye.
bool FluxionRendererInternal_RenderView_GetOcclusion(FluxionRenderViewHandle view, FluxionVec4* outSettings);
bool FluxionRendererInternal_RenderView_GetMatrices(FluxionRenderViewHandle view, FluxionMat4* outView, FluxionMat4* outProjection);

// --- smoothing the staircase (FXAAPass.c) --------------------------------

// Makes ready what this pass needs at the given size, and says whether
// the resolve should write into it rather than into the screen. False is
// the ordinary case of the whole thing being switched off.
bool FluxionRendererInternal_FXAA_Begin(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList, u32 width, u32 height);

// Where the resolve writes when the above said yes.
FluxionRHITextureViewHandle FluxionRendererInternal_FXAA_GetTargetView(const FluxionRenderer* renderer);

// Reads what the resolve wrote and writes the target the view named.
void FluxionRendererInternal_FXAA_Resolve(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                          FluxionRHITextureViewHandle outputView, u32 width, u32 height);

void FluxionRendererInternal_FXAA_Release(FluxionRenderer* renderer);

// What the scene passes attach instead of the target the caller gave the
// view. Invalid before the first frame has sized it.
FluxionRHITextureViewHandle FluxionRendererInternal_PostProcess_GetSceneColorView(const FluxionRenderer* renderer);

void FluxionPostProcessPass_Setup(FluxionRenderGraphBuilder* builder, void* userData);
void FluxionPostProcessPass_Execute(FluxionRHICommandListHandle commandList, void* userData);

// Exposure, white point and whether the output needs the display's
// transfer function -- the same three the frame constants carry, asked
// for by the one pass that now acts on them.
bool FluxionRendererInternal_RenderView_GetToneMapping(FluxionRenderViewHandle view, FluxionVec4* outToneMapping);

// Said by the renderer, once a frame, when the resolve at the end will
// apply the camera and the curve instead of every surface shader.
void FluxionRendererInternal_RenderView_SetToneMappingDeferred(FluxionRenderViewHandle view, bool deferred);

// What glows and by how much: x the threshold, y the knee, z how much of
// the glow is added back. The same three the caller set on the view.
bool FluxionRendererInternal_RenderView_GetBloom(FluxionRenderViewHandle view, FluxionVec4* outBloom);

// The grading, as the four vectors the resolve reads: the balance
// (temperature, tint, contrast, saturation) and the three-way control.
// Already turned from distances into multipliers -- see the definition.
// What the measured brightness is allowed to do to the camera: the key,
// the speed, and the two ends of the range -- already filled in with the
// engine's own where the description said nothing. The frame's length
// comes back separately because zero is a real answer for it and not a
// missing one.
bool FluxionRendererInternal_RenderView_GetAutoExposure(FluxionRenderViewHandle view, FluxionVec4* outParams,
                                                       f32* outDeltaSeconds);

bool FluxionRendererInternal_RenderView_GetGrading(FluxionRenderViewHandle view, FluxionVec4* outBalance,
                                                  FluxionVec4* outLift, FluxionVec4* outGamma, FluxionVec4* outGain);

// Said by the pipeline asset, through the renderer.
void FluxionRendererInternal_Renderer_SetBloomEnabled(FluxionRenderer* renderer, bool enabled);

// --- "MotionVectorPass" registered pass type (MotionVectorPass.c) ----------

void FluxionMotionVectorPass_Setup(FluxionRenderGraphBuilder* builder, void* userData);
void FluxionMotionVectorPass_Execute(FluxionRHICommandListHandle commandList, void* userData);
void FluxionRendererInternal_MotionVector_Destroy(FluxionRenderer* renderer);

// --- "ForwardOpaquePass" registered pass type (ForwardOpaquePass.c) --------
//
// Registered once per FluxionRenderer instance (see Fluxion_Renderer_Create)
// under the name "ForwardOpaquePass"; userData for every node instance is
// always a FluxionRenderer* (see Fluxion_Renderer_GetForwardOpaquePassUserData).

void FluxionForwardOpaquePass_Setup(FluxionRenderGraphBuilder* builder, void* userData);
void FluxionForwardOpaquePass_Execute(FluxionRHICommandListHandle commandList, void* userData);

#ifdef __cplusplus
}
#endif
