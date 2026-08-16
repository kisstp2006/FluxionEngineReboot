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

// Uploads a block-compressed texture through the real backends.
//
// The size arithmetic is covered on its own in Test_Format.c; what this
// adds is the part no unit test can reach -- whether the layout the
// contract describes is the layout the driver reads. A wrong
// bufferRowLength is not a crash and not a wrong return value: it is a
// picture with its rows sheared, which nothing here could see. What makes
// this a real check is that both the Vulkan validation layers and the GL
// debug callback are wired to assert on an error, so a disagreement about
// the layout stops the process rather than passing quietly.

// Thirty-two wide on purpose. At sixteen, a BC7 row is four blocks --
// sixty-four bytes, padded up to the 256-byte row alignment -- and the
// padded row happens to work out to exactly sixteen texels, which is the
// width. So a stride computed in blocks where it should be in texels
// would agree with the right answer, and this check would pass while
// being wrong. At thirty-two it does not agree.
#define FLUXION_TEST_BC_WIDTH  32
#define FLUXION_TEST_BC_HEIGHT 32
#define FLUXION_TEST_BC_MIPS   6 // 32, 16, 8, 4, 2, 1 -- the last three are all one block

static void Fluxion_Test_UploadOneCompressedTexture(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue,
                                                    FluxionRHIFormat format, const char* formatName, const char* backendName)
{
    const FluxionRHIFormatInfo info = Fluxion_RHI_GetFormatInfo(format);
    TEST_CHECK(ctx, info.compressed);

    FluxionRHITextureDesc textureDesc;
    memset(&textureDesc, 0, sizeof(textureDesc));
    textureDesc.width = FLUXION_TEST_BC_WIDTH;
    textureDesc.height = FLUXION_TEST_BC_HEIGHT;
    textureDesc.depth = 1;
    textureDesc.mipLevels = FLUXION_TEST_BC_MIPS;
    textureDesc.arrayLayers = 1;
    textureDesc.sampleCount = 1;
    textureDesc.format = format;
    textureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    textureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    textureDesc.debugName = "RHITests.CompressedUpload";

    // Not a failure: no hardware is required to have every compressed
    // format. Asked before creating rather than discovered by creating,
    // because a backend told to make a texture it cannot reports an
    // error, and an error is a fault rather than an answer.
    if (!Fluxion_RHI_Device_IsFormatSupported(device, format))
    {
        FLUXION_LOG_WARN("RHITests", "%s: this adapter has no %s -- skipping its upload check.", backendName, formatName);
        return;
    }

    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(texture));
    if (!FLUXION_HANDLE_IS_VALID(texture)) return;

    // The layout the contract asks for, worked out the way a caller is
    // meant to: rows of BLOCKS, padded to the row alignment, each level
    // starting on the placement alignment.
    usize levelStagingOffset[FLUXION_TEST_BC_MIPS];
    usize levelRowBytes[FLUXION_TEST_BC_MIPS];
    u32 levelBlockRows[FLUXION_TEST_BC_MIPS];
    usize stagingSize = 0;

    for (u32 level = 0; level < FLUXION_TEST_BC_MIPS; ++level)
    {
        u32 levelWidth = FLUXION_TEST_BC_WIDTH >> level;
        u32 levelHeight = FLUXION_TEST_BC_HEIGHT >> level;
        if (levelWidth == 0) levelWidth = 1;
        if (levelHeight == 0) levelHeight = 1;

        const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(format, levelWidth);
        const usize paddedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
        const u32 blockRows = Fluxion_RHI_GetFormatBlockRows(format, levelHeight);

        stagingSize = (stagingSize + FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT;

        levelStagingOffset[level] = stagingSize;
        levelRowBytes[level] = paddedRowBytes;
        levelBlockRows[level] = blockRows;

        stagingSize += paddedRowBytes * blockRows;
    }

    FluxionRHIBufferDesc stagingDesc;
    stagingDesc.size = stagingSize;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = "RHITests.CompressedUpload.Staging";
    FluxionRHIBufferHandle staging = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(staging));
    if (!FLUXION_HANDLE_IS_VALID(staging))
    {
        Fluxion_RHI_DestroyTexture(texture);
        return;
    }

    {
        u8* mapped = (u8*)Fluxion_RHI_MapBuffer(staging);
        TEST_CHECK(ctx, mapped != NULL);
        if (mapped != NULL)
        {
            // Every byte different from its neighbours, so a level read
            // from the wrong offset would not happen to look right. The
            // bytes are not a valid encoding of anything -- no decoder
            // runs here, and a block format has no invalid bit patterns
            // for one to reject.
            for (usize i = 0; i < stagingSize; ++i) mapped[i] = (u8)(i * 31u + 7u);
            Fluxion_RHI_UnmapBuffer(staging);
        }
    }

    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(commandList));
    Fluxion_RHI_CommandList_Begin(commandList);

    FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBarrier toCopy;
    toCopy.texture = texture;
    toCopy.buffer = noBuffer;
    // The texture was made a moment ago and holds nothing, which is what
    // UNDEFINED means -- naming any other state here claims contents that
    // were never written.
    toCopy.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    toCopy.after = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    Fluxion_RHI_CommandList_Barrier(commandList, &toCopy, 1);

    for (u32 level = 0; level < FLUXION_TEST_BC_MIPS; ++level)
    {
        Fluxion_RHI_CommandList_CopyBufferToTexture(commandList, staging, levelStagingOffset[level], texture, level, 0);
    }

    FluxionRHIBarrier toRead;
    toRead.texture = texture;
    toRead.buffer = noBuffer;
    toRead.before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    toRead.after = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);

    Fluxion_RHI_CommandList_End(commandList);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &commandList, 1, fence);
    TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));

    // A view over a compressed texture is what a shader would actually
    // sample through, and creating one is where a backend that mapped the
    // format to nothing would finally say so.
    FluxionRHITextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.format = format;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = FLUXION_TEST_BC_MIPS;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    FluxionRHITextureViewHandle view = Fluxion_RHI_CreateTextureView(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(view));

    // The smallest levels are the ones the arithmetic gets wrong: a 2x2
    // and a 1x1 level of a 4x4-block format each still hold one whole
    // block, and a plan that shrank them below that would have staged
    // fewer bytes than the copy above just read.
    TEST_CHECK(ctx, levelBlockRows[FLUXION_TEST_BC_MIPS - 1] == 1);
    TEST_CHECK(ctx, levelRowBytes[FLUXION_TEST_BC_MIPS - 1] == FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT);

    Fluxion_RHI_DestroyTextureView(view);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyBuffer(staging);
    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_Device_CollectGarbage(device);

    FLUXION_LOG_INFO("RHITests", "%s: %s uploaded through the block-aware path.", backendName, formatName);
}

static void Fluxion_Test_CompressedUploadOnBackend(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
{
    if (!Fluxion_RHI_IsBackendAvailable(backend))
    {
        FLUXION_LOG_WARN("RHITests", "%s is not in this build -- skipping its compressed-upload check.", backendName);
        return;
    }

    FluxionRHIInstanceDesc instanceDesc = { "RHITests", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backend, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_WARN("RHITests", "%s instance creation failed -- skipping its compressed-upload check.", backendName);
        return;
    }

    FluxionRHIAdapterHandle adapters[16];
    const u32 adapterCount = Fluxion_RHI_EnumerateAdapters(instance, adapters, 16);
    if (adapterCount == 0)
    {
        FLUXION_LOG_WARN("RHITests", "No %s adapters -- skipping its compressed-upload check.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    if (!FLUXION_HANDLE_IS_VALID(device))
    {
        FLUXION_LOG_WARN("RHITests", "%s device creation failed -- skipping its compressed-upload check.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(queue));

    // The question a cook target asks before it picks a format at all.
    // Every device this engine will run on has the ordinary colour
    // format, and none of them has a format that does not exist.
    TEST_CHECK(ctx, Fluxion_RHI_Device_IsFormatSupported(device, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM));
    TEST_CHECK(ctx, !Fluxion_RHI_Device_IsFormatSupported(device, FLUXION_RHI_FORMAT_UNKNOWN));
    TEST_CHECK(ctx, !Fluxion_RHI_Device_IsFormatSupported(device, (FluxionRHIFormat)9999));

    // And a dead handle answers no rather than reaching through it.
    FluxionRHIDeviceHandle noDevice = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    TEST_CHECK(ctx, !Fluxion_RHI_Device_IsFormatSupported(noDevice, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM));

    const u64 errorsBefore = Fluxion_RHI_Validation_GetErrorCount();

    // Two formats rather than one, and deliberately the two whose blocks
    // are DIFFERENT sizes: eight bytes for BC4 and sixteen for BC7. A row
    // stride worked out from the wrong one of those is wrong by a factor
    // of two, which is the kind of mistake a single format cannot catch.
    Fluxion_Test_UploadOneCompressedTexture(ctx, device, queue, FLUXION_RHI_FORMAT_BC7_UNORM, "BC7_UNORM", backendName);
    Fluxion_Test_UploadOneCompressedTexture(ctx, device, queue, FLUXION_RHI_FORMAT_BC4_UNORM, "BC4_UNORM", backendName);

    // And the one a normal map will be stored in, which is the format
    // most likely to be reached in anger first.
    Fluxion_Test_UploadOneCompressedTexture(ctx, device, queue, FLUXION_RHI_FORMAT_BC5_UNORM, "BC5_UNORM", backendName);

    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == errorsBefore);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

void Test_CompressedUpload_Run(TestContext* ctx)
{
    Fluxion_Test_CompressedUploadOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    Fluxion_Test_CompressedUploadOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");

    // OpenGL is left out on purpose: this backend reaches a real GL
    // context through a window, and there is none here -- Test_OpenGLBackend
    // skips for the same reason on the same machines. The GL compressed
    // path is exercised by ForwardRendererDemo instead, which has one.
}
