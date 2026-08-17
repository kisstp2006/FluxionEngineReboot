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
// tiles. Two by two at these numbers -- enough for the sun's four
// cascades, which is what the sun needs and all it needs. Raising the
// atlas is the one number to change; the allocator and everything above
// it are told the size rather than assuming it.
#define FLUXION_RENDERER_SHADOW_ATLAS_SIZE 2048
#define FLUXION_RENDERER_SHADOW_TILE_SIZE 1024

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

    // A storage buffer rather than an array in the uniform block above.
    // An array would need a maximum written into the shader, and that
    // maximum would be a number somebody has to raise -- and raising it
    // costs every frame that does not use it, because a uniform block is
    // paid for whether it is full or not.
    desc.entries[9].binding = 9;
    desc.entries[9].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[9].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // The sky, as nine coefficients. A second storage buffer rather than
    // more fields in the block at binding 0, because a compute pass is
    // what fills it: a uniform buffer would have to be written from the
    // processor, and by then the numbers are already on the device.
    desc.entries[10].binding = 10;
    desc.entries[10].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[10].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    // Where each shadow's light looks from, and which tile holds it.
    desc.entries[11].binding = 11;
    desc.entries[11].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[11].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entryCount = 12;
    desc.debugName = "Fluxion.Renderer.FrameBindGroupLayout";
    return desc;
}

// OBJECT is a uniform (constant) buffer, not a storage buffer -- it's a
// single small read-only struct per draw (the world transform), never
// written by any shader. A storage buffer's SRV+UAV pair (see
// D3D12Pipeline.cpp's dual-range comment) requires the backing resource
// to allow unordered access, which D3D12 forbids on the CPU-visible
// (upload-heap) memory this buffer actually needs -- a uniform buffer
// has no such restriction and no per-element-stride ambiguity either.
static inline FluxionRHIBindGroupLayoutDesc FluxionRendererInternal_MakeObjectLayoutDesc(void)
{
    FluxionRHIBindGroupLayoutDesc desc = { };
    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX | FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    desc.entryCount = 1;
    desc.debugName = "Fluxion.Renderer.ObjectBindGroupLayout";
    return desc;
}

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

    FluxionDrawPacket packets[FLUXION_RENDERER_MAX_DRAW_PACKETS_PER_FRAME];
    FluxionMat4 packetTransforms[FLUXION_RENDERER_MAX_DRAW_PACKETS_PER_FRAME];
    u32 packetCount;

    // Incremented by ForwardOpaquePass.c's Execute for each draw call it
    // actually issues -- see Fluxion_Renderer_GetLastDrawCallCount.
    u32 lastDrawCallCount;

    // Per-draw world transform storage buffer, grown by doubling
    // whenever packetCount would exceed its current element capacity --
    // never shrinks. Uploaded from packetTransforms at EndFrame.
    FluxionRHIBufferHandle objectBuffer;
    u32 objectBufferCapacity; // in FluxionMat4 elements
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

bool FluxionRendererInternal_RenderTarget_Get(FluxionRenderTargetHandle target, FluxionRHITextureViewHandle* outColorViews, u32* outColorViewCount, FluxionRHITextureViewHandle* outDepthView);

bool FluxionRendererInternal_RenderView_Get(FluxionRenderViewHandle view, FluxionRenderTargetHandle* outRenderTarget, u32* outLayerMask, FluxionRHIBindGroupHandle* outFrameBindGroup);

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
