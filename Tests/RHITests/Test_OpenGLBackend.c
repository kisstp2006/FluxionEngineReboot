#include "TestFramework.h"

#include <string.h>

#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RHI/RHI.h>

// Exercises the real OpenGL backend end to end, mirroring
// Test_VulkanBackend.c as closely as this backend's differences allow.
// Unlike Vulkan, this backend's Fluxion_RHI_CreateDevice bootstraps its
// own hidden native window internally (see the design note at the top of
// Source/RHI/Private/OpenGL/OpenGLCommon.h) -- so device/resource creation
// below doesn't itself need a window. A real, visible window is only
// needed to exercise the swapchain, which this test also does, retargeting
// the same context onto that window. RHITests has no existing pattern for
// a test needing the window system, so Init/Shutdown are bracketed locally
// here to avoid leaking window-system state into any other test in the
// same process.
//
// A machine with no usable OpenGL 4.5 core-profile driver (no display, no
// GPU driver, a software/CI environment) soft-skips the device-level
// checks instead of failing the suite, same graceful-skip philosophy
// already established by Test_VulkanBackend.c.
void Test_OpenGLBackend_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RHITests", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_OPENGL, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_WARN("RHITests", "OpenGL instance creation failed -- skipping Test_OpenGLBackend.");
        return;
    }

    u32 adapterCount = Fluxion_RHI_EnumerateAdapters(instance, NULL, 0);
    if (adapterCount == 0)
    {
        FLUXION_LOG_WARN("RHITests", "No OpenGL adapters enumerated -- skipping the rest of Test_OpenGLBackend.");
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
        FLUXION_LOG_WARN("RHITests", "OpenGL device (GL 4.5 core context) creation failed -- no usable driver on this machine; skipping the rest of Test_OpenGLBackend.");
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIAdapterInfo adapterInfo;
    TEST_CHECK(ctx, Fluxion_RHI_GetAdapterInfo(adapters[0], &adapterInfo));
    TEST_CHECK(ctx, adapterInfo.name[0] != '\0');
    FLUXION_LOG_INFO("RHITests", "OpenGL adapter: %s", adapterInfo.name);

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

    // --- Command recording: staging -> GPU copy + a barrier ---------------

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

    Fluxion_RHI_CommandList_End(commandList);

    // --- Synchronization + submission --------------------------------------

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(fence));

    Fluxion_RHI_Queue_Submit(graphicsQueue, &commandList, 1, fence);
    Fluxion_RHI_WaitForFence(fence);
    Fluxion_RHI_ResetFence(fence);

    FluxionRHISemaphoreHandle semaphore = Fluxion_RHI_CreateSemaphore(device);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(semaphore));

    // --- Swapchain (this backend's default-framebuffer sentinel path) ------
    // Bracketed locally: RHITests has no existing global window-system
    // fixture, so this is initialized and torn down entirely within this
    // function.

    FluxionEventQueue eventQueue;
    Fluxion_EventQueue_Init(&eventQueue, NULL, 64);
    Fluxion_WindowSystem_Init(NULL, &eventQueue, 4);

    FluxionWindowDesc windowDesc = { "RHITests OpenGL", 64, 64, false };
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
        FLUXION_LOG_WARN("RHITests", "Could not create a window for the OpenGL swapchain check -- skipping that part of Test_OpenGLBackend.");
    }

    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&eventQueue);

    // --- Cleanup ------------------------------------------------------------

    Fluxion_RHI_DestroySemaphore(semaphore);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroySampler(sampler);
    Fluxion_RHI_DestroyTextureView(textureView);
    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_DestroyBuffer(gpuBuffer);
    Fluxion_RHI_DestroyBuffer(stagingBuffer);

    Fluxion_RHI_Device_CollectGarbage(device);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
