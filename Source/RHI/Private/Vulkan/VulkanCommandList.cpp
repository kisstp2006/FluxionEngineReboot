// Command lists, format/resource-state mapping, and queue submission.
// Every FluxionRHICommandListHandle owns its own VkCommandPool
// (RESET_COMMAND_BUFFER_BIT) + one primary VkCommandBuffer -- no
// per-thread pool pooling yet, since nothing in this engine drives
// parallel command recording across worker threads today; that only
// becomes worth the added complexity once a render graph actually
// schedules recording across multiple threads. Uses Dynamic Rendering +
// Synchronization2 exclusively: there is no VkRenderPass/VkFramebuffer/
// vkCmdPipelineBarrier anywhere in this file.

#include "VulkanCommon.h"

#include <Fluxion/Foundation/Log.h>

#include <cstdio>
#include <cstring>

// --- Format mapping -----------------------------------------------------

VkFormat Fluxion_RHIVulkan_MapFormat(FluxionRHIFormat format)
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case FLUXION_RHI_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case FLUXION_RHI_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case FLUXION_RHI_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        case FLUXION_RHI_FORMAT_R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
        case FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case FLUXION_RHI_FORMAT_R32G32B32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
        case FLUXION_RHI_FORMAT_R32G32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
        case FLUXION_RHI_FORMAT_D32_FLOAT: return VK_FORMAT_D32_SFLOAT;
        case FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;

        case FLUXION_RHI_FORMAT_BC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
        case FLUXION_RHI_FORMAT_BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
        case FLUXION_RHI_FORMAT_BC6H_UFLOAT: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case FLUXION_RHI_FORMAT_BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
        case FLUXION_RHI_FORMAT_BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;

        case FLUXION_RHI_FORMAT_ASTC_4X4_UNORM: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case FLUXION_RHI_FORMAT_ASTC_4X4_SRGB: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT: return VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK;
        case FLUXION_RHI_FORMAT_ASTC_6X6_UNORM: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case FLUXION_RHI_FORMAT_ASTC_6X6_SRGB: return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        case FLUXION_RHI_FORMAT_ASTC_6X6_FLOAT: return VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK;
        case FLUXION_RHI_FORMAT_ASTC_8X8_UNORM: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case FLUXION_RHI_FORMAT_ASTC_8X8_SRGB: return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
        case FLUXION_RHI_FORMAT_ASTC_8X8_FLOAT: return VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK;

        default: return VK_FORMAT_UNDEFINED;
    }
}

FluxionRHIFormat Fluxion_RHIVulkan_MapFormatBack(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_R8G8B8A8_UNORM: return FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return FLUXION_RHI_FORMAT_R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM: return FLUXION_RHI_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB: return FLUXION_RHI_FORMAT_B8G8R8A8_SRGB;
        case VK_FORMAT_R32_SFLOAT: return FLUXION_RHI_FORMAT_R32_FLOAT;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
        case VK_FORMAT_R32G32B32_SFLOAT: return FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
        case VK_FORMAT_R32G32_SFLOAT: return FLUXION_RHI_FORMAT_R32G32_FLOAT;
        case VK_FORMAT_D32_SFLOAT: return FLUXION_RHI_FORMAT_D32_FLOAT;
        case VK_FORMAT_D24_UNORM_S8_UINT: return FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT;

        // Kept in step with the table above, and not only for symmetry:
        // the copy path below asks this what it is holding, so a
        // compressed format missing from here would be sized as though it
        // were not compressed.
        case VK_FORMAT_BC4_UNORM_BLOCK: return FLUXION_RHI_FORMAT_BC4_UNORM;
        case VK_FORMAT_BC5_UNORM_BLOCK: return FLUXION_RHI_FORMAT_BC5_UNORM;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK: return FLUXION_RHI_FORMAT_BC6H_UFLOAT;
        case VK_FORMAT_BC7_UNORM_BLOCK: return FLUXION_RHI_FORMAT_BC7_UNORM;
        case VK_FORMAT_BC7_SRGB_BLOCK: return FLUXION_RHI_FORMAT_BC7_SRGB;

        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return FLUXION_RHI_FORMAT_ASTC_4X4_UNORM;
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return FLUXION_RHI_FORMAT_ASTC_4X4_SRGB;
        case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK: return FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT;
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: return FLUXION_RHI_FORMAT_ASTC_6X6_UNORM;
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: return FLUXION_RHI_FORMAT_ASTC_6X6_SRGB;
        case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK: return FLUXION_RHI_FORMAT_ASTC_6X6_FLOAT;
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: return FLUXION_RHI_FORMAT_ASTC_8X8_UNORM;
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: return FLUXION_RHI_FORMAT_ASTC_8X8_SRGB;
        case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK: return FLUXION_RHI_FORMAT_ASTC_8X8_FLOAT;

        default: return FLUXION_RHI_FORMAT_UNKNOWN;
    }
}

// --- Portable resource state -> Vulkan (decision: doc Rule "RHI exposes
// GPU concepts through portable states, backend maps them") --------------

void Fluxion_RHIVulkan_MapResourceState(FluxionRHIResourceState state, VkImageLayout* outLayout, VkAccessFlags2* outAccess, VkPipelineStageFlags2* outStage)
{
    switch (state)
    {
        case FLUXION_RHI_RESOURCE_STATE_UNDEFINED:
            *outLayout = VK_IMAGE_LAYOUT_UNDEFINED; *outAccess = VK_ACCESS_2_NONE; *outStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_COMMON:
            *outLayout = VK_IMAGE_LAYOUT_GENERAL; *outAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT; *outStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE:
            *outLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; *outAccess = VK_ACCESS_2_TRANSFER_READ_BIT; *outStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION:
            *outLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; *outAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT; *outStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_VERTEX_BUFFER:
            *outLayout = VK_IMAGE_LAYOUT_UNDEFINED; *outAccess = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT; *outStage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_INDEX_BUFFER:
            *outLayout = VK_IMAGE_LAYOUT_UNDEFINED; *outAccess = VK_ACCESS_2_INDEX_READ_BIT; *outStage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_CONSTANT_BUFFER:
            *outLayout = VK_IMAGE_LAYOUT_UNDEFINED; *outAccess = VK_ACCESS_2_UNIFORM_READ_BIT; *outStage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_SHADER_READ:
            *outLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; *outAccess = VK_ACCESS_2_SHADER_READ_BIT; *outStage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE:
            *outLayout = VK_IMAGE_LAYOUT_GENERAL; *outAccess = VK_ACCESS_2_SHADER_WRITE_BIT; *outStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET:
            *outLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; *outAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT; *outStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_DEPTH_READ:
            *outLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; *outAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT; *outStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE:
            *outLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; *outAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; *outStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            return;
        case FLUXION_RHI_RESOURCE_STATE_PRESENT:
            *outLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; *outAccess = VK_ACCESS_2_NONE; *outStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            return;
    }
    *outLayout = VK_IMAGE_LAYOUT_UNDEFINED; *outAccess = VK_ACCESS_2_NONE; *outStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

// --- Command lists --------------------------------------------------------

struct FluxionRHIVulkanCommandList
{
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    FluxionRHIQueueType queueType = FLUXION_RHI_QUEUE_TYPE_GRAPHICS;
    bool recording = false;
    bool insideRendering = false;
    // Set by SetPipeline, consumed by SetBindGroup -- vkCmdBindDescriptorSets
    // needs both the bind point (graphics vs. compute) and the pipeline
    // layout a bound set is being matched against, neither of which the
    // portable FluxionRHI_CommandList_SetBindGroup call carries itself.
    VkPipelineLayout currentPipelineLayout = VK_NULL_HANDLE;
    VkPipelineBindPoint currentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

static FluxionRHIVulkanSlot s_commandListSlots[FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS];
static FluxionRHIVulkanCommandList s_commandLists[FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS];

static u32 Fluxion_RHIVulkan_QueueFamilyFor(FluxionRHIVulkanDevice* deviceState, FluxionRHIQueueType type)
{
    switch (type)
    {
        case FLUXION_RHI_QUEUE_TYPE_COMPUTE: return deviceState->computeFamily;
        case FLUXION_RHI_QUEUE_TYPE_TRANSFER: return deviceState->transferFamily;
        default: return deviceState->graphicsFamily;
    }
}

FluxionRHICommandListHandle Fluxion_RHIVulkan_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHICommandListHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_commandListSlots, FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS, &index, &generation)) return invalid;

    FluxionRHIVulkanCommandList* cl = &s_commandLists[index];
    *cl = FluxionRHIVulkanCommandList{};
    cl->queueType = type;

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = Fluxion_RHIVulkan_QueueFamilyFor(deviceState, type);
    if (vkCreateCommandPool(deviceState->device, &poolInfo, nullptr, &cl->pool) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_commandListSlots, FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS, index, generation);
        return invalid;
    }

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cl->pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(deviceState->device, &allocInfo, &cl->commandBuffer) != VK_SUCCESS)
    {
        vkDestroyCommandPool(deviceState->device, cl->pool, nullptr);
        Fluxion_RHIVulkan_PoolFree(s_commandListSlots, FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS, index, generation);
        return invalid;
    }

    FluxionRHICommandListHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_DestroyCommandList(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_commandListSlots, FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS, commandList.index, commandList.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyCommandList called with an invalid or already-destroyed handle");
        return;
    }
    // Not GC-deferred (unlike Buffer/Texture/...): a command list is
    // expected to be idle (its last submission's fence already waited on,
    // or the device idle) before the caller destroys it -- the intended
    // usage is a fixed set of command lists created once and reused for
    // the whole run rather than churned per-frame.
    FluxionRHIVulkanCommandList* cl = &s_commandLists[commandList.index];
    if (cl->pool != VK_NULL_HANDLE) vkDestroyCommandPool(Fluxion_RHIVulkan_GetOwningDevice(), cl->pool, nullptr);
    *cl = FluxionRHIVulkanCommandList{};
    Fluxion_RHIVulkan_PoolFree(s_commandListSlots, FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS, commandList.index, commandList.generation);
}

VkCommandBuffer Fluxion_RHIVulkan_ResolveCommandBuffer(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_commandListSlots, FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS, commandList.index, commandList.generation)) return VK_NULL_HANDLE;
    return s_commandLists[commandList.index].commandBuffer;
}

FluxionRHIQueueType Fluxion_RHIVulkan_ResolveCommandListQueueType(FluxionRHICommandListHandle commandList)
{
    return s_commandLists[commandList.index].queueType;
}

static FluxionRHIVulkanCommandList* Fluxion_RHIVulkan_RequireRecording(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_commandListSlots, FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS, commandList.index, commandList.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: command list handle is invalid or destroyed");
        return nullptr;
    }
    FluxionRHIVulkanCommandList* cl = &s_commandLists[commandList.index];
    if (!cl->recording)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: command recorded outside Begin/End");
        return nullptr;
    }
    return cl;
}

void Fluxion_RHIVulkan_CommandListBegin(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_commandListSlots, FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS, commandList.index, commandList.generation)) return;
    FluxionRHIVulkanCommandList* cl = &s_commandLists[commandList.index];
    FLUXION_ASSERT_MSG(!cl->recording, "Fluxion RHI Vulkan backend: Begin called while already recording");

    vkResetCommandBuffer(cl->commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cl->commandBuffer, &beginInfo);
    cl->recording = true;
    cl->currentPipelineLayout = VK_NULL_HANDLE; // a fresh recording has no pipeline bound yet
}

void Fluxion_RHIVulkan_CommandListEnd(FluxionRHICommandListHandle commandList)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    if (cl == nullptr) return;
    FLUXION_ASSERT_MSG(!cl->insideRendering, "Fluxion RHI Vulkan backend: End called while still inside BeginRendering/EndRendering");
    vkEndCommandBuffer(cl->commandBuffer);
    cl->recording = false;
}

void Fluxion_RHIVulkan_CommandListBeginRendering(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    if (cl == nullptr || desc == nullptr) return;
    FLUXION_ASSERT_MSG(!cl->insideRendering, "Fluxion RHI Vulkan backend: BeginRendering called twice without an EndRendering");

    VkRenderingAttachmentInfo colorAttachments[FLUXION_RHI_MAX_RENDER_TARGETS];
    u32 colorCount = desc->colorAttachmentCount > FLUXION_RHI_MAX_RENDER_TARGETS ? FLUXION_RHI_MAX_RENDER_TARGETS : desc->colorAttachmentCount;
    for (u32 i = 0; i < colorCount; ++i)
    {
        FluxionRHIVulkanTextureView* view = Fluxion_RHIVulkan_ResolveTextureView(desc->colorAttachments[i].view);
        colorAttachments[i] = {};
        colorAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[i].imageView = view != nullptr ? view->view : VK_NULL_HANDLE;
        colorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[i].loadOp = desc->colorAttachments[i].clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::memcpy(colorAttachments[i].clearValue.color.float32, desc->colorAttachments[i].clearColor, sizeof(f32) * 4);
    }

    VkRenderingAttachmentInfo depthAttachment = {};
    bool hasDepth = desc->depthAttachment != nullptr;
    if (hasDepth)
    {
        FluxionRHIVulkanTextureView* view = Fluxion_RHIVulkan_ResolveTextureView(desc->depthAttachment->view);
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = view != nullptr ? view->view : VK_NULL_HANDLE;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = desc->depthAttachment->clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil.depth = desc->depthAttachment->clearColor[0];
        depthAttachment.clearValue.depthStencil.stencil = (u32)desc->depthAttachment->clearColor[1];
    }

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = { desc->width, desc->height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = colorCount;
    renderingInfo.pColorAttachments = colorAttachments;
    renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr && deviceState->cmdBeginRendering != nullptr)
        deviceState->cmdBeginRendering(cl->commandBuffer, &renderingInfo);

    // Every pipeline in this backend enables VK_DYNAMIC_STATE_VIEWPORT/
    // SCISSOR, so Vulkan requires them to be set before any draw. A
    // per-draw viewport isn't meaningful yet (nothing calls for anything
    // other than "cover the whole render target"), so a single
    // full-render-area default set once per BeginRendering is exactly
    // what every caller wants -- this also avoids adding SetViewport/
    // SetScissor to the RHI contract before anything actually needs a
    // different value.
    // The viewport is given a negative height, starting at the bottom
    // rather than the top. Vulkan's clip space puts +Y downwards, unlike
    // every other backend here, and a flipped viewport is what turns it
    // back the right way up -- so a positive Y is upwards on the screen
    // whichever backend is running, and nothing above the RHI has to know
    // which one that is. Left uncompensated, the difference hides inside
    // anything symmetrical and only shows when something is not: a cube
    // looks identical either way, a line pointing up does not.
    //
    // Available without asking since Vulkan 1.1, and this backend already
    // refuses anything below 1.2.
    VkViewport viewport = { 0.0f, (f32)desc->height, (f32)desc->width, -(f32)desc->height, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { desc->width, desc->height } };
    vkCmdSetViewport(cl->commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(cl->commandBuffer, 0, 1, &scissor);

    cl->insideRendering = true;
}

void Fluxion_RHIVulkan_CommandListEndRendering(FluxionRHICommandListHandle commandList)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    if (cl == nullptr) return;
    FLUXION_ASSERT_MSG(cl->insideRendering, "Fluxion RHI Vulkan backend: EndRendering called without a matching BeginRendering");
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr && deviceState->cmdEndRendering != nullptr)
        deviceState->cmdEndRendering(cl->commandBuffer);
    cl->insideRendering = false;
}

void Fluxion_RHIVulkan_CommandListSetPipeline(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    FluxionRHIVulkanPipeline* pipelineState = Fluxion_RHIVulkan_ResolvePipeline(pipeline);
    if (cl == nullptr || pipelineState == nullptr) return;
    vkCmdBindPipeline(cl->commandBuffer, pipelineState->bindPoint, pipelineState->pipeline);
    cl->currentPipelineLayout = pipelineState->layout;
    cl->currentBindPoint = pipelineState->bindPoint;
}

void Fluxion_RHIVulkan_CommandListSetBindGroup(FluxionRHICommandListHandle commandList, u32 groupIndex, FluxionRHIBindGroupHandle bindGroup)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    VkDescriptorSet set = Fluxion_RHIVulkan_ResolveBindGroup(bindGroup);
    if (cl == nullptr || set == VK_NULL_HANDLE || cl->currentPipelineLayout == VK_NULL_HANDLE) return;
    vkCmdBindDescriptorSets(cl->commandBuffer, cl->currentBindPoint, cl->currentPipelineLayout, groupIndex, 1, &set, 0, nullptr);
}

void Fluxion_RHIVulkan_CommandListSetVertexBuffer(FluxionRHICommandListHandle commandList, u32 slot, FluxionRHIBufferHandle buffer, usize offset)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    FluxionRHIVulkanBuffer* bufferState = Fluxion_RHIVulkan_ResolveBuffer(buffer);
    if (cl == nullptr || bufferState == nullptr) return;
    VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(cl->commandBuffer, slot, 1, &bufferState->buffer, &vkOffset);
}

void Fluxion_RHIVulkan_CommandListSetIndexBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle buffer, usize offset, bool use16BitIndices)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    FluxionRHIVulkanBuffer* bufferState = Fluxion_RHIVulkan_ResolveBuffer(buffer);
    if (cl == nullptr || bufferState == nullptr) return;
    vkCmdBindIndexBuffer(cl->commandBuffer, bufferState->buffer, offset, use16BitIndices ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void Fluxion_RHIVulkan_CommandListDraw(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    if (cl == nullptr) return;
    FLUXION_ASSERT_MSG(cl->insideRendering, "Fluxion RHI Vulkan backend: Draw called outside BeginRendering/EndRendering");
    vkCmdDraw(cl->commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void Fluxion_RHIVulkan_CommandListDrawIndexed(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    if (cl == nullptr) return;
    FLUXION_ASSERT_MSG(cl->insideRendering, "Fluxion RHI Vulkan backend: DrawIndexed called outside BeginRendering/EndRendering");
    vkCmdDrawIndexed(cl->commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void Fluxion_RHIVulkan_CommandListDrawIndirect(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    FluxionRHIVulkanBuffer* bufferState = Fluxion_RHIVulkan_ResolveBuffer(argsBuffer);
    if (cl == nullptr || bufferState == nullptr) return;
    FLUXION_ASSERT_MSG(cl->insideRendering, "Fluxion RHI Vulkan backend: DrawIndirect called outside BeginRendering/EndRendering");
    vkCmdDrawIndirect(cl->commandBuffer, bufferState->buffer, offset, drawCount, stride);
}

void Fluxion_RHIVulkan_CommandListDispatch(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    if (cl == nullptr) return;
    vkCmdDispatch(cl->commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void Fluxion_RHIVulkan_CommandListCopyBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHIBufferHandle dst, usize dstOffset, usize size)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    FluxionRHIVulkanBuffer* srcState = Fluxion_RHIVulkan_ResolveBuffer(src);
    FluxionRHIVulkanBuffer* dstState = Fluxion_RHIVulkan_ResolveBuffer(dst);
    if (cl == nullptr || srcState == nullptr || dstState == nullptr) return;
    VkBufferCopy region = { srcOffset, dstOffset, size };
    vkCmdCopyBuffer(cl->commandBuffer, srcState->buffer, dstState->buffer, 1, &region);
}

void Fluxion_RHIVulkan_CommandListCopyTexture(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, FluxionRHITextureHandle dst)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    FluxionRHIVulkanTexture* srcState = Fluxion_RHIVulkan_ResolveTexture(src);
    FluxionRHIVulkanTexture* dstState = Fluxion_RHIVulkan_ResolveTexture(dst);
    if (cl == nullptr || srcState == nullptr || dstState == nullptr) return;

    VkImageCopy region = {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { srcState->width, srcState->height, srcState->depth };
    vkCmdCopyImage(cl->commandBuffer, srcState->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstState->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void Fluxion_RHIVulkan_CommandListCopyBufferToTexture(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHITextureHandle dst, u32 mipLevel, u32 arrayLayer)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    FluxionRHIVulkanBuffer* srcState = Fluxion_RHIVulkan_ResolveBuffer(src);
    FluxionRHIVulkanTexture* dstState = Fluxion_RHIVulkan_ResolveTexture(dst);
    if (cl == nullptr || srcState == nullptr || dstState == nullptr) return;

    u32 width = dstState->width >> mipLevel; if (width == 0) width = 1;
    u32 height = dstState->height >> mipLevel; if (height == 0) height = 1;

    // The contract lays rows out FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT
    // apart (see RHI.h); bufferRowLength expresses that stride in
    // texels. Zero -- tight packing -- would only agree with the
    // contract for rows that are exactly 256 bytes wide, which is the
    // disagreement the contract exists to rule out.
    //
    // A "row" is a row of BLOCKS, which for an uncompressed format is a
    // row of texels and for a compressed one is not. Asked of the format
    // rather than worked out here, so that the answer cannot differ from
    // the one the caller sized its staging buffer with.
    const FluxionRHIFormat portableFormat = Fluxion_RHIVulkan_MapFormatBack(dstState->format);
    const FluxionRHIFormatInfo formatInfo = Fluxion_RHI_GetFormatInfo(portableFormat);
    const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(portableFormat, width);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;

    VkBufferImageCopy region = {};
    region.bufferOffset = srcOffset;
    // Blocks across, back into texels -- which is the unit this field is
    // in whether or not the format has blocks. The row alignment is 256
    // and every block size here divides it, so this stays a whole number
    // of blocks rather than rounding to one.
    region.bufferRowLength = formatInfo.blockBytes != 0
                                 ? (u32)(alignedRowBytes / formatInfo.blockBytes) * formatInfo.blockWidth
                                 : 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, arrayLayer, 1 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(cl->commandBuffer, srcState->buffer, dstState->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void Fluxion_RHIVulkan_CommandListCopyTextureToBuffer(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, u32 mipLevel, u32 arrayLayer, FluxionRHIBufferHandle dst, usize dstOffset)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    FluxionRHIVulkanTexture* srcState = Fluxion_RHIVulkan_ResolveTexture(src);
    FluxionRHIVulkanBuffer* dstState = Fluxion_RHIVulkan_ResolveBuffer(dst);
    if (cl == nullptr || srcState == nullptr || dstState == nullptr) return;

    u32 width = srcState->width >> mipLevel; if (width == 0) width = 1;
    u32 height = srcState->height >> mipLevel; if (height == 0) height = 1;

    // The same layout as the upload, said the same way -- see
    // CommandListCopyBufferToTexture above. The two directions have to
    // agree about where a row starts, and the surest way to make them
    // agree is for both to ask the same question of the same table.
    const FluxionRHIFormat portableFormat = Fluxion_RHIVulkan_MapFormatBack(srcState->format);
    const FluxionRHIFormatInfo formatInfo = Fluxion_RHI_GetFormatInfo(portableFormat);
    const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(portableFormat, width);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;

    VkBufferImageCopy region = {};
    region.bufferOffset = dstOffset;
    region.bufferRowLength = formatInfo.blockBytes != 0
                                 ? (u32)(alignedRowBytes / formatInfo.blockBytes) * formatInfo.blockWidth
                                 : 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, arrayLayer, 1 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyImageToBuffer(cl->commandBuffer, srcState->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstState->buffer, 1, &region);
}

bool Fluxion_RHIVulkan_DeviceIsFormatSupported(FluxionRHIDeviceHandle device, FluxionRHIFormat format)
{
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr) return false;

    const VkFormat native = Fluxion_RHIVulkan_MapFormat(format);
    if (native == VK_FORMAT_UNDEFINED) return false;

    // Asked of the physical device, which is where the answer lives --
    // and asked with the query that does NOT report an error for a format
    // it does not have. Creating the image to find out would.
    VkFormatProperties properties = {};
    vkGetPhysicalDeviceFormatProperties(deviceState->physicalDevice, native, &properties);

    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
}

void Fluxion_RHIVulkan_CommandListBarrier(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount)
{
    FluxionRHIVulkanCommandList* cl = Fluxion_RHIVulkan_RequireRecording(commandList);
    if (cl == nullptr || barriers == nullptr || barrierCount == 0) return;
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState == nullptr || deviceState->cmdPipelineBarrier2 == nullptr) return;

    VkImageMemoryBarrier2 imageBarriers[16];
    VkBufferMemoryBarrier2 bufferBarriers[16];
    u32 imageCount = 0, bufferCount = 0;

    for (u32 i = 0; i < barrierCount && (imageCount < 16 && bufferCount < 16); ++i)
    {
        VkImageLayout beforeLayout, afterLayout;
        VkAccessFlags2 beforeAccess, afterAccess;
        VkPipelineStageFlags2 beforeStage, afterStage;
        Fluxion_RHIVulkan_MapResourceState(barriers[i].before, &beforeLayout, &beforeAccess, &beforeStage);
        Fluxion_RHIVulkan_MapResourceState(barriers[i].after, &afterLayout, &afterAccess, &afterStage);

        if (FLUXION_HANDLE_IS_VALID(barriers[i].texture))
        {
            FluxionRHIVulkanTexture* texture = Fluxion_RHIVulkan_ResolveTexture(barriers[i].texture);
            if (texture == nullptr) continue;
            bool isDepth = texture->format == VK_FORMAT_D32_SFLOAT || texture->format == VK_FORMAT_D24_UNORM_S8_UINT;
            VkImageMemoryBarrier2 barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = beforeStage;
            barrier.srcAccessMask = beforeAccess;
            barrier.dstStageMask = afterStage;
            barrier.dstAccessMask = afterAccess;
            barrier.oldLayout = beforeLayout;
            barrier.newLayout = afterLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->image;
            barrier.subresourceRange = { (VkImageAspectFlags)(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, texture->mipLevels, 0, texture->arrayLayers };
            imageBarriers[imageCount++] = barrier;
        }
        else if (FLUXION_HANDLE_IS_VALID(barriers[i].buffer))
        {
            FluxionRHIVulkanBuffer* buffer = Fluxion_RHIVulkan_ResolveBuffer(barriers[i].buffer);
            if (buffer == nullptr) continue;
            VkBufferMemoryBarrier2 barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = beforeStage;
            barrier.srcAccessMask = beforeAccess;
            barrier.dstStageMask = afterStage;
            barrier.dstAccessMask = afterAccess;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer->buffer;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            bufferBarriers[bufferCount++] = barrier;
        }
    }

    VkDependencyInfo dependencyInfo = {};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = imageCount;
    dependencyInfo.pImageMemoryBarriers = imageBarriers;
    dependencyInfo.bufferMemoryBarrierCount = bufferCount;
    dependencyInfo.pBufferMemoryBarriers = bufferBarriers;
    deviceState->cmdPipelineBarrier2(cl->commandBuffer, &dependencyInfo);
}

// --- Submission -------------------------------------------------------------

void Fluxion_RHIVulkan_QueueSubmit(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence)
{
    extern VkQueue Fluxion_RHIVulkan_ResolveQueue(FluxionRHIQueueHandle);
    extern FluxionRHIVulkanDevice* Fluxion_RHIVulkan_ResolveDeviceFromQueue(FluxionRHIQueueHandle);
    VkQueue vkQueue = Fluxion_RHIVulkan_ResolveQueue(queue);
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDeviceFromQueue(queue);
    if (vkQueue == VK_NULL_HANDLE || deviceState == nullptr || deviceState->queueSubmit2 == nullptr) return;

    VkCommandBufferSubmitInfo cmdSubmitInfos[FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS];
    u32 cmdCount = commandListCount > FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS ? FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS : commandListCount;
    for (u32 i = 0; i < cmdCount; ++i)
    {
        cmdSubmitInfos[i] = {};
        cmdSubmitInfos[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdSubmitInfos[i].commandBuffer = Fluxion_RHIVulkan_ResolveCommandBuffer(commandLists[i]);
    }

    // Every submit signals the device-internal GC timeline (see the
    // retirement-queue comment in VulkanCommon.h) in addition to
    // whatever user fence was requested -- up to two timeline-semaphore
    // signals.
    VkSemaphoreSubmitInfo signalInfos[2];
    u32 signalCount = 0;

    u64 gcValue = Fluxion_RHIVulkan_NextGCValue(deviceState);
    signalInfos[signalCount] = {};
    signalInfos[signalCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfos[signalCount].semaphore = deviceState->gcTimeline;
    signalInfos[signalCount].value = gcValue;
    signalInfos[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ++signalCount;

    if (FLUXION_HANDLE_IS_VALID(signalFence))
    {
        u64 fenceValue = 0;
        VkSemaphore fenceSemaphore = Fluxion_RHIVulkan_FenceBeginSignal(signalFence, &fenceValue);
        if (fenceSemaphore != VK_NULL_HANDLE)
        {
            signalInfos[signalCount] = {};
            signalInfos[signalCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfos[signalCount].semaphore = fenceSemaphore;
            signalInfos[signalCount].value = fenceValue;
            signalInfos[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            ++signalCount;
        }
    }

    VkSubmitInfo2 submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = cmdCount;
    submitInfo.pCommandBufferInfos = cmdSubmitInfos;
    submitInfo.signalSemaphoreInfoCount = signalCount;
    submitInfo.pSignalSemaphoreInfos = signalInfos;

    VkResult submitResult = deviceState->queueSubmit2(vkQueue, 1, &submitInfo, VK_NULL_HANDLE);
    if (submitResult != VK_SUCCESS)
    {
        FLUXION_LOG_ERROR("RHIVulkan", "vkQueueSubmit2 failed with VkResult=%d", (int)submitResult);
        fflush(nullptr);
    }
}
