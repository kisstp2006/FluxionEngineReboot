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
    const char* debugName; // optional, may be NULL
} FluxionMeshBufferDesc;

// Uploads vertexData/indexData into GPU_ONLY buffers through a CPU
// staging buffer, a queue submission, and a fence wait (the same
// staging pattern Samples/ForwardRendererDemo's manual upload code
// uses) -- synchronous: blocks until the upload completes, so the
// staging buffer can be torn down before returning.
FluxionMeshBufferHandle Fluxion_MeshBuffer_Create(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, const FluxionMeshBufferDesc* desc);
void Fluxion_MeshBuffer_Destroy(FluxionMeshBufferHandle mesh);

#ifdef __cplusplus
}
#endif
