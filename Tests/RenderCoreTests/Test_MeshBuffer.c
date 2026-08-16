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

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>

typedef struct Test_MeshBuffer_Vertex
{
    float position[3];
} Test_MeshBuffer_Vertex;

void Test_MeshBuffer_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);

    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(instance, adapters, 1);

    FluxionRHIDeviceDesc deviceDesc = { 0 };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    Test_MeshBuffer_Vertex vertices[3] = {
        { { 0.0f, 0.5f, 0.0f } },
        { { 0.5f, -0.5f, 0.0f } },
        { { -0.5f, -0.5f, 0.0f } },
    };
    unsigned short indices[3] = { 0, 1, 2 };

    FluxionRHIVertexLayout vertexLayout;
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    vertexLayout.attributes[0].offset = 0;
    vertexLayout.attributeCount = 1;
    vertexLayout.stride = sizeof(Test_MeshBuffer_Vertex);

    FluxionMeshBufferDesc desc;
    desc.vertexData = vertices;
    desc.vertexDataSize = sizeof(vertices);
    desc.indexData = indices;
    desc.indexDataSize = sizeof(indices);
    desc.use16BitIndices = true;
    desc.vertexLayout = vertexLayout;
    desc.bounds.min = (FluxionVec3){ -0.5f, -0.5f, 0.0f };
    desc.bounds.max = (FluxionVec3){ 0.5f, 0.5f, 0.0f };
    desc.debugName = "Test_MeshBuffer.Triangle";

    FluxionMeshBufferHandle indexedMesh = Fluxion_MeshBuffer_Create(device, queue, &desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(indexedMesh));

    FluxionMeshBufferDesc nonIndexedDesc = desc;
    nonIndexedDesc.indexData = NULL;
    nonIndexedDesc.indexDataSize = 0;
    FluxionMeshBufferHandle nonIndexedMesh = Fluxion_MeshBuffer_Create(device, queue, &nonIndexedDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(nonIndexedMesh));

    Fluxion_MeshBuffer_Destroy(indexedMesh);
    Fluxion_MeshBuffer_Destroy(nonIndexedMesh);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
