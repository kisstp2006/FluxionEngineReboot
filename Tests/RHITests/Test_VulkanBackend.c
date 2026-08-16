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

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RHI/RHI.h>

// Exercises the real Vulkan backend end to end: instance/adapter/device/
// queue creation, buffer/texture/view/sampler lifecycle, a CPU->GPU
// staging upload via Map/Unmap + CommandList CopyBuffer, fence/semaphore
// lifecycle, submission, and CollectGarbage. Build+link must succeed
// everywhere, but a machine with no enumerable Vulkan adapter (no ICD)
// soft-skips the device-level checks instead of failing the suite --
// CI environments aren't guaranteed to have one, even though this
// repo's own dev machines do (a real GPU driver on Windows, a software
// Mesa/lavapipe ICD on WSL2).
void Test_VulkanBackend_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RHITests", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_VULKAN, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_WARN("RHITests", "Vulkan instance creation failed -- no usable Vulkan loader/ICD on this machine; skipping Test_VulkanBackend.");
        return;
    }

    u32 adapterCount = Fluxion_RHI_EnumerateAdapters(instance, NULL, 0);
    if (adapterCount == 0)
    {
        FLUXION_LOG_WARN("RHITests", "No Vulkan adapters enumerated -- skipping the rest of Test_VulkanBackend.");
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIAdapterHandle adapters[16];
    u32 written = Fluxion_RHI_EnumerateAdapters(instance, adapters, 16);
    TEST_CHECK(ctx, written > 0);

    FluxionRHIAdapterInfo adapterInfo;
    TEST_CHECK(ctx, Fluxion_RHI_GetAdapterInfo(adapters[0], &adapterInfo));
    TEST_CHECK(ctx, adapterInfo.name[0] != '\0');
    // Every Vulkan 1.2+ device this backend accepted must at least report
    // timeline-sync support (a hard minimum this backend requires at
    // device-creation time).
    FLUXION_LOG_INFO("RHITests", "Vulkan adapter: %s", adapterInfo.name);

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(device));
    if (!FLUXION_HANDLE_IS_VALID(device))
    {
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle graphicsQueue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(graphicsQueue));

    // --- Buffer / Map-Unmap staging upload ----------------------------------

    FluxionRHIBufferDesc stagingDesc = { 256, FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC, FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU, "TestStaging" };
    FluxionRHIBufferHandle stagingBuffer = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(stagingBuffer));

    void* mapped = Fluxion_RHI_MapBuffer(stagingBuffer);
    TEST_CHECK(ctx, mapped != NULL);
    if (mapped != NULL)
    {
        memset(mapped, 0xAB, 256);
    }
    Fluxion_RHI_UnmapBuffer(stagingBuffer);

    FluxionRHIBufferDesc gpuBufferDesc = { 256, FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "TestVertexBuffer" };
    FluxionRHIBufferHandle gpuBuffer = Fluxion_RHI_CreateBuffer(device, &gpuBufferDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(gpuBuffer));

    // --- Texture / view / sampler ------------------------------------------

    FluxionRHITextureDesc textureDesc = { 64, 64, 1, 1, 1, 1, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "TestTexture", FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(texture));

    FluxionRHITextureViewDesc viewDesc = { texture, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureViewHandle textureView = Fluxion_RHI_CreateTextureView(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(textureView));

    FluxionRHISamplerDesc samplerDesc = { FLUXION_RHI_FILTER_LINEAR, FLUXION_RHI_FILTER_LINEAR, FLUXION_RHI_FILTER_LINEAR,
        FLUXION_RHI_ADDRESS_MODE_REPEAT, FLUXION_RHI_ADDRESS_MODE_REPEAT, FLUXION_RHI_ADDRESS_MODE_REPEAT, 1.0f, "TestSampler" };
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(sampler));

    // --- Command recording: staging -> GPU copy + a barrier ---------------

    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(commandList));

    Fluxion_RHI_CommandList_Begin(commandList);
    Fluxion_RHI_CommandList_CopyBuffer(commandList, stagingBuffer, 0, gpuBuffer, 0, 256);

    FluxionRHIBarrier barrier;
    // A zero-filled handle is NOT the same as an invalid one here --
    // FLUXION_HANDLE_INVALID_INDEX is 0xFFFFFFFF, not 0, so `texture`
    // must be set explicitly invalid rather than left memset(0) (which
    // would alias real texture slot 0 and misroute this as an image
    // barrier instead of a buffer barrier).
    FluxionRHITextureHandle invalidTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    barrier.texture = invalidTexture;
    barrier.buffer = gpuBuffer;
    barrier.before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    barrier.after = FLUXION_RHI_RESOURCE_STATE_VERTEX_BUFFER;
    Fluxion_RHI_CommandList_Barrier(commandList, &barrier, 1);

    Fluxion_RHI_CommandList_End(commandList);

    // --- Synchronization + submission --------------------------------------

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(fence));

    Fluxion_RHI_Queue_Submit(graphicsQueue, &commandList, 1, fence);
    Fluxion_RHI_WaitForFence(fence);
    Fluxion_RHI_ResetFence(fence);

    FluxionRHISemaphoreHandle semaphore = Fluxion_RHI_CreateSemaphore(device);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(semaphore));

    // --- Cleanup ------------------------------------------------------------

    Fluxion_RHI_DestroySemaphore(semaphore);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroySampler(sampler);
    Fluxion_RHI_DestroyTextureView(textureView);
    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_DestroyBuffer(gpuBuffer);
    Fluxion_RHI_DestroyBuffer(stagingBuffer);

    // None of the Destroy* calls above freed anything for real yet --
    // this backend defers actual destruction until CollectGarbage can
    // confirm the GPU is done (guaranteed here since WaitForFence already
    // blocked until the one and only submission completed).
    Fluxion_RHI_Device_CollectGarbage(device);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
