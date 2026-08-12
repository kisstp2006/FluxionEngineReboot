#include "TestFramework.h"

#include <string.h>

#include <Fluxion/RHI/RHI.h>

// Exercises the BindGroupLayout/BindGroup/ComputePipeline/PipelineCache
// API against the Null backend -- deterministic, no GPU/driver
// dependency, same reasoning as Test_NullBackend.c. Vulkan-specific
// behavior (VkDescriptorSetLayout caching/dedup, real descriptor writes)
// is exercised separately wherever Test_VulkanBackend.c runs, not here.
void Test_BindGroup_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RHITests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(instance));

    FluxionRHIAdapterHandle adapter;
    Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1);

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(device));

    // A layout with one uniform buffer entry (binding 0, visible to
    // fragment) and one sampled-texture entry (binding 1) -- the shape a
    // Material BindGroup would typically have.
    FluxionRHIBindGroupLayoutEntryDesc entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].binding = 0;
    entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    entries[1].binding = 1;
    entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    FluxionRHIBindGroupLayoutDesc layoutDesc;
    memset(&layoutDesc, 0, sizeof(layoutDesc));
    layoutDesc.entries[0] = entries[0];
    layoutDesc.entries[1] = entries[1];
    layoutDesc.entryCount = 2;
    layoutDesc.debugName = "TestMaterialLayout";

    FluxionRHIBindGroupLayoutHandle layout = Fluxion_RHI_CreateBindGroupLayout(device, &layoutDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(layout));

    // Backing resources for the BindGroup's entries.
    FluxionRHIBufferDesc bufferDesc = { 256, FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "TestUniformBuffer" };
    FluxionRHIBufferHandle uniformBuffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(uniformBuffer));

    FluxionRHITextureDesc textureDesc = { 64, 64, 1, 1, 1, 1, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, FLUXION_RHI_TEXTURE_USAGE_SAMPLED, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "TestTexture" };
    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(texture));

    FluxionRHITextureViewDesc viewDesc = { texture, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, 0, 1, 0, 1 };
    FluxionRHITextureViewHandle textureView = Fluxion_RHI_CreateTextureView(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(textureView));

    FluxionRHIBindGroupEntry groupEntries[2];
    memset(groupEntries, 0, sizeof(groupEntries));
    groupEntries[0].binding = 0;
    groupEntries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    groupEntries[0].buffer = uniformBuffer;
    groupEntries[0].bufferSize = 256;
    groupEntries[1].binding = 1;
    groupEntries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    groupEntries[1].textureView = textureView;

    FluxionRHIBindGroupDesc bindGroupDesc;
    memset(&bindGroupDesc, 0, sizeof(bindGroupDesc));
    bindGroupDesc.layout = layout;
    bindGroupDesc.entries = groupEntries;
    bindGroupDesc.entryCount = 2;

    FluxionRHIBindGroupHandle bindGroup = Fluxion_RHI_CreateBindGroup(device, &bindGroupDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(bindGroup));

    // A BindGroup created against an invalid layout handle gracefully
    // fails rather than crashing.
    FluxionRHIBindGroupLayoutHandle badLayout = { 999, 0 };
    FluxionRHIBindGroupDesc badBindGroupDesc = bindGroupDesc;
    badBindGroupDesc.layout = badLayout;
    FluxionRHIBindGroupHandle badBindGroup = Fluxion_RHI_CreateBindGroup(device, &badBindGroupDesc);
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(badBindGroup));

    // SetBindGroup inside a real command-list recording -- exercises the
    // dispatch path even though the Null backend's implementation is a
    // recording-state check only.
    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(commandList));
    Fluxion_RHI_CommandList_Begin(commandList);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_MATERIAL, bindGroup);
    Fluxion_RHI_CommandList_End(commandList);

    // Compute pipeline: a shader + the same bind group layout.
    const u8 dummyBytecode[4] = { 0, 0, 0, 0 };
    FluxionRHIShaderDesc csDesc = { FLUXION_RHI_SHADER_STAGE_COMPUTE, dummyBytecode, sizeof(dummyBytecode), "main", "TestCS" };
    FluxionRHIShaderHandle computeShader = Fluxion_RHI_CreateShader(device, &csDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(computeShader));

    FluxionRHIComputePipelineDesc computeDesc;
    memset(&computeDesc, 0, sizeof(computeDesc));
    computeDesc.computeShader = computeShader;
    computeDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = layout;
    computeDesc.bindGroupLayoutCount = FLUXION_RHI_MAX_BIND_GROUPS;
    computeDesc.debugName = "TestComputePipeline";
    FluxionRHIPipelineHandle computePipeline = Fluxion_RHI_CreateComputePipeline(device, &computeDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(computePipeline));

    // A compute pipeline created against an invalid shader handle
    // gracefully fails.
    FluxionRHIShaderHandle badShader = { 999, 0 };
    FluxionRHIComputePipelineDesc badComputeDesc = computeDesc;
    badComputeDesc.computeShader = badShader;
    FluxionRHIPipelineHandle badComputePipeline = Fluxion_RHI_CreateComputePipeline(device, &badComputeDesc);
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(badComputePipeline));

    // Pipeline cache save/load: the Null backend has nothing to persist,
    // so both consistently report "unsupported" rather than pretending
    // to succeed.
    TEST_CHECK(ctx, !Fluxion_RHI_Device_SavePipelineCacheToFile(device, "unused_null_backend_cache.bin"));
    TEST_CHECK(ctx, !Fluxion_RHI_Device_LoadPipelineCacheFromFile(device, "unused_null_backend_cache.bin"));

    // Teardown.
    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyPipeline(computePipeline);
    Fluxion_RHI_DestroyShader(computeShader);
    Fluxion_RHI_DestroyBindGroup(bindGroup);
    Fluxion_RHI_DestroyBindGroupLayout(layout);
    Fluxion_RHI_DestroyTextureView(textureView);
    Fluxion_RHI_DestroyTexture(texture);
    Fluxion_RHI_DestroyBuffer(uniformBuffer);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
