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

    // A storage buffer rather than an array in the uniform block above.
    // An array would need a maximum written into the shader, and that
    // maximum would be a number somebody has to raise -- and raising it
    // costs every frame that does not use it, because a uniform block is
    // paid for whether it is full or not.
    desc.entries[3].binding = 3;
    desc.entries[3].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[3].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entryCount = 4;
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
