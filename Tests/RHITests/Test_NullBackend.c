#include "TestFramework.h"

#include <string.h>

#include <Fluxion/RHI/RHI.h>

// Covers the full valid-usage life cycle end to end, plus the "gracefully
// returns invalid/false" paths (bad device handle, bad adapter handle,
// ...). Deliberately does NOT exercise the misuse paths that trigger
// FLUXION_ASSERT_MSG (double free, Draw outside Begin/End, ...) --
// FLUXION_ASSERT_MSG calls FLUXION_DEBUG_BREAK(), which traps the process
// in a Debug build with no debugger attached, the same reason no other
// test in this codebase deliberately triggers an existing
// FLUXION_ASSERT_MSG precondition check (e.g. "Init called twice")
// either.
void Test_NullBackend_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RHITests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(instance));

    FluxionRHIAdapterHandle adapters[4];
    u32 adapterCount = Fluxion_RHI_EnumerateAdapters(instance, NULL, 0);
    TEST_CHECK(ctx, adapterCount == 1);
    u32 written = Fluxion_RHI_EnumerateAdapters(instance, adapters, 4);
    TEST_CHECK(ctx, written == 1);

    FluxionRHIAdapterInfo adapterInfo;
    TEST_CHECK(ctx, Fluxion_RHI_GetAdapterInfo(adapters[0], &adapterInfo));
    TEST_CHECK(ctx, strcmp(adapterInfo.name, "Null Adapter") == 0);
    TEST_CHECK(ctx, (adapterInfo.capabilities & FLUXION_RHI_CAPABILITY_BINDLESS_RESOURCES) != 0);

    // Bad adapter handle: gracefully fails, no crash.
    FluxionRHIAdapterHandle badAdapter = { 999, 0 };
    FluxionRHIAdapterInfo unusedInfo;
    TEST_CHECK(ctx, !Fluxion_RHI_GetAdapterInfo(badAdapter, &unusedInfo));

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_BINDLESS_RESOURCES | FLUXION_RHI_CAPABILITY_COMPUTE_SHADERS };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(device));

    // Bad adapter handle at device-creation time: gracefully fails.
    FluxionRHIDeviceHandle badDevice = Fluxion_RHI_CreateDevice(badAdapter, &deviceDesc);
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(badDevice));

    FluxionRHIQueueHandle graphicsQueue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(graphicsQueue));

    // Buffer / texture / view / sampler lifecycle.
    FluxionRHIBufferDesc bufferDesc = { 256, FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "TestVertexBuffer" };
    FluxionRHIBufferHandle buffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(buffer));

    FluxionRHITextureDesc textureDesc = { 64, 64, 1, 1, 1, 1, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, FLUXION_RHI_TEXTURE_USAGE_SAMPLED, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "TestTexture", FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(texture));

    FluxionRHITextureViewDesc viewDesc = { texture, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureViewHandle textureView = Fluxion_RHI_CreateTextureView(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(textureView));

    FluxionRHISamplerDesc samplerDesc = { FLUXION_RHI_FILTER_LINEAR, FLUXION_RHI_FILTER_LINEAR, FLUXION_RHI_FILTER_LINEAR,
        FLUXION_RHI_ADDRESS_MODE_REPEAT, FLUXION_RHI_ADDRESS_MODE_REPEAT, FLUXION_RHI_ADDRESS_MODE_REPEAT, 1.0f, "TestSampler" };
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(sampler));

    // Shader / pipeline lifecycle -- placeholder bytecode, the Null
    // backend never inspects it.
    const u8 dummyBytecode[4] = { 0, 0, 0, 0 };
    FluxionRHIShaderDesc vsDesc = { FLUXION_RHI_SHADER_STAGE_VERTEX, dummyBytecode, sizeof(dummyBytecode), "main", "TestVS" };
    FluxionRHIShaderHandle vertexShader = Fluxion_RHI_CreateShader(device, &vsDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(vertexShader));

    FluxionRHIShaderDesc fsDesc = { FLUXION_RHI_SHADER_STAGE_FRAGMENT, dummyBytecode, sizeof(dummyBytecode), "main", "TestFS" };
    FluxionRHIShaderHandle fragmentShader = Fluxion_RHI_CreateShader(device, &fsDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(fragmentShader));

    FluxionRHIGraphicsPipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.colorFormats[0] = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.debugName = "TestPipeline";
    FluxionRHIPipelineHandle pipeline = Fluxion_RHI_CreateGraphicsPipeline(device, &pipelineDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(pipeline));

    // Command list: full valid Begin/BeginRendering/Draw/EndRendering/End
    // sequence.
    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(commandList));

    Fluxion_RHI_CommandList_Begin(commandList);

    FluxionRHIRenderingAttachment colorAttachment;
    colorAttachment.view = textureView;
    colorAttachment.clear = true;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = &colorAttachment;
    renderingDesc.colorAttachmentCount = 1;
    renderingDesc.depthAttachment = NULL;
    renderingDesc.width = 64;
    renderingDesc.height = 64;

    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
    Fluxion_RHI_CommandList_SetPipeline(commandList, pipeline);
    Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, buffer, 0);
    Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
    Fluxion_RHI_CommandList_EndRendering(commandList);
    Fluxion_RHI_CommandList_End(commandList);

    // Submit + fence.
    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(fence));

    FluxionRHICommandListHandle submitList[1] = { commandList };
    Fluxion_RHI_Queue_Submit(graphicsQueue, submitList, 1, fence);
    Fluxion_RHI_WaitForFence(fence); // Null backend: completes instantly either way

    FluxionRHISemaphoreHandle semaphore = Fluxion_RHI_CreateSemaphore(device);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(semaphore));

    FluxionRHIQueryPoolHandle queryPool = Fluxion_RHI_CreateQueryPool(device, 8);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(queryPool));

    // Teardown, valid handles only (no double-free/misuse paths here).
    Fluxion_RHI_DestroyQueryPool(queryPool);
    Fluxion_RHI_DestroySemaphore(semaphore);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyPipeline(pipeline);
    Fluxion_RHI_DestroyShader(fragmentShader);
    Fluxion_RHI_DestroyShader(vertexShader);
    Fluxion_RHI_DestroySampler(sampler);
    Fluxion_RHI_DestroyTextureView(textureView);
    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_DestroyBuffer(buffer);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
