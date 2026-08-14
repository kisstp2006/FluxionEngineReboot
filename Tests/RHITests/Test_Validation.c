#include "TestFramework.h"

#include <Fluxion/RHI/RHI.h>

#include <string.h>

// Drives deliberately broken calls into the validation layer and asserts
// on the error count. Every scenario here would, without the layer,
// either trip a backend assert (stopping the process at the first
// mistake) or silently do something undefined -- the layer's whole value
// is that these become countable reports instead, and that the broken
// call never reaches the backend.
void Test_Validation_Run(TestContext* ctx)
{
    // enableValidation = true is what installs the layer.
    FluxionRHIInstanceDesc instanceDesc = { "RHITests.Validation", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(instance));

    FluxionRHIAdapterHandle adapter;
    Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1);
    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);

    Fluxion_RHI_Validation_ResetErrorCount();

    // --- A valid frame produces zero reports -----------------------------
    //
    // First, because every check below would be meaningless if ordinary
    // correct usage also tripped reports.
    FluxionRHIBufferDesc bufferDesc;
    memset(&bufferDesc, 0, sizeof(bufferDesc));
    bufferDesc.size = 256;
    bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER;
    bufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    bufferDesc.debugName = "Test_Validation.Buffer";
    FluxionRHIBufferHandle buffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(buffer));

    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(cmd);
    Fluxion_RHI_CommandList_End(cmd);
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 0);

    // --- Use after destroy ------------------------------------------------
    Fluxion_RHI_DestroyBuffer(buffer);
    Fluxion_RHI_MapBuffer(buffer); // dead handle -- reported, not forwarded
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 1);

    // --- Double destroy ---------------------------------------------------
    Fluxion_RHI_DestroyBuffer(buffer);
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 2);

    // --- Draw without BeginRendering -------------------------------------
    //
    // A real graphics pipeline is bound first, so the ONLY thing wrong
    // with the draw is the missing render pass -- without this, the
    // missing-pipeline report would fire instead and this check could
    // not tell whether the render-pass check exists at all.
    FluxionRHIShaderDesc shaderDesc;
    memset(&shaderDesc, 0, sizeof(shaderDesc));
    shaderDesc.stage = FLUXION_RHI_SHADER_STAGE_VERTEX;
    shaderDesc.bytecode = "";
    shaderDesc.bytecodeSize = 1;
    FluxionRHIShaderHandle vs = Fluxion_RHI_CreateShader(device, &shaderDesc);
    shaderDesc.stage = FLUXION_RHI_SHADER_STAGE_FRAGMENT;
    FluxionRHIShaderHandle fs = Fluxion_RHI_CreateShader(device, &shaderDesc);

    FluxionRHIGraphicsPipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.vertexShader = vs;
    pipelineDesc.fragmentShader = fs;
    pipelineDesc.debugName = "Test_Validation.Pipeline";
    FluxionRHIPipelineHandle pipeline = Fluxion_RHI_CreateGraphicsPipeline(device, &pipelineDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(pipeline));

    Fluxion_RHI_Validation_ResetErrorCount();
    Fluxion_RHI_CommandList_Begin(cmd);
    Fluxion_RHI_CommandList_SetPipeline(cmd, pipeline);
    Fluxion_RHI_CommandList_Draw(cmd, 3, 1, 0, 0);
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 1);

    // --- Submit while recording -------------------------------------------
    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHIFenceHandle noFence = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    Fluxion_RHI_Queue_Submit(queue, &cmd, 1, noFence);
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 2);

    // --- Begin twice ------------------------------------------------------
    Fluxion_RHI_CommandList_Begin(cmd);
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 3);
    Fluxion_RHI_CommandList_End(cmd);

    // --- Resource state mismatch ------------------------------------------
    //
    // A texture is created UNDEFINED; declaring its before-state as
    // RENDER_TARGET without ever transitioning it there is the lie the
    // state tracker exists to catch. Reported AND forwarded -- behavior
    // must not differ with validation on.
    Fluxion_RHI_Validation_ResetErrorCount();
    FluxionRHITextureDesc textureDesc;
    memset(&textureDesc, 0, sizeof(textureDesc));
    textureDesc.width = 4;
    textureDesc.height = 4;
    textureDesc.depth = 1;
    textureDesc.mipLevels = 1;
    textureDesc.arrayLayers = 1;
    textureDesc.sampleCount = 1;
    textureDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    textureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    textureDesc.debugName = "Test_Validation.Texture";
    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);

    Fluxion_RHI_CommandList_Begin(cmd);
    FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBarrier lying = { texture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
    Fluxion_RHI_CommandList_Barrier(cmd, &lying, 1);
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 1);

    // The tracker updated to the barrier's after-state, so a follow-up
    // that declares SHADER_READ is clean -- a tracker that reported once
    // and then lost the thread would fail here.
    FluxionRHIBarrier honest = { texture, noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE };
    Fluxion_RHI_CommandList_Barrier(cmd, &honest, 1);
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 1);
    Fluxion_RHI_CommandList_End(cmd);

    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_DestroyPipeline(pipeline);
    Fluxion_RHI_DestroyShader(vs);
    Fluxion_RHI_DestroyShader(fs);
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);

    // --- Off means absent -------------------------------------------------
    //
    // The same dead-handle mistakes with validation off must not grow
    // the count: the wrapper is not installed, so there is nothing to
    // count with. (The calls themselves are NOT repeated here -- without
    // the layer they would hit backend asserts, which is precisely the
    // difference being demonstrated.)
    FluxionRHIInstanceDesc plainDesc = { "RHITests.Validation.Off", false };
    FluxionRHIInstanceHandle plain = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &plainDesc);
    Fluxion_RHI_Validation_ResetErrorCount();
    Fluxion_RHI_EnumerateAdapters(plain, &adapter, 1);
    FluxionRHIDeviceHandle plainDevice = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    FluxionRHICommandListHandle plainCmd = Fluxion_RHI_CreateCommandList(plainDevice, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(plainCmd);
    Fluxion_RHI_CommandList_End(plainCmd);
    Fluxion_RHI_DestroyCommandList(plainCmd);
    Fluxion_RHI_DestroyDevice(plainDevice);
    Fluxion_RHI_DestroyInstance(plain);
    TEST_CHECK(ctx, Fluxion_RHI_Validation_GetErrorCount() == 0);
}
