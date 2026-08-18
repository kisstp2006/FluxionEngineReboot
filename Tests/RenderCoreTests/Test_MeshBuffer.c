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

#include <string.h>

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>

typedef struct Test_MeshBuffer_Vertex
{
    float position[3];
} Test_MeshBuffer_Vertex;

// WHICH LEVEL A DISTANCE ASKS FOR, and what a mesh refuses to be made
// with.
//
// The distances are the whole of the LOD as far as anything outside this
// file is concerned: what comes back is an index, and an index picks a
// range of the index buffer. So these are the numbers that decide what a
// frame draws, and they are checked ON THE BOUNDARY as well as either
// side of it -- "at exactly ten metres" is the answer somebody will
// eventually have to defend.
static void Test_MeshBuffer_Levels(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue,
                                   const FluxionMeshBufferDesc* base)
{
    // NO SIZE AT ALL, on purpose: with a radius of zero the distance to
    // the mesh's near side IS the camera's distance, so the threshold
    // below can be checked exactly on the boundary rather than near it.
    // A mesh with size is the case after this one.
    FluxionMeshBufferDesc desc = *base;
    desc.bounds.min = (FluxionVec3){ 0.0f, 0.0f, 0.0f };
    desc.bounds.max = (FluxionVec3){ 0.0f, 0.0f, 0.0f };
    desc.debugName = "Test_MeshBuffer.Levels";

    desc.levels[0] = (FluxionMeshLevel){ 0, 3, 0.0f };
    desc.levels[1] = (FluxionMeshLevel){ 0, 2, 10.0f };
    desc.levelCount = 2;

    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));
    if (!FLUXION_HANDLE_IS_VALID(mesh)) return;

    TEST_CHECK(ctx, Fluxion_MeshBuffer_GetLevelCount(mesh) == 2);

    FluxionMeshLevel level;
    TEST_CHECK(ctx, Fluxion_MeshBuffer_GetLevel(mesh, 1, &level));
    TEST_CHECK(ctx, level.firstIndex == 0 && level.indexCount == 2 && level.minDistance == 10.0f);

    TEST_CHECK(ctx, Fluxion_MeshBuffer_SelectLevel(mesh, (FluxionVec3){ 0.0f, 0.0f, 9.9f }, NULL) == 0);

    // EXACTLY ON THE BOUNDARY, which belongs to the level that begins
    // there -- the answer somebody eventually has to defend, so it is
    // written down rather than left to whichever comparison was typed.
    TEST_CHECK(ctx, Fluxion_MeshBuffer_SelectLevel(mesh, (FluxionVec3){ 0.0f, 0.0f, 10.0f }, NULL) == 1);
    TEST_CHECK(ctx, Fluxion_MeshBuffer_SelectLevel(mesh, (FluxionVec3){ 0.0f, 0.0f, 40.0f }, NULL) == 1);

    // On top of it is level zero rather than a negative distance
    // wrapping round to the coarsest level.
    TEST_CHECK(ctx, Fluxion_MeshBuffer_SelectLevel(mesh, (FluxionVec3){ 0.0f, 0.0f, 0.0f }, NULL) == 0);

    // THE TRANSFORM COUNTS. The same mesh, moved twenty metres away by
    // its world matrix and looked at from the origin, is past the
    // threshold -- the levels are chosen in the world, not in the mesh's
    // own space.
    FluxionMat4 moved = Fluxion_Mat4_Identity();
    moved.m[2][3] = -20.0f;
    TEST_CHECK(ctx, Fluxion_MeshBuffer_SelectLevel(mesh, (FluxionVec3){ 0.0f, 0.0f, 0.0f }, &moved) == 1);

    Fluxion_MeshBuffer_Destroy(mesh);

    // --- and the size of the thing counts ---------------------------------
    //
    // THE DISTANCE IS TO THE NEAR SIDE. A metre-wide cube reaches about
    // 0.87 metres from its own centre (that is the half-diagonal, which
    // is what a sphere around a box has to be), so a camera 10.5 metres
    // from its centre is only 9.63 from the cube itself -- and the
    // coarser level does not start yet. Measured from the centre it
    // would, which is the mistake this case exists to catch.
    FluxionMeshBufferDesc sized = desc;
    sized.bounds.min = (FluxionVec3){ -0.5f, -0.5f, -0.5f };
    sized.bounds.max = (FluxionVec3){ 0.5f, 0.5f, 0.5f };
    sized.debugName = "Test_MeshBuffer.SizedLevels";

    FluxionMeshBufferHandle sizedMesh = Fluxion_MeshBuffer_Create(device, queue, &sized);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(sizedMesh));
    if (FLUXION_HANDLE_IS_VALID(sizedMesh))
    {
        TEST_CHECK(ctx, Fluxion_MeshBuffer_SelectLevel(sizedMesh, (FluxionVec3){ 0.0f, 0.0f, 10.5f }, NULL) == 0);
        TEST_CHECK(ctx, Fluxion_MeshBuffer_SelectLevel(sizedMesh, (FluxionVec3){ 0.0f, 0.0f, 11.0f }, NULL) == 1);
        Fluxion_MeshBuffer_Destroy(sizedMesh);
    }

    // --- and what is refused ---------------------------------------------
    //
    // Each of these draws something nobody described if it is accepted,
    // and none of them is a compile error.

    FluxionMeshBufferDesc pastTheEnd = desc;
    pastTheEnd.levels[1] = (FluxionMeshLevel){ 2, 4, 10.0f }; // indices 2..6 of three
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_MeshBuffer_Create(device, queue, &pastTheEnd)));

    FluxionMeshBufferDesc outOfOrder = desc;
    outOfOrder.levels[1] = (FluxionMeshLevel){ 0, 2, 0.0f }; // no further away than level zero
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_MeshBuffer_Create(device, queue, &outOfOrder)));

    FluxionMeshBufferDesc tooMany = desc;
    tooMany.levelCount = FLUXION_MESH_BUFFER_MAX_LEVELS + 1;
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_MeshBuffer_Create(device, queue, &tooMany)));
}

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

    // ZEROED FIRST, and then filled: a zero in this description means
    // "the engine's own answer" -- one level of detail covering the
    // whole index buffer, here -- and a field left as whatever was on
    // the stack is a mesh describing indices it does not have.
    FluxionMeshBufferDesc desc;
    memset(&desc, 0, sizeof(desc));
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

    // A mesh that said nothing about levels has one, covering the whole
    // index buffer. Checked rather than assumed: everything written
    // before there were levels depends on it.
    TEST_CHECK(ctx, Fluxion_MeshBuffer_GetLevelCount(indexedMesh) == 1);

    FluxionMeshLevel whole;
    TEST_CHECK(ctx, Fluxion_MeshBuffer_GetLevel(indexedMesh, 0, &whole));
    TEST_CHECK(ctx, whole.firstIndex == 0 && whole.indexCount == 3 && whole.minDistance == 0.0f);
    TEST_CHECK(ctx, !Fluxion_MeshBuffer_GetLevel(indexedMesh, 1, &whole));

    // And it answers level zero from everywhere, so a caller never has
    // to ask first whether a mesh has levels at all.
    TEST_CHECK(ctx, Fluxion_MeshBuffer_SelectLevel(indexedMesh, (FluxionVec3){ 0.0f, 0.0f, 1000.0f }, NULL) == 0);

    Test_MeshBuffer_Levels(ctx, device, queue, &desc);

    Fluxion_MeshBuffer_Destroy(indexedMesh);
    Fluxion_MeshBuffer_Destroy(nonIndexedMesh);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
