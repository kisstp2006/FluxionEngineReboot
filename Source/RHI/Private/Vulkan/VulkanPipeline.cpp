// Graphics pipelines. No binding model yet -- every pipeline gets a
// minimal VkPipelineLayout with zero descriptor sets and one
// push-constant range, enough for a caller to push e.g. a transform
// matrix. A full binding model (descriptor sets, reflection-driven
// layouts, a pipeline cache) is deliberately out of scope here and will
// build on top of this same entry point later without needing a second,
// temporary pipeline API. Built entirely against Dynamic Rendering
// (VkPipelineRenderingCreateInfo) -- there is no VkRenderPass anywhere in
// this backend.

#include "VulkanCommon.h"

#define FLUXION_RHI_VULKAN_PUSH_CONSTANT_SIZE 128

static FluxionRHIVulkanSlot s_pipelineSlots[FLUXION_RHI_VULKAN_MAX_PIPELINES];
static FluxionRHIVulkanPipeline s_pipelines[FLUXION_RHI_VULKAN_MAX_PIPELINES];

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

    VkPushConstantRange pushConstant = {};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = FLUXION_RHI_VULKAN_PUSH_CONSTANT_SIZE;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    FluxionRHIVulkanPipeline* pipeline = &s_pipelines[index];
    *pipeline = FluxionRHIVulkanPipeline{};
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

    if (vkCreateGraphicsPipelines(deviceState->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline->pipeline) != VK_SUCCESS)
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
}

void Fluxion_RHIVulkan_DestroyPipeline(FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, pipeline.index, pipeline.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyPipeline called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHIVulkan_PoolFree(s_pipelineSlots, FLUXION_RHI_VULKAN_MAX_PIPELINES, pipeline.index, pipeline.generation);
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
