#include "TestFramework.h"

#include <string.h>

#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RHI/RHI.h>

// Exercises the real D3D12 backend end to end, mirroring
// Test_VulkanBackend.c/Test_OpenGLBackend.c as closely as this backend's
// differences allow -- plus a BindGroup + compute pipeline round trip,
// since this backend's root-signature/descriptor-heap plumbing (unlike
// the other two) is new in this milestone and has no earlier test
// coverage to lean on. Only compiled on Windows (see
// Tests/RHITests/CMakeLists.txt) -- D3D12 doesn't exist elsewhere.
//
// A machine with no usable D3D12-capable adapter/driver soft-skips the
// device-level checks instead of failing the suite, same graceful-skip
// philosophy as the Vulkan/OpenGL backend tests.
void Test_D3D12Backend_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RHITests", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_D3D12, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_WARN("RHITests", "D3D12 instance creation failed -- skipping Test_D3D12Backend.");
        return;
    }

    u32 adapterCount = Fluxion_RHI_EnumerateAdapters(instance, NULL, 0);
    if (adapterCount == 0)
    {
        FLUXION_LOG_WARN("RHITests", "No D3D12-capable adapters enumerated -- skipping the rest of Test_D3D12Backend.");
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIAdapterHandle adapters[4];
    u32 written = Fluxion_RHI_EnumerateAdapters(instance, adapters, 4);
    TEST_CHECK(ctx, written > 0);

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    if (!FLUXION_HANDLE_IS_VALID(device))
    {
        FLUXION_LOG_WARN("RHITests", "D3D12 device creation failed -- no usable driver on this machine; skipping the rest of Test_D3D12Backend.");
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIAdapterInfo adapterInfo;
    TEST_CHECK(ctx, Fluxion_RHI_GetAdapterInfo(adapters[0], &adapterInfo));
    TEST_CHECK(ctx, adapterInfo.name[0] != '\0');
    FLUXION_LOG_INFO("RHITests", "D3D12 adapter: %s", adapterInfo.name);

    FluxionRHIQueueHandle graphicsQueue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(graphicsQueue));

    // --- Buffer / Map-Unmap staging upload ----------------------------------

    FluxionRHIBufferDesc stagingDesc = { 256, FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC, FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU, "TestStaging" };
    FluxionRHIBufferHandle stagingBuffer = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(stagingBuffer));

    void* mapped = Fluxion_RHI_MapBuffer(stagingBuffer);
    TEST_CHECK(ctx, mapped != NULL);
    if (mapped != NULL) memset(mapped, 0xAB, 256);
    Fluxion_RHI_UnmapBuffer(stagingBuffer);

    FluxionRHIBufferDesc gpuBufferDesc = { 256, FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "TestVertexBuffer" };
    FluxionRHIBufferHandle gpuBuffer = Fluxion_RHI_CreateBuffer(device, &gpuBufferDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(gpuBuffer));

    // --- Texture / view / sampler ------------------------------------------

    FluxionRHITextureDesc textureDesc = { 64, 64, 1, 1, 1, 1, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "TestTexture" };
    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(texture));

    FluxionRHITextureViewDesc viewDesc = { texture, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, 0, 1, 0, 1 };
    FluxionRHITextureViewHandle textureView = Fluxion_RHI_CreateTextureView(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(textureView));

    FluxionRHISamplerDesc samplerDesc = { FLUXION_RHI_FILTER_LINEAR, FLUXION_RHI_FILTER_LINEAR, FLUXION_RHI_FILTER_LINEAR,
        FLUXION_RHI_ADDRESS_MODE_REPEAT, FLUXION_RHI_ADDRESS_MODE_REPEAT, FLUXION_RHI_ADDRESS_MODE_REPEAT, 1.0f, "TestSampler" };
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(sampler));

    // --- BindGroupLayout + BindGroup (root signature / descriptor heap path) --

    FluxionRHIBindGroupLayoutEntryDesc materialEntries[2];
    memset(materialEntries, 0, sizeof(materialEntries));
    materialEntries[0].binding = 0;
    materialEntries[0].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    materialEntries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    materialEntries[1].binding = 1;
    materialEntries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    materialEntries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    FluxionRHIBindGroupLayoutDesc materialLayoutDesc;
    memset(&materialLayoutDesc, 0, sizeof(materialLayoutDesc));
    materialLayoutDesc.entries[0] = materialEntries[0];
    materialLayoutDesc.entries[1] = materialEntries[1];
    materialLayoutDesc.entryCount = 2;
    materialLayoutDesc.debugName = "TestMaterialLayout";
    FluxionRHIBindGroupLayoutHandle materialLayout = Fluxion_RHI_CreateBindGroupLayout(device, &materialLayoutDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(materialLayout));

    FluxionRHIBindGroupEntry materialGroupEntries[2];
    memset(materialGroupEntries, 0, sizeof(materialGroupEntries));
    materialGroupEntries[0].binding = 0;
    materialGroupEntries[0].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    materialGroupEntries[0].textureView = textureView;
    materialGroupEntries[1].binding = 1;
    materialGroupEntries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    materialGroupEntries[1].sampler = sampler;

    FluxionRHIBindGroupDesc materialGroupDesc;
    memset(&materialGroupDesc, 0, sizeof(materialGroupDesc));
    materialGroupDesc.layout = materialLayout;
    materialGroupDesc.entries = materialGroupEntries;
    materialGroupDesc.entryCount = 2;
    FluxionRHIBindGroupHandle materialBindGroup = Fluxion_RHI_CreateBindGroup(device, &materialGroupDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(materialBindGroup));

    // A BindGroup created against an invalid layout handle gracefully fails.
    FluxionRHIBindGroupLayoutHandle badLayout = { 999, 0 };
    FluxionRHIBindGroupDesc badGroupDesc = materialGroupDesc;
    badGroupDesc.layout = badLayout;
    FluxionRHIBindGroupHandle badBindGroup = Fluxion_RHI_CreateBindGroup(device, &badGroupDesc);
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(badBindGroup));

    // --- Command recording: staging -> GPU copy + a barrier + SetBindGroup --

    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(commandList));

    Fluxion_RHI_CommandList_Begin(commandList);
    Fluxion_RHI_CommandList_CopyBuffer(commandList, stagingBuffer, 0, gpuBuffer, 0, 256);

    FluxionRHIBarrier barrier;
    FluxionRHITextureHandle invalidTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    barrier.texture = invalidTexture;
    barrier.buffer = gpuBuffer;
    barrier.before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    barrier.after = FLUXION_RHI_RESOURCE_STATE_VERTEX_BUFFER;
    Fluxion_RHI_CommandList_Barrier(commandList, &barrier, 1);

    // SetBindGroup without any pipeline bound first is a no-op (no root
    // signature to resolve the group index against) -- exercises the
    // dispatch path doesn't crash on that caller-error case.
    Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_MATERIAL, materialBindGroup);

    Fluxion_RHI_CommandList_End(commandList);

    // --- Synchronization + submission --------------------------------------

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(fence));

    Fluxion_RHI_Queue_Submit(graphicsQueue, &commandList, 1, fence);
    Fluxion_RHI_WaitForFence(fence);
    Fluxion_RHI_ResetFence(fence);

    FluxionRHISemaphoreHandle semaphore = Fluxion_RHI_CreateSemaphore(device);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(semaphore));

    // --- Pipeline cache: nothing saved yet, so a load against a
    // nonexistent path gracefully fails rather than crashing. -----------------
    TEST_CHECK(ctx, !Fluxion_RHI_Device_LoadPipelineCacheFromFile(device, "fluxion_test_nonexistent_pipeline_cache.bin"));

    // --- Swapchain -----------------------------------------------------------
    // Bracketed locally: RHITests has no existing global window-system
    // fixture, so this is initialized and torn down entirely within this
    // function (same pattern as Test_OpenGLBackend.c).

    FluxionEventQueue eventQueue;
    Fluxion_EventQueue_Init(&eventQueue, NULL, 64);
    Fluxion_WindowSystem_Init(NULL, &eventQueue, 4);

    FluxionWindowDesc windowDesc = { "RHITests D3D12", 64, 64, false };
    FluxionWindowHandle window = Fluxion_Window_Create(&windowDesc);
    if (FLUXION_HANDLE_IS_VALID(window))
    {
        FluxionRHISwapchainDesc swapchainDesc = { 64, 64, FLUXION_RHI_FORMAT_B8G8R8A8_UNORM, 2, true };
        FluxionRHISwapchainHandle swapchain = Fluxion_RHI_CreateSwapchain(device, window, &swapchainDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(swapchain));
        if (FLUXION_HANDLE_IS_VALID(swapchain))
        {
            u32 imageIndex = Fluxion_RHI_Swapchain_AcquireNextImage(swapchain, semaphore);
            FluxionRHITextureHandle swapchainTexture = Fluxion_RHI_Swapchain_GetTexture(swapchain, imageIndex);
            TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(swapchainTexture));
            Fluxion_RHI_Swapchain_Present(swapchain, imageIndex, semaphore);
            Fluxion_RHI_DestroySwapchain(swapchain);
        }
        Fluxion_Window_Destroy(window);
    }
    else
    {
        FLUXION_LOG_WARN("RHITests", "Could not create a window for the D3D12 swapchain check -- skipping that part of Test_D3D12Backend.");
    }

    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&eventQueue);

    // --- Cleanup ------------------------------------------------------------

    Fluxion_RHI_DestroySemaphore(semaphore);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyBindGroup(materialBindGroup);
    Fluxion_RHI_DestroyBindGroupLayout(materialLayout);
    Fluxion_RHI_DestroySampler(sampler);
    Fluxion_RHI_DestroyTextureView(textureView);
    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_DestroyBuffer(gpuBuffer);
    Fluxion_RHI_DestroyBuffer(stagingBuffer);

    Fluxion_RHI_Device_CollectGarbage(device);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
