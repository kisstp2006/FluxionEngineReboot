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
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionAABB
{
    FluxionVec3 min;
    FluxionVec3 max;
} FluxionAABB;

FLUXION_DEFINE_HANDLE(FluxionMeshBufferHandle);

// HOW MANY LEVELS OF DETAIL ONE MESH MAY CARRY.
//
// Eight, and the number is a shape rather than a limit anybody reaches:
// each level is roughly half the triangles of the one before it, so
// eight levels is a mesh that has run out of triangles long before the
// camera has run out of distance.
#define FLUXION_MESH_BUFFER_MAX_LEVELS 8

// ONE LEVEL OF DETAIL: A RANGE OF THE INDEX BUFFER, NOT A MESH OF ITS
// OWN.
//
// The vertices are shared and the indices sit end to end in one buffer,
// so choosing a level is choosing where to start reading and how much --
// which is exactly the pair an indirect draw command already carries.
// The drawing path therefore does not change at all when a mesh gains
// levels; only which two numbers go into the command.
typedef struct FluxionMeshLevel
{
    u32 firstIndex;
    u32 indexCount;

    // From how far away this level is the one used. Levels are given
    // nearest first, so this rises from one to the next, and LEVEL
    // ZERO'S IS ALWAYS ZERO whatever is written here -- something has to
    // be drawn when the camera is on top of it.
    f32 minDistance;
} FluxionMeshLevel;

typedef struct FluxionMeshBufferDesc
{
    const void* vertexData;
    usize vertexDataSize;
    const void* indexData;
    usize indexDataSize;
    // RHI.h has no standalone index-format enum to reuse --
    // Fluxion_RHI_CommandList_SetIndexBuffer itself takes a plain
    // use16BitIndices bool, so this mirrors that instead of inventing a
    // format enum the RHI doesn't have.
    bool use16BitIndices;
    FluxionRHIVertexLayout vertexLayout;
    FluxionAABB bounds;

    // The levels, nearest first. NONE MEANS ONE: a mesh that says
    // nothing about levels is a mesh with a single level covering the
    // whole index buffer from distance zero, which is what every mesh
    // was before there were levels.
    //
    // A level whose range falls outside the index buffer, or one that
    // begins nearer than the level before it, is refused rather than
    // drawn -- indices read past the end of a buffer are geometry
    // nobody can explain.
    FluxionMeshLevel levels[FLUXION_MESH_BUFFER_MAX_LEVELS];
    u32 levelCount;

    const char* debugName; // optional, may be NULL
} FluxionMeshBufferDesc;

// Uploads vertexData/indexData into GPU_ONLY buffers through a CPU
// staging buffer, a queue submission, and a fence wait (the same
// staging pattern Samples/ForwardRendererDemo's manual upload code
// uses) -- synchronous: blocks until the upload completes, so the
// staging buffer can be torn down before returning.
FluxionMeshBufferHandle Fluxion_MeshBuffer_Create(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, const FluxionMeshBufferDesc* desc);
void Fluxion_MeshBuffer_Destroy(FluxionMeshBufferHandle mesh);

// --- Which level a distance asks for --------------------------------------

u32 Fluxion_MeshBuffer_GetLevelCount(FluxionMeshBufferHandle mesh);
bool Fluxion_MeshBuffer_GetLevel(FluxionMeshBufferHandle mesh, u32 level, FluxionMeshLevel* outLevel);

// The level this mesh should be drawn at, seen from `cameraPosition`
// with `world` for its transform.
//
// THE DISTANCE IS TO THE NEAR SIDE OF THE MESH, not to its origin --
// the same rule the culling measures by, and for the same reason: a
// large object whose centre is far away can still be filling the screen.
// A mesh with one level always answers zero, so a caller never has to
// ask whether this mesh has levels at all.
u32 Fluxion_MeshBuffer_SelectLevel(FluxionMeshBufferHandle mesh, FluxionVec3 cameraPosition, const FluxionMat4* world);

#ifdef __cplusplus
}
#endif
