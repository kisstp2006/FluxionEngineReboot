// Graphics + compute pipelines. Every pipeline's VkPipelineLayout is
// built from the caller's up-to-FLUXION_RHI_MAX_BIND_GROUPS
// FluxionRHIBindGroupLayoutHandles (resolved to VkDescriptorSetLayout via
// VulkanBindGroup.cpp) plus one push-constant range, so a caller that
// passes zero bind group layouts gets exactly the old push-constant-only
// layout shape. Every pipeline creation also goes through the device's
// shared VkPipelineCache (see Fluxion_RHIVulkan_SavePipelineCacheToFile/
// LoadPipelineCacheFromFile below). Built entirely against Dynamic
// Rendering (VkPipelineRenderingCreateInfo) -- there is no VkRenderPass
// anywhere in this backend.

#include "VulkanCommon.h"

#include <cstdio>
#include <vector>

#define FLUXION_RHI_VULKAN_PUSH_CONSTANT_SIZE 128

static FluxionRHIVulkanSlot s_pipelineSlots[FLUXION_RHI_VULKAN_MAX_PIPELINES];
static FluxionRHIVulkanPipeline s_pipelines[FLUXION_RHI_VULKAN_MAX_PIPELINES];

// Forward-declared: defined near the Save/LoadPipelineCacheFromFile
// functions below, but also needed here to lazily create an empty cache
// the first time either pipeline kind is created.
static bool Fluxion_RHIVulkan_EnsurePipelineCache(FluxionRHIVulkanDevice* deviceState, const void* initialData, usize initialDataSize);

static VkPrimitiveTopology Fluxion_RHIVulkan_MapTopology(FluxionRHIPrimitiveTopology topology)
{
    switch (topology)
    {
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_LINE_LIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_POINT_LIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static VkCullModeFlags Fluxion_RHIVulkan_MapCullMode(FluxionRHICullMode mode)
{
    switch (mode)
    {
        case FLUXION_RHI_CULL_MODE_FRONT: return VK_CULL_MODE_FRONT_BIT;
        case FLUXION_RHI_CULL_MODE_BACK: return VK_CULL_MODE_BACK_BIT;
        default: return VK_CULL_MODE_NONE;
    }
}

static VkCompareOp Fluxion_RHIVulkan_MapCompareOp(FluxionRHICompareOp op)
{
    switch (op)
    {
        case FLUXION_RHI_COMPARE_OP_LESS: return VK_COMPARE_OP_LESS;
        case FLUXION_RHI_COMPARE_OP_EQUAL: return VK_COMPARE_OP_EQUAL;
        case FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER: return VK_COMPARE_OP_GREATER;
        case FLUXION_RHI_COMPARE_OP_NOT_EQUAL: return VK_COMPARE_OP_NOT_EQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER_OR_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case FLUXION_RHI_COMPARE_OP_ALWAYS: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_NEVER;
    }
}

// Shared by the graphics and compute paths -- resolves the caller's
// FluxionRHIBindGroupLayoutHandles to native VkDescriptorSetLayouts and
// builds the VkPipelineLayoutCreateInfo both pipeline kinds need.
// VkPipelineLayoutCreateInfo's pSetLayouts array has no way to mark a set
// index as "unused", so an invalid handle at index i (a shader that
// doesn't use that FLUXION_RHI_BIND_GROUP_* frequency) is filled with the
// device's shared zero-binding empty layout instead of leaving a gap.
// Returns false (and leaves *outLayoutInfo untouched) only if a handle is
// both present (FLUXION_HANDLE_IS_VALID) and fails to resolve, e.g. the
// caller passed a stale/destroyed layout.
static bool Fluxion_RHIVulkan_BuildPipelineLayoutInfo(FluxionRHIVulkanDevice* deviceState, const FluxionRHIBindGroupLayoutHandle* bindGroupLayouts, u32 layoutCount,
    VkDescriptorSetLayout* outSetLayouts, VkPipelineLayoutCreateInfo* outLayoutInfo, VkPushConstantRange* outPushConstant)
{
    u32 setLayoutCount = layoutCount > FLUXION_RHI_MAX_BIND_GROUPS ? FLUXION_RHI_MAX_BIND_GROUPS : layoutCount;
    for (u32 i = 0; i < setLayoutCount; ++i)
    {
        if (!FLUXION_HANDLE_IS_VALID(bindGroupLayouts[i]))
        {
            outSetLayouts[i] = Fluxion_RHIVulkan_GetOrCreateEmptyBindGroupLayout(deviceState);
            continue;
        }
        outSetLayouts[i] = Fluxion_RHIVulkan_ResolveBindGroupLayout(bindGroupLayouts[i]);
        if (outSetLayouts[i] == VK_NULL_HANDLE) return false;
    }

    outPushConstant->stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    outPushConstant->offset = 0;
    outPushConstant->size = FLUXION_RHI_VULKAN_PUSH_CONSTANT_SIZE;

    *outLayoutInfo = {};
    outLayoutInfo->sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    outLayoutInfo->setLayoutCount = setLayoutCount;
    outLayoutInfo->pSetLayouts = setLayoutCount > 0 ? outSetLayouts : nullptr;
    outLayoutInfo->pushConstantRangeCount = 1;
    outLayoutInfo->pPushConstantRanges = outPushConstant;
    return true;
}

FluxionRHIPipelineHandle Fluxion_RHIVulkan_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    VkShaderModule vertexModule = desc != nullptr ? Fluxion_RHIVulkan_ResolveShaderModule(desc->vertexShader) : VK_NULL_HANDLE;
    VkShaderModule fragmentModule = desc != nullptr ? Fluxion_RHIVulkan_ResolveShaderModule(desc->fragmentShader) : VK_NULL_HANDLE;
    if (deviceState == nullptr || desc == nullptr || vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, &index, &generation)) return invalid;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = Fluxion_RHIVulkan_ResolveShaderEntryPoint(desc->vertexShader);
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = Fluxion_RHIVulkan_ResolveShaderEntryPoint(desc->fragmentShader);

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = desc->vertexLayout.stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[FLUXION_RHI_MAX_VERTEX_ATTRIBUTES];
    for (u32 i = 0; i < desc->vertexLayout.attributeCount; ++i)
    {
        attributes[i].location = desc->vertexLayout.attributes[i].location;
        attributes[i].binding = 0;
        attributes[i].format = Fluxion_RHIVulkan_MapFormat(desc->vertexLayout.attributes[i].format);
        attributes[i].offset = desc->vertexLayout.attributes[i].offset;
    }

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = desc->vertexLayout.attributeCount > 0 ? 1 : 0;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = desc->vertexLayout.attributeCount;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = Fluxion_RHIVulkan_MapTopology(desc->topology);

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization = {};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = desc->rasterState.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterization.cullMode = Fluxion_RHIVulkan_MapCullMode(desc->rasterState.cullMode);
    rasterization.frontFace = desc->rasterState.frontFaceCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc->depthState.testEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc->depthState.writeEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = Fluxion_RHIVulkan_MapCompareOp(desc->depthState.compareOp);

    VkPipelineColorBlendAttachmentState blendAttachments[FLUXION_RHI_MAX_RENDER_TARGETS];
    for (u32 i = 0; i < desc->colorFormatCount; ++i)
    {
        blendAttachments[i] = {};
        blendAttachments[i].blendEnable = desc->blendState.blendEnable ? VK_TRUE : VK_FALSE;
        blendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = desc->colorFormatCount;
    colorBlend.pAttachments = blendAttachments;

    VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkFormat colorFormats[FLUXION_RHI_MAX_RENDER_TARGETS];
    for (u32 i = 0; i < desc->colorFormatCount; ++i) colorFormats[i] = Fluxion_RHIVulkan_MapFormat(desc->colorFormats[i]);

    VkPipelineRenderingCreateInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = desc->colorFormatCount;
    renderingInfo.pColorAttachmentFormats = colorFormats;
    renderingInfo.depthAttachmentFormat = desc->depthFormat != FLUXION_RHI_FORMAT_UNKNOWN ? Fluxion_RHIVulkan_MapFormat(desc->depthFormat) : VK_FORMAT_UNDEFINED;

    VkDescriptorSetLayout setLayouts[FLUXION_RHI_MAX_BIND_GROUPS];
    VkPipelineLayoutCreateInfo layoutInfo;
    VkPushConstantRange pushConstant;
    if (!Fluxion_RHIVulkan_BuildPipelineLayoutInfo(deviceState, desc->bindGroupLayouts, desc->bindGroupLayoutCount, setLayouts, &layoutInfo, &pushConstant))
    {
        Fluxion_RHIVulkan_PoolFree(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, index, generation);
        return invalid;
    }

    FluxionRHIVulkanPipeline* pipeline = &s_pipelines[index];
    *pipeline = FluxionRHIVulkanPipeline{};
    pipeline->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (vkCreatePipelineLayout(deviceState->device, &layoutInfo, nullptr, &pipeline->layout) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, index, generation);
        return invalid;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline->layout;

    Fluxion_RHIVulkan_EnsurePipelineCache(deviceState, nullptr, 0);
    if (vkCreateGraphicsPipelines(deviceState->device, deviceState->pipelineCache, 1, &pipelineInfo, nullptr, &pipeline->pipeline) != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(deviceState->device, pipeline->layout, nullptr);
        Fluxion_RHIVulkan_PoolFree(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, index, generation);
        return invalid;
    }

    FluxionRHIPipelineHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

FluxionRHIPipelineHandle Fluxion_RHIVulkan_CreateComputePipeline(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    VkShaderModule computeModule = desc != nullptr ? Fluxion_RHIVulkan_ResolveShaderModule(desc->computeShader) : VK_NULL_HANDLE;
    if (deviceState == nullptr || desc == nullptr || computeModule == VK_NULL_HANDLE) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, &index, &generation)) return invalid;

    VkDescriptorSetLayout setLayouts[FLUXION_RHI_MAX_BIND_GROUPS];
    VkPipelineLayoutCreateInfo layoutInfo;
    VkPushConstantRange pushConstant;
    if (!Fluxion_RHIVulkan_BuildPipelineLayoutInfo(deviceState, desc->bindGroupLayouts, desc->bindGroupLayoutCount, setLayouts, &layoutInfo, &pushConstant))
    {
        Fluxion_RHIVulkan_PoolFree(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, index, generation);
        return invalid;
    }

    FluxionRHIVulkanPipeline* pipeline = &s_pipelines[index];
    *pipeline = FluxionRHIVulkanPipeline{};
    pipeline->bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    if (vkCreatePipelineLayout(deviceState->device, &layoutInfo, nullptr, &pipeline->layout) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, index, generation);
        return invalid;
    }

    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = computeModule;
    stage.pName = Fluxion_RHIVulkan_ResolveShaderEntryPoint(desc->computeShader);

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = pipeline->layout;

    Fluxion_RHIVulkan_EnsurePipelineCache(deviceState, nullptr, 0);
    if (vkCreateComputePipelines(deviceState->device, deviceState->pipelineCache, 1, &pipelineInfo, nullptr, &pipeline->pipeline) != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(deviceState->device, pipeline->layout, nullptr);
        Fluxion_RHIVulkan_PoolFree(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, index, generation);
        return invalid;
    }

    FluxionRHIPipelineHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_FinalizePipelineSlot(u32 index)
{
    FluxionRHIVulkanPipeline* pipeline = &s_pipelines[index];
    VkDevice device = Fluxion_RHIVulkan_GetOwningDevice();
    if (pipeline->pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline->pipeline, nullptr);
    if (pipeline->layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline->layout, nullptr);
    *pipeline = FluxionRHIVulkanPipeline{};
    Fluxion_RHIVulkan_PoolFinalize(s_pipelineSlots, index);
}

void Fluxion_RHIVulkan_DestroyPipeline(FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, pipeline.index, pipeline.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyPipeline called with an invalid or already-destroyed handle");
        return;
    }
    // See VulkanMemory.cpp's DestroyBuffer comment -- PoolRetire, not PoolFree.
    Fluxion_RHIVulkan_PoolRetire(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, pipeline.index, pipeline.generation);
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHIVulkan_Retire(deviceState, FluxionRHIVulkanRetiredEntry::Kind::Pipeline, pipeline.index, deviceState->gcCounter);
    else
        Fluxion_RHIVulkan_FinalizePipelineSlot(pipeline.index);
}

FluxionRHIVulkanPipeline* Fluxion_RHIVulkan_ResolvePipeline(FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, pipeline.index, pipeline.generation)) return nullptr;
    return &s_pipelines[pipeline.index];
}

// --- Pipeline cache ----------------------------------------------------------
//
// Created empty (or seeded from a prior LoadPipelineCacheFromFile) the
// first time a device needs it, then handed to every subsequent
// vkCreate{Graphics,Compute}Pipelines call above -- never read from or
// written to disk on its own; that's entirely the caller's call via
// these two functions.

static bool Fluxion_RHIVulkan_EnsurePipelineCache(FluxionRHIVulkanDevice* deviceState, const void* initialData, usize initialDataSize)
{
    if (deviceState->pipelineCache != VK_NULL_HANDLE)
    {
        // A cache already exists (created empty by an earlier pipeline
        // creation) -- there is no way to "load into" an existing
        // VkPipelineCache, so seeding it now would require recreating it.
        // Since this is only ever called before any pipeline has been
        // created (see Fluxion_RHIVulkan_LoadPipelineCacheFromFile's
        // caller contract), this path only triggers if a caller loads
        // after already creating a pipeline -- not supported, fails
        // cleanly instead of silently ignoring the requested data.
        return initialData == nullptr;
    }

    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = initialDataSize;
    cacheInfo.pInitialData = initialData;
    return vkCreatePipelineCache(deviceState->device, &cacheInfo, nullptr, &deviceState->pipelineCache) == VK_SUCCESS;
}

bool Fluxion_RHIVulkan_SavePipelineCacheToFile(FluxionRHIDeviceHandle device, const char* path)
{
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || path == nullptr || deviceState->pipelineCache == VK_NULL_HANDLE) return false;

    usize dataSize = 0;
    if (vkGetPipelineCacheData(deviceState->device, deviceState->pipelineCache, &dataSize, nullptr) != VK_SUCCESS || dataSize == 0) return false;

    std::vector<u8> data(dataSize);
    if (vkGetPipelineCacheData(deviceState->device, deviceState->pipelineCache, &dataSize, data.data()) != VK_SUCCESS) return false;

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) return false;
#else
    file = fopen(path, "wb");
    if (file == nullptr) return false;
#endif
    usize written = fwrite(data.data(), 1, dataSize, file);
    fclose(file);
    return written == dataSize;
}

bool Fluxion_RHIVulkan_LoadPipelineCacheFromFile(FluxionRHIDeviceHandle device, const char* path)
{
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || path == nullptr) return false;

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr) return false;
#else
    file = fopen(path, "rb");
    if (file == nullptr) return false;
#endif
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize <= 0)
    {
        fclose(file);
        return false;
    }

    std::vector<u8> data((usize)fileSize);
    usize readBytes = fread(data.data(), 1, data.size(), file);
    fclose(file);
    if (readBytes != data.size()) return false;

    // A driver/version-mismatched blob is silently rejected by Vulkan
    // itself (VkPipelineCacheCreateInfo's header-validation rules) --
    // vkCreatePipelineCache still succeeds in that case, just with an
    // effectively empty cache, so there is nothing extra to validate here.
    return Fluxion_RHIVulkan_EnsurePipelineCache(deviceState, data.data(), data.size());
}
