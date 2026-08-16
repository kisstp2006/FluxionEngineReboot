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

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RHI/Format.h>
#include <Fluxion/RHI/RHI.h>

#include <string.h>

// Six square faces, made and viewed as one thing.
//
// A cube map is not a new kind of memory -- it is six array layers, and
// on every backend here the image itself is an ordinary array. What makes
// it a cube is asked for at creation on one backend, at view time on
// another, and in the target on a third, which is exactly the sort of
// difference that produces a texture nobody can sample and no message
// about why.
//
// So this makes one, views it as a cube, uploads a distinct colour into
// each face, and checks that the pieces exist. What it cannot check
// without a shader is which face is which -- that is what the sample's
// sky is for, where a face in the wrong place is a sky that is visibly
// turned round.

#define FLUXION_TEST_CUBE_SIZE 16

static void Fluxion_Test_CubemapOnBackend(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
{
    if (!Fluxion_RHI_IsBackendAvailable(backend))
    {
        FLUXION_LOG_WARN("RHITests", "%s is not in this build -- skipping its cube-map check.", backendName);
        return;
    }

    FluxionRHIInstanceDesc instanceDesc = { "RHITests", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backend, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_WARN("RHITests", "No usable %s loader -- skipping its cube-map check.", backendName);
        return;
    }

    FluxionRHIAdapterHandle adapters[8];
    if (Fluxion_RHI_EnumerateAdapters(instance, adapters, 8) == 0)
    {
        FLUXION_LOG_WARN("RHITests", "No %s adapters -- skipping its cube-map check.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    if (!FLUXION_HANDLE_IS_VALID(device))
    {
        FLUXION_LOG_WARN("RHITests", "%s device creation failed -- skipping its cube-map check.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    const u64 errorsBefore = Fluxion_RHI_Validation_GetErrorCount();

    FluxionRHITextureDesc cubeDesc;
    memset(&cubeDesc, 0, sizeof(cubeDesc));
    cubeDesc.width = FLUXION_TEST_CUBE_SIZE;
    cubeDesc.height = FLUXION_TEST_CUBE_SIZE;
    cubeDesc.depth = 1;
    cubeDesc.mipLevels = 1;
    cubeDesc.arrayLayers = FLUXION_RHI_CUBE_FACE_COUNT;
    cubeDesc.sampleCount = 1;
    cubeDesc.format = FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT; // what an environment is stored in
    cubeDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    cubeDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    cubeDesc.debugName = "RHITests.Cube";
    cubeDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_CUBE;

    FluxionRHITextureHandle cube = Fluxion_RHI_CreateTexture(device, &cubeDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(cube));

    if (FLUXION_HANDLE_IS_VALID(cube))
    {
        // Viewed as a cube: this is the view a shader samples by
        // direction, and on one of these backends it is the ONLY place
        // the texture becomes a cube at all.
        FluxionRHITextureViewDesc cubeViewDesc;
        memset(&cubeViewDesc, 0, sizeof(cubeViewDesc));
        cubeViewDesc.texture = cube;
        cubeViewDesc.format = cubeDesc.format;
        cubeViewDesc.mipLevelCount = 1;
        cubeViewDesc.arrayLayerCount = FLUXION_RHI_CUBE_FACE_COUNT;
        cubeViewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_CUBE;

        FluxionRHITextureViewHandle cubeView = Fluxion_RHI_CreateTextureView(device, &cubeViewDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(cubeView));

        // And the same texture viewed as one ordinary layer, which is
        // what rendering into a single face needs. Both views of one
        // texture at once, because that is the arrangement the
        // prefiltering will want.
        FluxionRHITextureViewDesc faceViewDesc = cubeViewDesc;
        faceViewDesc.baseArrayLayer = 2;
        faceViewDesc.arrayLayerCount = 1;
        faceViewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

        FluxionRHITextureViewHandle faceView = Fluxion_RHI_CreateTextureView(device, &faceViewDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(faceView));

        // A different colour into every face, through the ordinary upload
        // path. Six copies, one per layer -- the same call a
        // single-layer texture uses, with the layer that changes.
        const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(cubeDesc.format, FLUXION_TEST_CUBE_SIZE);
        const usize paddedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                     FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
        const usize faceBytes = paddedRowBytes * FLUXION_TEST_CUBE_SIZE;
        const usize placementAligned = (faceBytes + FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) /
                                       FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT;

        FluxionRHIBufferDesc stagingDesc;
        stagingDesc.size = placementAligned * FLUXION_RHI_CUBE_FACE_COUNT;
        stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
        stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
        stagingDesc.debugName = "RHITests.Cube.Staging";
        FluxionRHIBufferHandle staging = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(staging));

        u8* mapped = (u8*)Fluxion_RHI_MapBuffer(staging);
        TEST_CHECK(ctx, mapped != NULL);
        if (mapped != NULL)
        {
            for (usize i = 0; i < stagingDesc.size; ++i) mapped[i] = (u8)(i * 13u + 5u);
            Fluxion_RHI_UnmapBuffer(staging);
        }

        FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
        Fluxion_RHI_CommandList_Begin(commandList);

        FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        FluxionRHIBarrier toCopy;
        toCopy.texture = cube;
        toCopy.buffer = noBuffer;
        toCopy.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
        toCopy.after = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
        Fluxion_RHI_CommandList_Barrier(commandList, &toCopy, 1);

        for (u32 face = 0; face < FLUXION_RHI_CUBE_FACE_COUNT; ++face)
        {
            Fluxion_RHI_CommandList_CopyBufferToTexture(commandList, staging, placementAligned * face, cube, 0, face);
        }

        FluxionRHIBarrier toRead = toCopy;
        toRead.before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
        toRead.after = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
        Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);
        Fluxion_RHI_CommandList_End(commandList);

        FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
        Fluxion_RHI_Queue_Submit(queue, &commandList, 1, fence);
        TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));

        Fluxion_RHI_DestroyFence(fence);
        Fluxion_RHI_DestroyCommandList(commandList);
        Fluxion_RHI_DestroyBuffer(staging);
        Fluxion_RHI_DestroyTextureView(faceView);
        Fluxion_RHI_DestroyTextureView(cubeView);
        Fluxion_RHI_DestroyTexture(cube);
        Fluxion_RHI_Device_CollectGarbage(device);

        FLUXION_LOG_INFO("RHITests", "%s: a cube map was made, viewed both ways and filled face by face.", backendName);
    }

    // --- A shape that is not a cube is refused ---------------------------
    //
    // Refused rather than quietly made into an array: a description that
    // says CUBE and brings four layers is a mistake somewhere else, and
    // making an array out of it would move the failure to whichever
    // shader sampled it -- where there is nothing left to point at.
    FluxionRHITextureDesc wrongLayers = cubeDesc;
    wrongLayers.arrayLayers = 4;
    wrongLayers.debugName = "RHITests.Cube.WrongLayers";
    FluxionRHITextureHandle refusedLayers = Fluxion_RHI_CreateTexture(device, &wrongLayers);
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(refusedLayers));

    FluxionRHITextureDesc notSquare = cubeDesc;
    notSquare.height = FLUXION_TEST_CUBE_SIZE * 2;
    notSquare.debugName = "RHITests.Cube.NotSquare";
    FluxionRHITextureHandle refusedShape = Fluxion_RHI_CreateTexture(device, &notSquare);
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(refusedShape));

    // Given back if a backend handed one over anyway. A check that leaks
    // what it expected to be refused fails twice: once on the check, and
    // again as an allocator complaining at shutdown about memory nobody
    // freed -- and the second failure buries the first.
    if (FLUXION_HANDLE_IS_VALID(refusedLayers)) Fluxion_RHI_DestroyTexture(refusedLayers);
    if (FLUXION_HANDLE_IS_VALID(refusedShape)) Fluxion_RHI_DestroyTexture(refusedShape);
    Fluxion_RHI_Device_CollectGarbage(device);

    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == errorsBefore);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

void Test_Cubemap_Run(TestContext* ctx)
{
    Fluxion_Test_CubemapOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    Fluxion_Test_CubemapOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");

    // OpenGL needs a real context through a window, and there is none
    // here -- the same reason Test_OpenGLBackend skips. Its cube path is
    // exercised by the sample's sky instead.
}
