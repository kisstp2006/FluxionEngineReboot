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

// GPU memory: buffer/texture/view/sampler pools, all backed by VMA
// sub-allocations -- VMA picks the actual VkMemoryPropertyFlags/heap from
// FluxionRHIMemoryClass, and callers never see a VmaAllocation. Destroy*
// here never calls vkDestroy*/vmaDestroy* directly: it retires the slot
// and the real free happens from Fluxion_RHIVulkan_FinalizeRetired once
// the device confirms the GPU is done with it (see the retirement-queue
// comment in VulkanCommon.h).

#include "VulkanCommon.h"

#include <Fluxion/Foundation/Log.h>

static FluxionRHIVulkanSlot s_bufferSlots[FLUXION_RHI_VULKAN_MAX_BUFFERS];
static FluxionRHIVulkanBuffer s_buffers[FLUXION_RHI_VULKAN_MAX_BUFFERS];

static FluxionRHIVulkanSlot s_textureSlots[FLUXION_RHI_VULKAN_MAX_TEXTURES];
static FluxionRHIVulkanTexture s_textures[FLUXION_RHI_VULKAN_MAX_TEXTURES];

static FluxionRHIVulkanSlot s_viewSlots[FLUXION_RHI_VULKAN_MAX_TEXTURE_VIEWS];
static FluxionRHIVulkanTextureView s_views[FLUXION_RHI_VULKAN_MAX_TEXTURE_VIEWS];

static FluxionRHIVulkanSlot s_samplerSlots[FLUXION_RHI_VULKAN_MAX_SAMPLERS];
static VkSampler s_samplers[FLUXION_RHI_VULKAN_MAX_SAMPLERS];

// --- Buffers ------------------------------------------------------------

static VmaMemoryUsage Fluxion_RHIVulkan_MapMemoryClass(FluxionRHIMemoryClass memoryClass)
{
    switch (memoryClass)
    {
        case FLUXION_RHI_MEMORY_CLASS_GPU_ONLY: return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        case FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU: return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        case FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU: return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        case FLUXION_RHI_MEMORY_CLASS_TRANSIENT: return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        case FLUXION_RHI_MEMORY_CLASS_READBACK: return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }
    return VMA_MEMORY_USAGE_AUTO;
}

static VmaAllocationCreateFlags Fluxion_RHIVulkan_MapMemoryClassFlags(FluxionRHIMemoryClass memoryClass)
{
    switch (memoryClass)
    {
        case FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU: return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        case FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU:
        case FLUXION_RHI_MEMORY_CLASS_READBACK:
            return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        default: return 0;
    }
}

static VkBufferUsageFlags Fluxion_RHIVulkan_MapBufferUsage(u32 usageFlags)
{
    VkBufferUsageFlags vkUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (usageFlags & FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER) vkUsage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (usageFlags & FLUXION_RHI_BUFFER_USAGE_INDEX_BUFFER) vkUsage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (usageFlags & FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER) vkUsage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (usageFlags & FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER) vkUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (usageFlags & FLUXION_RHI_BUFFER_USAGE_INDIRECT) vkUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    return vkUsage;
}

FluxionRHIBufferHandle Fluxion_RHIVulkan_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc)
{
    FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr || desc->size == 0) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_bufferSlots, FLUXION_RHI_VULKAN_MAX_BUFFERS, &index, &generation)) return invalid;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc->size;
    bufferInfo.usage = Fluxion_RHIVulkan_MapBufferUsage(desc->usageFlags);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = Fluxion_RHIVulkan_MapMemoryClass(desc->memoryClass);
    allocInfo.flags = Fluxion_RHIVulkan_MapMemoryClassFlags(desc->memoryClass);

    FluxionRHIVulkanBuffer* buffer = &s_buffers[index];
    *buffer = FluxionRHIVulkanBuffer{};
    buffer->size = desc->size;
    VmaAllocationInfo allocationInfo = {};
    if (vmaCreateBuffer(deviceState->allocator, &bufferInfo, &allocInfo, &buffer->buffer, &buffer->allocation, &allocationInfo) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_bufferSlots, FLUXION_RHI_VULKAN_MAX_BUFFERS, index, generation);
        return invalid;
    }
    buffer->mappedPointer = allocationInfo.pMappedData; // non-null only for a CPU-visible memory class

    Fluxion_RHIVulkan_SetObjectName(deviceState->device, VK_OBJECT_TYPE_BUFFER, (u64)buffer->buffer, desc->debugName);

    FluxionRHIBufferHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHIVulkan_FinalizeBuffer(u32 index)
{
    // The owning device isn't tracked per-buffer (this backend only ever
    // has one active allocator per live device, and a buffer never
    // outlives its device), so the allocator itself is looked up from
    // whichever device is still alive -- in practice DestroyDevice already
    // flushes every retirement before tearing the allocator down, so this
    // path only runs against a still-valid allocator.
    extern FluxionRHIVulkanDevice* Fluxion_RHIVulkan_ResolveDevice(FluxionRHIDeviceHandle);
    // Buffers/textures don't carry an owning-device back-reference (this
    // backend only ever has one active device) -- vmaDestroyBuffer only
    // needs the allocation's own VmaAllocator, which VMA tracks
    // internally per-allocation, so a null allocator here is fine to
    // skip.
    FluxionRHIVulkanBuffer* buffer = &s_buffers[index];
    if (buffer->buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(Fluxion_RHIVulkan_GetOwningAllocator(), buffer->buffer, buffer->allocation);
    }
    *buffer = FluxionRHIVulkanBuffer{};
    Fluxion_RHIVulkan_PoolFinalize(s_bufferSlots, index);
}

void Fluxion_RHIVulkan_DestroyBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_bufferSlots, FLUXION_RHI_VULKAN_MAX_BUFFERS, buffer.index, buffer.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyBuffer called with an invalid or already-destroyed handle");
        return;
    }
    // PoolRetire (not PoolFree): the real vmaDestroyBuffer is deferred to
    // FinalizeBuffer, so this slot must stay out of PoolAllocate's reuse
    // pool until that actually runs, or a same-frame Create could reuse
    // this exact index before the old buffer is really freed (see
    // FluxionRHIVulkanSlot::pendingFinalize's comment in VulkanCommon.h).
    Fluxion_RHIVulkan_PoolRetire(s_bufferSlots, FLUXION_RHI_VULKAN_MAX_BUFFERS, buffer.index, buffer.generation);
    extern FluxionRHIVulkanDevice* Fluxion_RHIVulkan_SoleDevice(void);
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHIVulkan_Retire(deviceState, FluxionRHIVulkanRetiredEntry::Kind::Buffer, buffer.index, deviceState->gcCounter);
    else
        Fluxion_RHIVulkan_FinalizeBuffer(buffer.index);
}

FluxionRHIVulkanBuffer* Fluxion_RHIVulkan_ResolveBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_bufferSlots, FLUXION_RHI_VULKAN_MAX_BUFFERS, buffer.index, buffer.generation)) return nullptr;
    return &s_buffers[buffer.index];
}

void* Fluxion_RHIVulkan_MapBuffer(FluxionRHIBufferHandle buffer)
{
    FluxionRHIVulkanBuffer* bufferState = Fluxion_RHIVulkan_ResolveBuffer(buffer);
    // VMA_ALLOCATION_CREATE_MAPPED_BIT (set for every CPU-visible memory
    // class in Fluxion_RHIVulkan_MapMemoryClassFlags) keeps the pointer
    // persistently valid for the buffer's whole lifetime -- Map is just a
    // lookup, no vkMapMemory call needed here.
    return bufferState != nullptr ? bufferState->mappedPointer : nullptr;
}

void Fluxion_RHIVulkan_UnmapBuffer(FluxionRHIBufferHandle buffer)
{
    FLUXION_UNUSED(buffer); // persistently mapped -- nothing to unmap
}

// --- Textures -------------------------------------------------------------

static VkImageUsageFlags Fluxion_RHIVulkan_MapTextureUsage(u32 usageFlags)
{
    VkImageUsageFlags vkUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usageFlags & FLUXION_RHI_TEXTURE_USAGE_SAMPLED) vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usageFlags & FLUXION_RHI_TEXTURE_USAGE_STORAGE) vkUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (usageFlags & FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET) vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usageFlags & FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL) vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return vkUsage;
}

FluxionRHITextureHandle Fluxion_RHIVulkan_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc)
{
    FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_textureSlots, FLUXION_RHI_VULKAN_MAX_TEXTURES, &index, &generation)) return invalid;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = desc->depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    imageInfo.format = Fluxion_RHIVulkan_MapFormat(desc->format);
    imageInfo.extent = { desc->width, desc->height, desc->depth };
    imageInfo.mipLevels = desc->mipLevels > 0 ? desc->mipLevels : 1;
    imageInfo.arrayLayers = desc->arrayLayers > 0 ? desc->arrayLayers : 1;
    imageInfo.samples = (VkSampleCountFlagBits)(desc->sampleCount > 0 ? desc->sampleCount : 1);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = Fluxion_RHIVulkan_MapTextureUsage(desc->usageFlags);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // A cube is six square layers that a view may read as one thing
    // sampled by direction. The image itself is an ordinary array; the
    // flag is what makes such a view legal, and it has to be asked for at
    // creation because an image cannot be given it afterwards.
    //
    // Refused rather than corrected when the shape does not fit: a
    // description that says CUBE and brings four layers is a mistake
    // somewhere else, and quietly making an array out of it would move
    // the failure to whichever shader sampled it.
    if (desc->dimension == FLUXION_RHI_TEXTURE_DIMENSION_CUBE)
    {
        if (imageInfo.arrayLayers != FLUXION_RHI_CUBE_FACE_COUNT || desc->width != desc->height)
        {
            FLUXION_LOG_ERROR("RHI.Vulkan",
                "a cube texture needs six square layers; this one has %u layers at %ux%u",
                imageInfo.arrayLayers, desc->width, desc->height);
            Fluxion_RHIVulkan_PoolFree(s_textureSlots, FLUXION_RHI_VULKAN_MAX_TEXTURES, index, generation);
            return invalid;
        }
        imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = Fluxion_RHIVulkan_MapMemoryClass(desc->memoryClass);

    FluxionRHIVulkanTexture* texture = &s_textures[index];
    *texture = FluxionRHIVulkanTexture{};
    texture->format = imageInfo.format;
    texture->width = desc->width;
    texture->height = desc->height;
    texture->depth = imageInfo.extent.depth;
    texture->mipLevels = imageInfo.mipLevels;
    texture->arrayLayers = imageInfo.arrayLayers;

    if (vmaCreateImage(deviceState->allocator, &imageInfo, &allocInfo, &texture->image, &texture->allocation, nullptr) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_textureSlots, FLUXION_RHI_VULKAN_MAX_TEXTURES, index, generation);
        return invalid;
    }

    Fluxion_RHIVulkan_SetObjectName(deviceState->device, VK_OBJECT_TYPE_IMAGE, (u64)texture->image, desc->debugName);

    FluxionRHITextureHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

bool Fluxion_RHIVulkan_AllocateTextureSlot(FluxionRHITextureHandle* outHandle, u32* outIndex)
{
    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_textureSlots, FLUXION_RHI_VULKAN_MAX_TEXTURES, &index, &generation)) return false;
    s_textures[index] = FluxionRHIVulkanTexture{};
    outHandle->index = index;
    outHandle->generation = generation;
    if (outIndex != nullptr) *outIndex = index;
    return true;
}

void Fluxion_RHIVulkan_FreeTextureSlotDirect(FluxionRHITextureHandle handle)
{
    // Used only by the swapchain: it owns its VkImages outright (no
    // vmaDestroyImage -- vkDestroySwapchainKHR frees them), so this frees
    // just the pool slot, bypassing the normal retirement path.
    Fluxion_RHIVulkan_PoolFree(s_textureSlots, FLUXION_RHI_VULKAN_MAX_TEXTURES, handle.index, handle.generation);
}

static void Fluxion_RHIVulkan_FinalizeTexture(u32 index)
{
    FluxionRHIVulkanTexture* texture = &s_textures[index];
    if (texture->image != VK_NULL_HANDLE && !texture->ownedBySwapchain)
    {
        vmaDestroyImage(Fluxion_RHIVulkan_GetOwningAllocator(), texture->image, texture->allocation);
    }
    *texture = FluxionRHIVulkanTexture{};
    Fluxion_RHIVulkan_PoolFinalize(s_textureSlots, index);
}

void Fluxion_RHIVulkan_DestroyTexture(FluxionRHITextureHandle texture)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_textureSlots, FLUXION_RHI_VULKAN_MAX_TEXTURES, texture.index, texture.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyTexture called with an invalid or already-destroyed handle");
        return;
    }
    // See DestroyBuffer's comment -- PoolRetire, not PoolFree.
    Fluxion_RHIVulkan_PoolRetire(s_textureSlots, FLUXION_RHI_VULKAN_MAX_TEXTURES, texture.index, texture.generation);
    extern FluxionRHIVulkanDevice* Fluxion_RHIVulkan_SoleDevice(void);
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHIVulkan_Retire(deviceState, FluxionRHIVulkanRetiredEntry::Kind::Texture, texture.index, deviceState->gcCounter);
    else
        Fluxion_RHIVulkan_FinalizeTexture(texture.index);
}

FluxionRHIVulkanTexture* Fluxion_RHIVulkan_ResolveTexture(FluxionRHITextureHandle texture)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_textureSlots, FLUXION_RHI_VULKAN_MAX_TEXTURES, texture.index, texture.generation)) return nullptr;
    return &s_textures[texture.index];
}

// --- Texture views ----------------------------------------------------------

FluxionRHITextureViewHandle Fluxion_RHIVulkan_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc)
{
    FluxionRHITextureViewHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    FluxionRHIVulkanTexture* texture = desc != nullptr ? Fluxion_RHIVulkan_ResolveTexture(desc->texture) : nullptr;
    if (deviceState == nullptr || texture == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_viewSlots, FLUXION_RHI_VULKAN_MAX_TEXTURE_VIEWS, &index, &generation)) return invalid;

    VkFormat format = desc->format != FLUXION_RHI_FORMAT_UNKNOWN ? Fluxion_RHIVulkan_MapFormat(desc->format) : texture->format;
    bool isDepth = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT;

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->image;
    // What the SHADER sees, which need not be what the image is: the same
    // six layers are a cube to one view and an ordinary array to another,
    // and rendering into a single face wants the second.
    viewInfo.viewType = desc->dimension == FLUXION_RHI_TEXTURE_DIMENSION_CUBE
                            ? VK_IMAGE_VIEW_TYPE_CUBE
                            : (texture->arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = desc->baseMipLevel;
    viewInfo.subresourceRange.levelCount = desc->mipLevelCount > 0 ? desc->mipLevelCount : 1;
    viewInfo.subresourceRange.baseArrayLayer = desc->baseArrayLayer;
    viewInfo.subresourceRange.layerCount = desc->arrayLayerCount > 0 ? desc->arrayLayerCount : 1;

    FluxionRHIVulkanTextureView* view = &s_views[index];
    view->texture = desc->texture;
    if (vkCreateImageView(deviceState->device, &viewInfo, nullptr, &view->view) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_viewSlots, FLUXION_RHI_VULKAN_MAX_TEXTURE_VIEWS, index, generation);
        return invalid;
    }

    FluxionRHITextureViewHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHIVulkan_FinalizeTextureView(u32 index)
{
    FluxionRHIVulkanTextureView* view = &s_views[index];
    if (view->view != VK_NULL_HANDLE)
    {
        extern VkDevice Fluxion_RHIVulkan_GetOwningDevice(void);
        vkDestroyImageView(Fluxion_RHIVulkan_GetOwningDevice(), view->view, nullptr);
    }
    *view = FluxionRHIVulkanTextureView{};
    Fluxion_RHIVulkan_PoolFinalize(s_viewSlots, index);
}

void Fluxion_RHIVulkan_DestroyTextureView(FluxionRHITextureViewHandle view)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_viewSlots, FLUXION_RHI_VULKAN_MAX_TEXTURE_VIEWS, view.index, view.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyTextureView called with an invalid or already-destroyed handle");
        return;
    }
    // See DestroyBuffer's comment -- PoolRetire, not PoolFree.
    Fluxion_RHIVulkan_PoolRetire(s_viewSlots, FLUXION_RHI_VULKAN_MAX_TEXTURE_VIEWS, view.index, view.generation);
    extern FluxionRHIVulkanDevice* Fluxion_RHIVulkan_SoleDevice(void);
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHIVulkan_Retire(deviceState, FluxionRHIVulkanRetiredEntry::Kind::TextureView, view.index, deviceState->gcCounter);
    else
        Fluxion_RHIVulkan_FinalizeTextureView(view.index);
}

FluxionRHIVulkanTextureView* Fluxion_RHIVulkan_ResolveTextureView(FluxionRHITextureViewHandle view)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_viewSlots, FLUXION_RHI_VULKAN_MAX_TEXTURE_VIEWS, view.index, view.generation)) return nullptr;
    return &s_views[view.index];
}

// --- Samplers ---------------------------------------------------------------

static VkFilter Fluxion_RHIVulkan_MapFilter(FluxionRHIFilter filter) { return filter == FLUXION_RHI_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; }
// The same mapping the depth state uses, written out again here rather
// than shared: the pipeline's copy lives in another translation unit,
// and the two enums are the same enum -- a shared helper would be one
// more header for eight cases.
static VkCompareOp Fluxion_RHIVulkan_MapSamplerCompareOp(FluxionRHICompareOp op)
{
    switch (op)
    {
        case FLUXION_RHI_COMPARE_OP_NEVER: return VK_COMPARE_OP_NEVER;
        case FLUXION_RHI_COMPARE_OP_EQUAL: return VK_COMPARE_OP_EQUAL;
        case FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER: return VK_COMPARE_OP_GREATER;
        case FLUXION_RHI_COMPARE_OP_NOT_EQUAL: return VK_COMPARE_OP_NOT_EQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER_OR_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case FLUXION_RHI_COMPARE_OP_ALWAYS: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_LESS;
    }
}

static VkSamplerAddressMode Fluxion_RHIVulkan_MapAddressMode(FluxionRHIAddressMode mode)
{
    switch (mode)
    {
        case FLUXION_RHI_ADDRESS_MODE_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

FluxionRHISamplerHandle Fluxion_RHIVulkan_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc)
{
    FluxionRHISamplerHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_samplerSlots, FLUXION_RHI_VULKAN_MAX_SAMPLERS, &index, &generation)) return invalid;

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = Fluxion_RHIVulkan_MapFilter(desc->magFilter);
    samplerInfo.minFilter = Fluxion_RHIVulkan_MapFilter(desc->minFilter);
    samplerInfo.mipmapMode = desc->mipFilter == FLUXION_RHI_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = Fluxion_RHIVulkan_MapAddressMode(desc->addressModeU);
    samplerInfo.addressModeV = Fluxion_RHIVulkan_MapAddressMode(desc->addressModeV);
    samplerInfo.addressModeW = Fluxion_RHIVulkan_MapAddressMode(desc->addressModeW);
    samplerInfo.anisotropyEnable = desc->maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc->maxAnisotropy;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.compareEnable = desc->compareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = Fluxion_RHIVulkan_MapSamplerCompareOp(desc->compareOp);

    if (vkCreateSampler(deviceState->device, &samplerInfo, nullptr, &s_samplers[index]) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_samplerSlots, FLUXION_RHI_VULKAN_MAX_SAMPLERS, index, generation);
        return invalid;
    }

    Fluxion_RHIVulkan_SetObjectName(deviceState->device, VK_OBJECT_TYPE_SAMPLER, (u64)s_samplers[index], desc->debugName);

    FluxionRHISamplerHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHIVulkan_FinalizeSampler(u32 index)
{
    if (s_samplers[index] != VK_NULL_HANDLE)
    {
        extern VkDevice Fluxion_RHIVulkan_GetOwningDevice(void);
        vkDestroySampler(Fluxion_RHIVulkan_GetOwningDevice(), s_samplers[index], nullptr);
        s_samplers[index] = VK_NULL_HANDLE;
    }
    Fluxion_RHIVulkan_PoolFinalize(s_samplerSlots, index);
}

void Fluxion_RHIVulkan_DestroySampler(FluxionRHISamplerHandle sampler)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_samplerSlots, FLUXION_RHI_VULKAN_MAX_SAMPLERS, sampler.index, sampler.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroySampler called with an invalid or already-destroyed handle");
        return;
    }
    // See DestroyBuffer's comment -- PoolRetire, not PoolFree.
    Fluxion_RHIVulkan_PoolRetire(s_samplerSlots, FLUXION_RHI_VULKAN_MAX_SAMPLERS, sampler.index, sampler.generation);
    extern FluxionRHIVulkanDevice* Fluxion_RHIVulkan_SoleDevice(void);
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHIVulkan_Retire(deviceState, FluxionRHIVulkanRetiredEntry::Kind::Sampler, sampler.index, deviceState->gcCounter);
    else
        Fluxion_RHIVulkan_FinalizeSampler(sampler.index);
}

VkSampler Fluxion_RHIVulkan_ResolveSampler(FluxionRHISamplerHandle sampler)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_samplerSlots, FLUXION_RHI_VULKAN_MAX_SAMPLERS, sampler.index, sampler.generation)) return VK_NULL_HANDLE;
    return s_samplers[sampler.index];
}

// --- Retirement dispatch ------------------------------------------------

extern void Fluxion_RHIVulkan_FinalizeShaderSlot(u32 index);
extern void Fluxion_RHIVulkan_FinalizePipelineSlot(u32 index);

void Fluxion_RHIVulkan_FinalizeRetired(FluxionRHIVulkanDevice* deviceState, FluxionRHIVulkanRetiredEntry::Kind kind, u32 index)
{
    FLUXION_UNUSED(deviceState);
    switch (kind)
    {
        case FluxionRHIVulkanRetiredEntry::Kind::Buffer: Fluxion_RHIVulkan_FinalizeBuffer(index); break;
        case FluxionRHIVulkanRetiredEntry::Kind::Texture: Fluxion_RHIVulkan_FinalizeTexture(index); break;
        case FluxionRHIVulkanRetiredEntry::Kind::TextureView: Fluxion_RHIVulkan_FinalizeTextureView(index); break;
        case FluxionRHIVulkanRetiredEntry::Kind::Sampler: Fluxion_RHIVulkan_FinalizeSampler(index); break;
        case FluxionRHIVulkanRetiredEntry::Kind::Shader: Fluxion_RHIVulkan_FinalizeShaderSlot(index); break;
        case FluxionRHIVulkanRetiredEntry::Kind::Pipeline: Fluxion_RHIVulkan_FinalizePipelineSlot(index); break;
        case FluxionRHIVulkanRetiredEntry::Kind::BindGroup: Fluxion_RHIVulkan_FinalizeBindGroupSlot(index); break;
    }
}
