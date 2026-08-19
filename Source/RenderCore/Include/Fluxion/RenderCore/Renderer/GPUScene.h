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

#ifdef __cplusplus
extern "C" {
#endif

// WHAT THE FRAME LOOKS LIKE FROM THE DEVICE'S SIDE.
//
// Everything drawn this frame, in one buffer the GPU indexes into, plus
// the handful of draws that read it. The point is the second number:
// a thousand objects that share a mesh and a material are a THOUSAND
// ROWS and ONE DRAW CALL, where before they were a thousand of each --
// and a thousand bind groups built and torn down on the way.
//
// The order is not the order things were added in. Objects are grouped
// by what it costs to switch between them (pipeline, then material, then
// mesh) and a group's rows are laid next to each other, because a run of
// rows is exactly what one instanced draw can cover.

FLUXION_DEFINE_HANDLE(FluxionGPUSceneHandle);

// One row of the object buffer -- THE SAME BYTES Fluxion/Object.jsl
// declares as FluxionObject. Two statements of one layout, and the
// shader's is the one a driver reads, so a field added here without
// being added there is not an error anywhere: it is a matrix read from
// the wrong offset.
typedef struct FluxionGPUSceneObject
{
    // Already transposed for the shading languages, exactly like the
    // frame constants -- see Fluxion_RenderView_UpdateFrameConstants.
    FluxionMat4 model;

    // WHERE IT WAS LAST FRAME, and the row is twice the size for it.
    //
    // That is the price of everything temporal, paid here rather than in
    // a second buffer: a motion vector is this frame's position against
    // last frame's, and a pass that had to fetch the two from different
    // places would need both to be indexed the same way anyway.
    //
    // An object nobody tells about its past carries the same matrix
    // twice, which reads as standing still.
    FluxionMat4 previousModel;
} FluxionGPUSceneObject;

// One draw: what to bind, and which run of rows it covers.
typedef struct FluxionGPUSceneBatch
{
    FluxionMeshBufferHandle mesh;
    FluxionMaterialHandle material;
    FluxionRenderPipelineHandle pipeline;

    // WHICH LEVEL OF DETAIL, and it is part of what makes a batch a
    // batch: two objects of the same mesh seen from different distances
    // read different ranges of the same index buffer, and one command
    // carries one range. So they are two batches -- of one mesh, one
    // material, one pipeline, and two levels.
    u32 lodIndex;

    // Where this batch's rows start in the object buffer, and how many
    // there are. These describe what the batch HOLDS.
    u32 firstObject;
    u32 objectCount;

    // And what it DRAWS: where its slice of the visible list begins, and
    // how many of its objects survived the cull. The shader adds its own
    // zero-based instance index to the first of these -- see
    // Fluxion/Object.jsl -- and the indirect command's instance count is
    // the second.
    //
    // Two pairs rather than one, because they answer different
    // questions: a batch whose objects are all off screen still holds
    // them, it just draws none of them.
    u32 firstVisible;
    u32 visibleCount;

    // Everything in this batch, in world space, as one sphere.
    //
    // A SPHERE RATHER THAN A BOX because it does not have to be turned
    // with anything, and PER BATCH rather than per object because a
    // batch is what a draw can now skip: a pass that wants to throw
    // work away has this granularity to do it at, and no finer until
    // the culling moves to the GPU.
    FluxionVec3 boundsCentre;
    f32 boundsRadius;
} FluxionGPUSceneBatch;

// How far apart two batches' uniform slices sit. A constant-buffer view
// must start on a 256-byte boundary on one of the three backends, and
// one rule for all of them beats a per-backend difference nobody sees
// until that backend is the one running.
#define FLUXION_GPU_SCENE_BATCH_UNIFORM_STRIDE 256

// WHO DECIDES what this frame can see.
//
// The same three tests either way, and the same two places the answer
// goes -- what changes is where the arithmetic happens. A frame drawn
// one way and the same frame drawn the other must produce the same set,
// which is what Test_GPUCullGPU checks.
typedef enum FluxionGPUSceneCullMode
{
    // On the processor, during the upload. What everything did before
    // there was a choice, and so the zero value.
    FLUXION_GPU_SCENE_CULL_CPU = 0,

    // On the device, in a compute pass recorded just before the frame
    // draws. Falls back to the processor -- saying so once -- if the
    // shader will not build.
    FLUXION_GPU_SCENE_CULL_GPU,
} FluxionGPUSceneCullMode;

// WHAT THE FRAME CAN SEE, and so what it need not draw.
//
// All of it is the view's: the frustum comes from its matrices, the
// distance and the layer mask are its own. Handed over once a frame,
// before the upload, because everything below is decided per object and
// nothing about it changes while that is happening.
typedef struct FluxionGPUSceneCullDesc
{
    FluxionGPUSceneCullMode mode;

    // As this side writes it, NOT the transposed copy the shaders read --
    // the frustum planes are derived from it.
    FluxionMat4 viewProjection;

    FluxionVec3 cameraPosition;

    // Beyond this, nothing is drawn. Zero means no limit, which is what
    // a view that never mentioned distance gets.
    f32 cullDistance;

    // An object is drawn when it shares a layer with the view. Zero on
    // either side means "every layer", so a caller that never thought
    // about layers is not quietly excluded from its own frame.
    u32 layerMask;

    // False turns the whole thing off: every object is drawn, which is
    // what a program with no camera at all (a test, a tool) wants.
    bool enabled;

    // --- and what the frame before this one could see -------------------
    //
    // The depth pyramid of the PREVIOUS frame, with the camera that drew
    // it. An object that was behind everything a frame ago is not drawn
    // this frame -- which is one frame late by construction, and cheap:
    // four texel reads instead of a second pass over the geometry.
    //
    // Left invalid (or with occlusion false) when there is no previous
    // frame to be hidden by, which is what the history itself answers.
    FluxionRHITextureViewHandle occlusionPyramid;
    FluxionRHISamplerHandle occlusionSampler;
    FluxionMat4 previousViewProjection;
    f32 pyramidWidth;
    f32 pyramidHeight;
    u32 pyramidLevels;
    bool occlusionEnabled;
} FluxionGPUSceneCullDesc;

FluxionGPUSceneHandle Fluxion_GPUScene_Create(FluxionRHIDeviceHandle device);
void Fluxion_GPUScene_Destroy(FluxionGPUSceneHandle scene);

// Forgets last frame. Everything below describes one frame only.
void Fluxion_GPUScene_Begin(FluxionGPUSceneHandle scene);

// One thing to draw. False when the frame is already full (the limit is
// said rather than silently dropped, because a draw that vanished is a
// missing object nobody can account for).
//
// `transform` is an ordinary row-major world matrix; what reaches the
// device is its transpose, worked out here so that no caller has to
// remember to.
bool Fluxion_GPUScene_Add(FluxionGPUSceneHandle scene, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                          FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform);

// One thing to draw, with the layers it belongs to. The call above is
// this one with every layer.
// The same, and which level of detail this object is drawn at. Out of
// range for the mesh is clamped to its coarsest level rather than
// refused: a level that does not exist is a caller's arithmetic being
// wrong about a mesh, and the object still has to be drawn.
bool Fluxion_GPUScene_AddDetailed(FluxionGPUSceneHandle scene, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                                  FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform, u32 layerMask, u32 lodIndex);

// And where it was when the frame before this one was drawn. NULL means
// "where it is now", which is what a caller with no memory of last frame
// can honestly say, and what everything did before there was a past.
bool Fluxion_GPUScene_AddMoving(FluxionGPUSceneHandle scene, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                                FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform, const FluxionMat4* previousTransform,
                                u32 layerMask, u32 lodIndex);

bool Fluxion_GPUScene_AddLayered(FluxionGPUSceneHandle scene, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material,
                                 FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform, u32 layerMask);

// What this frame can see. Cleared by Begin, so a caller that says
// nothing gets a frame that draws everything.
void Fluxion_GPUScene_SetCulling(FluxionGPUSceneHandle scene, const FluxionGPUSceneCullDesc* cull);

// How many objects were added, and how many of them survived the cull.
// Two numbers rather than one, because their RATIO is the only thing
// that says whether the culling did anything.
//
// ON THE DEVICE'S PATH THIS NUMBER IS A FRAME BEHIND. What survived is
// counted in a buffer the device writes, and reading it in the frame
// that wrote it would mean waiting for the device in the middle of the
// frame -- for a number nothing draws with. It is zero until the first
// answer has come back.
u32 Fluxion_GPUScene_GetVisibleCount(FluxionGPUSceneHandle scene);

// Groups what was added, lays the rows out in that order, writes one
// indirect command per group, builds the OBJECT bind group each group is
// drawn with, and records the copies that put all of it where the GPU
// can read it.
//
// Call it inside a recording, AFTER the last Add of the frame and BEFORE
// anything that draws -- the same rule, for the same reason, as
// Fluxion_RenderView_UploadLighting.
//
// `objectLayout` is the OBJECT-frequency bind group layout every pass
// binds; the scene builds one group per batch from it rather than every
// pass building its own, so that two passes drawing the same frame
// cannot disagree about what a batch is bound to.
void Fluxion_GPUScene_Upload(FluxionGPUSceneHandle scene, FluxionRHICommandListHandle commandList,
                             FluxionRHIBindGroupLayoutHandle objectLayout);

u32 Fluxion_GPUScene_GetObjectCount(FluxionGPUSceneHandle scene);

// Whether the instance counts the frame draws with were worked out on
// the device.
//
// What a pass needs it for: a batch's visibleCount on THIS side is what
// the processor found, and on the device's path the processor found
// nothing -- the number is in the command buffer. A pass that read the
// zero and skipped the batch would draw an empty frame, so when this is
// true every batch is submitted and a batch with nothing in it costs one
// command that draws no instances.
bool Fluxion_GPUScene_CountsOnDevice(FluxionGPUSceneHandle scene);

// Valid after Upload, and until the next Begin.
u32 Fluxion_GPUScene_GetBatchCount(FluxionGPUSceneHandle scene);
const FluxionGPUSceneBatch* Fluxion_GPUScene_GetBatch(FluxionGPUSceneHandle scene, u32 batchIndex);

// The object rows, as a storage buffer the vertex stage reads.
FluxionRHIBufferHandle Fluxion_GPUScene_GetObjectBuffer(FluxionGPUSceneHandle scene);

// Which rows this frame draws, in draw order -- what
// Fluxion/Object.jsl calls visibleObjects.
FluxionRHIBufferHandle Fluxion_GPUScene_GetVisibleBuffer(FluxionGPUSceneHandle scene);

// One FluxionRHIDrawIndexedIndirectCommand per batch, in batch order.
FluxionRHIBufferHandle Fluxion_GPUScene_GetIndirectBuffer(FluxionGPUSceneHandle scene);

// One OBJECT-frequency uniform slice per batch, holding that batch's
// first-object index, FLUXION_GPU_SCENE_BATCH_UNIFORM_STRIDE apart.
// The same batch, bound to draw EVERY row it holds rather than only the
// rows the camera can see.
//
// What it is for: a shadow's casters are not the camera's business. An
// object behind the eye still throws a shadow into the picture, so a
// pass drawing into a shadow map binds this and draws the batch's whole
// object count, and does its own culling against the light.
FluxionRHIBindGroupHandle Fluxion_GPUScene_GetBatchAllRowsBindGroup(FluxionGPUSceneHandle scene, u32 batchIndex);

FluxionRHIBufferHandle Fluxion_GPUScene_GetBatchUniformBuffer(FluxionGPUSceneHandle scene);

// What a pass binds to draw batch `batchIndex`.
//
// LIVES UNTIL THE FRAME AFTER THIS ONE, and that is the point. Built and
// destroyed around each draw, a bind group's descriptors are freed while
// the command list that points at them is still only recorded -- and the
// next one allocated takes the same descriptor slots, so every draw in
// the list ends up reading the LAST batch's binding. That is not a
// validation error on any backend: it is one object drawn many times in
// the same place.
FluxionRHIBindGroupHandle Fluxion_GPUScene_GetBatchBindGroup(FluxionGPUSceneHandle scene, u32 batchIndex);

#ifdef __cplusplus
}
#endif
