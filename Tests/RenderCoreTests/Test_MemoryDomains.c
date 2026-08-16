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

#include "TestFramework.h"

#include <Fluxion/Foundation/Memory/MemoryTracker.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>

// The renderer's memory domains only mean something if the numbers move
// when work happens and settle when it is undone. A mesh upload is the
// smallest complete story: staging bytes appear under GPUUpload and are
// gone by the time Create returns; the vertex/index bytes appear under
// Renderer and stay until the mesh is destroyed; and because GPUUpload is
// a child, its traffic also rolls up into the Renderer total.
void Test_MemoryDomains_Run(TestContext* ctx)
{
#if FLUXION_MEMORY_TRACKING
    Fluxion_MemoryTracker_Init();

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests.MemoryDomains", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    FluxionRHIAdapterHandle adapter;
    Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1);
    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    typedef struct Vertex { f32 position[3]; } Vertex;
    Vertex vertices[3] = { { { 0, 0, 0 } }, { { 1, 0, 0 } }, { { 0, 1, 0 } } };
    u16 indices[3] = { 0, 1, 2 };

    FluxionMeshBufferDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertexData = vertices;
    desc.vertexDataSize = sizeof(vertices);
    desc.indexData = indices;
    desc.indexDataSize = sizeof(indices);
    desc.use16BitIndices = true;
    desc.vertexLayout.attributeCount = 1;
    desc.vertexLayout.attributes[0].location = 0;
    desc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    desc.vertexLayout.stride = sizeof(Vertex);
    desc.bounds.min = (FluxionVec3){ 0, 0, 0 };
    desc.bounds.max = (FluxionVec3){ 1, 1, 0 };
    desc.debugName = "Test_MemoryDomains.Mesh";

    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));

    FluxionMemoryStatistics rendererStats = Fluxion_MemoryTracker_GetStatistics(FLUXION_MEMORY_DOMAIN_ID_OF(Renderer));
    FluxionMemoryStatistics uploadStats = Fluxion_MemoryTracker_GetStatistics(FLUXION_MEMORY_DOMAIN_ID_OF(GPUUpload));

    // The mesh's own GPU bytes are standing right now.
    TEST_CHECK(ctx, rendererStats.currentBytes >= sizeof(vertices) + sizeof(indices));

    // The staging traffic came and went inside Create -- an upload that
    // never happened and one that leaked would both fail here, from
    // opposite directions.
    TEST_CHECK(ctx, uploadStats.allocationCount == 1);
    TEST_CHECK(ctx, uploadStats.deallocationCount == 1);
    TEST_CHECK(ctx, uploadStats.currentBytes == 0);
    TEST_CHECK(ctx, uploadStats.peakBytes >= sizeof(vertices) + sizeof(indices));

    // Roll-up: the parent saw the upload traffic too. Its peak covers
    // the moment mesh bytes and staging bytes stood at once.
    TEST_CHECK(ctx, rendererStats.peakBytes >= uploadStats.peakBytes);

    Fluxion_MeshBuffer_Destroy(mesh);
    rendererStats = Fluxion_MemoryTracker_GetStatistics(FLUXION_MEMORY_DOMAIN_ID_OF(Renderer));
    TEST_CHECK(ctx, rendererStats.currentBytes == 0);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
    Fluxion_MemoryTracker_Shutdown();
#else
    FLUXION_UNUSED(ctx);
#endif
}
