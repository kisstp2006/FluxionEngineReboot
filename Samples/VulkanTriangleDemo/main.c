// Manual test tool, not an automated test (same role as Samples/InputDemo):
// opens a window, brings up the Vulkan RHI backend, and every frame
// clears the screen and draws one indexed, colored triangle -- a visible,
// on-screen demonstration that RHITests' offscreen checks can't show on
// their own. Vertex/index data goes CPU->staging buffer->GPU_ONLY buffer
// via Map/Unmap + CommandList CopyBuffer + a Barrier, the same staging
// pattern real game code would use.
#include <Fluxion/Application/Events/EventQueue.h>
#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Platform/Time.h>
#include <Fluxion/RHI/RHI.h>

#include "TriangleShaders.h"

#include <stddef.h>
#include <string.h>

typedef struct FluxionDemoVertex
{
    f32 position[2];
    f32 color[3];
} FluxionDemoVertex;

#define FLUXION_DEMO_FRAMES_IN_FLIGHT 2

int main(void)
{
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 256);
    Fluxion_WindowSystem_Init(NULL, &queue, 1);

    FluxionWindowDesc windowDesc;
    windowDesc.title = "Fluxion VulkanTriangleDemo (close the window to quit)";
    windowDesc.width = 800;
    windowDesc.height = 600;
    windowDesc.resizable = true;
    FluxionWindowHandle window = Fluxion_Window_Create(&windowDesc);

    FluxionRHIInstanceDesc instanceDesc = { "VulkanTriangleDemo", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_VULKAN, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_ERROR("VulkanTriangleDemo", "Failed to create a Vulkan instance -- no usable Vulkan loader/ICD on this machine.");
        return 1;
    }

    FluxionRHIAdapterHandle adapter;
    if (Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1) == 0)
    {
        FLUXION_LOG_ERROR("VulkanTriangleDemo", "No Vulkan adapter found.");
        return 1;
    }
    FluxionRHIAdapterInfo adapterInfo;
    Fluxion_RHI_GetAdapterInfo(adapter, &adapterInfo);
    FLUXION_LOG_INFO("VulkanTriangleDemo", "Using adapter: %s", adapterInfo.name);

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    FluxionRHIQueueHandle graphicsQueue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    FluxionRHISwapchainDesc swapchainDesc;
    swapchainDesc.width = windowDesc.width;
    swapchainDesc.height = windowDesc.height;
    swapchainDesc.format = FLUXION_RHI_FORMAT_B8G8R8A8_UNORM;
    swapchainDesc.bufferCount = FLUXION_DEMO_FRAMES_IN_FLIGHT;
    swapchainDesc.vsync = true;
    FluxionRHISwapchainHandle swapchain = Fluxion_RHI_CreateSwapchain(device, window, &swapchainDesc);

    // --- Triangle geometry: staging (CPU_TO_GPU) -> GPU_ONLY, the real
    // upload pattern, not just a directly-mapped GPU buffer. --------------

    static const FluxionDemoVertex vertices[3] =
    {
        { { 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f } },
        { { -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
    };
    static const u16 indices[3] = { 0, 1, 2 };

    FluxionRHIBufferDesc stagingDesc = { sizeof(vertices) + sizeof(indices), FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC, FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU, "DemoStaging" };
    FluxionRHIBufferHandle stagingBuffer = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
    u8* mapped = (u8*)Fluxion_RHI_MapBuffer(stagingBuffer);
    memcpy(mapped, vertices, sizeof(vertices));
    memcpy(mapped + sizeof(vertices), indices, sizeof(indices));
    Fluxion_RHI_UnmapBuffer(stagingBuffer);

    FluxionRHIBufferDesc vertexBufferDesc = { sizeof(vertices), FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "DemoVertexBuffer" };
    FluxionRHIBufferHandle vertexBuffer = Fluxion_RHI_CreateBuffer(device, &vertexBufferDesc);
    FluxionRHIBufferDesc indexBufferDesc = { sizeof(indices), FLUXION_RHI_BUFFER_USAGE_INDEX_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "DemoIndexBuffer" };
    FluxionRHIBufferHandle indexBuffer = Fluxion_RHI_CreateBuffer(device, &indexBufferDesc);

    FluxionRHICommandListHandle uploadCommandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(uploadCommandList);
    Fluxion_RHI_CommandList_CopyBuffer(uploadCommandList, stagingBuffer, 0, vertexBuffer, 0, sizeof(vertices));
    Fluxion_RHI_CommandList_CopyBuffer(uploadCommandList, stagingBuffer, sizeof(vertices), indexBuffer, 0, sizeof(indices));
    FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBarrier uploadBarriers[2];
    uploadBarriers[0].texture = noTexture; uploadBarriers[0].buffer = vertexBuffer;
    uploadBarriers[0].before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION; uploadBarriers[0].after = FLUXION_RHI_RESOURCE_STATE_VERTEX_BUFFER;
    uploadBarriers[1].texture = noTexture; uploadBarriers[1].buffer = indexBuffer;
    uploadBarriers[1].before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION; uploadBarriers[1].after = FLUXION_RHI_RESOURCE_STATE_INDEX_BUFFER;
    Fluxion_RHI_CommandList_Barrier(uploadCommandList, uploadBarriers, 2);
    Fluxion_RHI_CommandList_End(uploadCommandList);

    FluxionRHIFenceHandle uploadFence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(graphicsQueue, &uploadCommandList, 1, uploadFence);
    Fluxion_RHI_WaitForFence(uploadFence);
    Fluxion_RHI_DestroyFence(uploadFence);
    Fluxion_RHI_DestroyCommandList(uploadCommandList);
    Fluxion_RHI_DestroyBuffer(stagingBuffer);
    Fluxion_RHI_Device_CollectGarbage(device);

    // --- Pipeline (minimal, no descriptor sets) -----------------------------

    FluxionRHIShaderDesc vsDesc = { FLUXION_RHI_SHADER_STAGE_VERTEX, g_TriangleVertexShaderSpirv, g_TriangleVertexShaderSpirv_size, "main", "TriangleVS" };
    FluxionRHIShaderHandle vertexShader = Fluxion_RHI_CreateShader(device, &vsDesc);
    FluxionRHIShaderDesc fsDesc = { FLUXION_RHI_SHADER_STAGE_FRAGMENT, g_TriangleFragmentShaderSpirv, g_TriangleFragmentShaderSpirv_size, "main", "TriangleFS" };
    FluxionRHIShaderHandle fragmentShader = Fluxion_RHI_CreateShader(device, &fsDesc);

    FluxionRHIGraphicsPipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout.attributes[0].location = 0;
    pipelineDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    pipelineDesc.vertexLayout.attributes[0].offset = offsetof(FluxionDemoVertex, position);
    pipelineDesc.vertexLayout.attributes[1].location = 1;
    pipelineDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    pipelineDesc.vertexLayout.attributes[1].offset = offsetof(FluxionDemoVertex, color);
    pipelineDesc.vertexLayout.attributeCount = 2;
    pipelineDesc.vertexLayout.stride = sizeof(FluxionDemoVertex);
    pipelineDesc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.colorFormats[0] = swapchainDesc.format;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = FLUXION_RHI_FORMAT_UNKNOWN;
    pipelineDesc.debugName = "TrianglePipeline";
    FluxionRHIPipelineHandle pipeline = Fluxion_RHI_CreateGraphicsPipeline(device, &pipelineDesc);

    // --- Per-frame-in-flight resources (caller-managed, no hidden
    // backend FrameContext) --------------------------------------------------

    FluxionRHICommandListHandle commandLists[FLUXION_DEMO_FRAMES_IN_FLIGHT];
    FluxionRHIFenceHandle frameFences[FLUXION_DEMO_FRAMES_IN_FLIGHT];
    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        commandLists[i] = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
        frameFences[i] = Fluxion_RHI_CreateFence(device, true);
    }
    // No FluxionRHISemaphoreHandle is created for Acquire/Present: this
    // backend's Acquire already CPU-blocks on an internal fence before
    // returning (VulkanSwapchain.cpp), and Present is preceded by an
    // explicit WaitForFence below -- an unused binary semaphore would
    // just accumulate signals nothing ever consumes (Vulkan requires a
    // semaphore be unsignaled before vkAcquireNextImageKHR signals it
    // again, which a "pass it and never wait on it" semaphore violates
    // on the second frame).
    FluxionRHISemaphoreHandle noSemaphore = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FLUXION_LOG_INFO("VulkanTriangleDemo", "Window created. Rendering an indexed triangle every frame.");

    bool running = true;
    u32 frameIndex = 0;
    while (running)
    {
        Fluxion_WindowSystem_PollEvents();
        FluxionEvent event;
        while (Fluxion_EventQueue_Pop(&queue, &event))
        {
            if (event.type == FLUXION_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }
        if (!running) break;

        u32 imageIndex = Fluxion_RHI_Swapchain_AcquireNextImage(swapchain, noSemaphore);

        // The swapchain's actual current image extent (queried from the
        // backend, decision discovered during implementation) -- NOT a
        // separately-queried window size, which can transiently disagree
        // with the swapchain's own tracked size (window resize race,
        // OS-level border/DPI accounting) and trips a hard Vulkan
        // validation error if the render area doesn't exactly match the
        // acquired image.
        u32 surfaceWidth = 0, surfaceHeight = 0;
        Fluxion_RHI_Swapchain_GetExtent(swapchain, &surfaceWidth, &surfaceHeight);
        FluxionRHITextureHandle backbuffer = Fluxion_RHI_Swapchain_GetTexture(swapchain, imageIndex);

        FluxionRHITextureViewDesc backbufferViewDesc = { backbuffer, swapchainDesc.format, 0, 1, 0, 1 };
        FluxionRHITextureViewHandle backbufferView = Fluxion_RHI_CreateTextureView(device, &backbufferViewDesc);

        FluxionRHICommandListHandle cmd = commandLists[frameIndex];
        Fluxion_RHI_CommandList_Begin(cmd);

        FluxionRHIBarrier toRenderTarget;
        toRenderTarget.texture = backbuffer; toRenderTarget.buffer.index = FLUXION_HANDLE_INVALID_INDEX; toRenderTarget.buffer.generation = 0;
        toRenderTarget.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
        toRenderTarget.after = FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET;
        Fluxion_RHI_CommandList_Barrier(cmd, &toRenderTarget, 1);

        FluxionRHIRenderingAttachment colorAttachment;
        colorAttachment.view = backbufferView;
        colorAttachment.clear = true;
        colorAttachment.clearColor[0] = 0.02f; colorAttachment.clearColor[1] = 0.02f; colorAttachment.clearColor[2] = 0.05f; colorAttachment.clearColor[3] = 1.0f;

        FluxionRHIRenderingDesc renderingDesc;
        renderingDesc.colorAttachments = &colorAttachment;
        renderingDesc.colorAttachmentCount = 1;
        renderingDesc.depthAttachment = NULL;
        renderingDesc.width = surfaceWidth;
        renderingDesc.height = surfaceHeight;
        Fluxion_RHI_CommandList_BeginRendering(cmd, &renderingDesc);

        Fluxion_RHI_CommandList_SetPipeline(cmd, pipeline);
        Fluxion_RHI_CommandList_SetVertexBuffer(cmd, 0, vertexBuffer, 0);
        Fluxion_RHI_CommandList_SetIndexBuffer(cmd, indexBuffer, 0, true);
        Fluxion_RHI_CommandList_DrawIndexed(cmd, 3, 1, 0, 0, 0);

        Fluxion_RHI_CommandList_EndRendering(cmd);

        FluxionRHIBarrier toPresent;
        toPresent.texture = backbuffer; toPresent.buffer.index = FLUXION_HANDLE_INVALID_INDEX; toPresent.buffer.generation = 0;
        toPresent.before = FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET;
        toPresent.after = FLUXION_RHI_RESOURCE_STATE_PRESENT;
        Fluxion_RHI_CommandList_Barrier(cmd, &toPresent, 1);

        Fluxion_RHI_CommandList_End(cmd);

        Fluxion_RHI_Queue_Submit(graphicsQueue, &cmd, 1, frameFences[frameIndex]);

        // Fluxion_RHI_Queue_Submit has no way to signal a binary
        // semaphore (only a signalFence), so there is nothing that could
        // ever signal a present-wait semaphore. Symmetric with the
        // Acquire-side design
        // (VulkanSwapchain.cpp CPU-blocks on an internal fence instead of
        // relying on the acquire semaphore): wait for this frame's own
        // submission to finish CPU-side before presenting, so Present
        // needs no GPU-side wait at all -- pass an invalid semaphore
        // handle, which the backend correctly turns into "0 wait
        // semaphores" rather than a present that waits on something that
        // can never be signaled.
        Fluxion_RHI_WaitForFence(frameFences[frameIndex]);
        Fluxion_RHI_ResetFence(frameFences[frameIndex]); // ready for this slot's next use, FLUXION_DEMO_FRAMES_IN_FLIGHT frames from now
        Fluxion_RHI_Swapchain_Present(swapchain, imageIndex, noSemaphore);

        // Safe to actually reclaim the retired backbuffer view right
        // here, since the WaitForFence above already confirmed the GPU
        // is done with this frame's work.
        Fluxion_RHI_Device_CollectGarbage(device);
        Fluxion_RHI_DestroyTextureView(backbufferView);

        frameIndex = (frameIndex + 1) % FLUXION_DEMO_FRAMES_IN_FLIGHT;
    }

    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        Fluxion_RHI_WaitForFence(frameFences[i]);
    }

    FLUXION_LOG_INFO("VulkanTriangleDemo", "Closing.");

    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        Fluxion_RHI_DestroyFence(frameFences[i]);
        Fluxion_RHI_DestroyCommandList(commandLists[i]);
    }
    Fluxion_RHI_DestroyPipeline(pipeline);
    Fluxion_RHI_DestroyShader(vertexShader);
    Fluxion_RHI_DestroyShader(fragmentShader);
    Fluxion_RHI_DestroyBuffer(vertexBuffer);
    Fluxion_RHI_DestroyBuffer(indexBuffer);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroySwapchain(swapchain);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);

    Fluxion_Window_Destroy(window);
    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
    return 0;
}
