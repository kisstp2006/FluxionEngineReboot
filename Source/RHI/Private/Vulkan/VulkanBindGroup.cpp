// BindGroupLayout/BindGroup: this backend's mapping of the portable
// binding model onto VkDescriptorSetLayout/VkDescriptorSet. Layouts are
// deduplicated by shape (a device-scoped cache keyed by a hash of the
// entry list), so two callers describing an identical Material-group
// shape share one native layout. BindGroups are allocated
// out of one shared, per-device VkDescriptorPool
// (FREE_DESCRIPTOR_SET_BIT) and destroyed individually through the same
// retirement-queue mechanism every other GPU resource in this backend
// uses -- a deliberately simpler v1 than the frame-scoped bump-allocator
// pattern, since BindGroups here are expected to be created per-material/
// per-object rather than churned every frame.

#include "VulkanCommon.h"

#include <cstring>

// --- Layout cache ------------------------------------------------------------

struct FluxionRHIVulkanBindGroupLayout
{
    VkDescriptorSetLayout nativeLayout = VK_NULL_HANDLE;
    FluxionRHIBindGroupLayoutEntryDesc entries[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    u32 entryCount = 0;
    bool bindless = false;
    u32 refCount = 0;
    u64 shapeHash = 0;
};

static FluxionRHIVulkanSlot s_layoutSlots[FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS];
static FluxionRHIVulkanBindGroupLayout s_layouts[FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS];

struct FluxionRHIVulkanBindGroup
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    FluxionRHIBindGroupLayoutHandle layout{};
};

static FluxionRHIVulkanSlot s_bindGroupSlots[FLUXION_RHI_VULKAN_MAX_BIND_GROUPS];
static FluxionRHIVulkanBindGroup s_bindGroups[FLUXION_RHI_VULKAN_MAX_BIND_GROUPS];

static VkDescriptorType Fluxion_RHIVulkan_MapBindingType(FluxionRHIBindingType type)
{
    switch (type)
    {
        case FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case FLUXION_RHI_BINDING_TYPE_SAMPLER: return VK_DESCRIPTOR_TYPE_SAMPLER;
        default: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

static VkShaderStageFlags Fluxion_RHIVulkan_MapShaderStageFlags(FluxionRHIShaderStageFlags flags)
{
    VkShaderStageFlags result = 0;
    if (flags & FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX) result |= VK_SHADER_STAGE_VERTEX_BIT;
    if (flags & FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT) result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (flags & FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE) result |= VK_SHADER_STAGE_COMPUTE_BIT;
    return result;
}

// FNV-1a over the entry list + bindless flag -- collisions would only
// ever cause two genuinely different shapes to wrongly share a layout,
// so this only needs to be good enough in practice, not cryptographic.
static u64 Fluxion_RHIVulkan_HashBindGroupLayoutDesc(const FluxionRHIBindGroupLayoutDesc* desc)
{
    u64 hash = 1469598103934665603ull;
    auto mix = [&hash](u64 value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix((u64)desc->bindless);
    for (u32 i = 0; i < desc->entryCount; ++i)
    {
        mix(desc->entries[i].binding);
        mix((u64)desc->entries[i].type);
        mix(desc->entries[i].visibility);
        mix(desc->entries[i].arrayCount);
    }
    return hash;
}

static bool Fluxion_RHIVulkan_DeviceSupportsDescriptorIndexing(VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing = {};
    descriptorIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &descriptorIndexing;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
    return descriptorIndexing.shaderSampledImageArrayNonUniformIndexing && descriptorIndexing.descriptorBindingPartiallyBound &&
        descriptorIndexing.descriptorBindingVariableDescriptorCount && descriptorIndexing.runtimeDescriptorArray;
}

static bool Fluxion_RHIVulkan_EnsureBindGroupPool(FluxionRHIVulkanDevice* deviceState)
{
    if (deviceState->bindGroupPool != VK_NULL_HANDLE) return true;

    VkDescriptorPoolSize poolSizes[4] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FLUXION_RHI_VULKAN_MAX_BIND_GROUPS * 4u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FLUXION_RHI_VULKAN_MAX_BIND_GROUPS * 2u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096u }, // generous: also backs bindless variable-count arrays
        { VK_DESCRIPTOR_TYPE_SAMPLER, FLUXION_RHI_VULKAN_MAX_BIND_GROUPS * 2u },
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (Fluxion_RHIVulkan_DeviceSupportsDescriptorIndexing(deviceState->physicalDevice))
        poolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = FLUXION_RHI_VULKAN_MAX_BIND_GROUPS;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;

    return vkCreateDescriptorPool(deviceState->device, &poolInfo, nullptr, &deviceState->bindGroupPool) == VK_SUCCESS;
}

FluxionRHIBindGroupLayoutHandle Fluxion_RHIVulkan_CreateBindGroupLayout(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc)
{
    FluxionRHIBindGroupLayoutHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr || desc->entryCount > FLUXION_RHI_MAX_BIND_GROUP_ENTRIES) return invalid;

    bool wantBindless = desc->bindless && Fluxion_RHIVulkan_DeviceSupportsDescriptorIndexing(deviceState->physicalDevice);
    u64 shapeHash = Fluxion_RHIVulkan_HashBindGroupLayoutDesc(desc);

    // Dedup against every already-live layout with an identical shape --
    // note wantBindless (not desc->bindless) participates in the hash
    // comparison below, so a "bindless requested but unsupported" layout
    // correctly dedupes against an ordinary one of the same shape.
    for (u32 i = 0; i < FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS; ++i)
    {
        if (!s_layoutSlots[i].alive) continue;
        FluxionRHIVulkanBindGroupLayout* candidate = &s_layouts[i];
        if (candidate->shapeHash != shapeHash || candidate->bindless != wantBindless || candidate->entryCount != desc->entryCount) continue;
        bool same = true;
        for (u32 e = 0; e < desc->entryCount; ++e)
        {
            const auto& a = candidate->entries[e];
            const auto& b = desc->entries[e];
            if (a.binding != b.binding || a.type != b.type || a.visibility != b.visibility || a.arrayCount != b.arrayCount) { same = false; break; }
        }
        if (!same) continue;

        ++candidate->refCount;
        FluxionRHIBindGroupLayoutHandle handle;
        handle.index = i;
        handle.generation = s_layoutSlots[i].generation;
        return handle;
    }

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_layoutSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS, &index, &generation)) return invalid;

    VkDescriptorSetLayoutBinding bindings[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    VkDescriptorBindingFlags bindingFlags[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    for (u32 i = 0; i < desc->entryCount; ++i)
    {
        bindings[i] = {};
        bindings[i].binding = desc->entries[i].binding;
        bindings[i].descriptorType = Fluxion_RHIVulkan_MapBindingType(desc->entries[i].type);
        bindings[i].descriptorCount = desc->entries[i].arrayCount > 0 ? desc->entries[i].arrayCount : 1;
        bindings[i].stageFlags = Fluxion_RHIVulkan_MapShaderStageFlags(desc->entries[i].visibility);
        bindingFlags[i] = 0;
        if (wantBindless && desc->entries[i].arrayCount > 0)
            bindingFlags[i] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = desc->entryCount;
    bindingFlagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = desc->entryCount;
    layoutInfo.pBindings = bindings;
    if (wantBindless)
    {
        layoutInfo.pNext = &bindingFlagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }

    FluxionRHIVulkanBindGroupLayout* layout = &s_layouts[index];
    *layout = FluxionRHIVulkanBindGroupLayout{};
    if (vkCreateDescriptorSetLayout(deviceState->device, &layoutInfo, nullptr, &layout->nativeLayout) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_layoutSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS, index, generation);
        return invalid;
    }

    layout->entryCount = desc->entryCount;
    for (u32 i = 0; i < desc->entryCount; ++i) layout->entries[i] = desc->entries[i];
    layout->bindless = wantBindless;
    layout->refCount = 1;
    layout->shapeHash = shapeHash;

    FluxionRHIBindGroupLayoutHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_DestroyBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_layoutSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS, layout.index, layout.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyBindGroupLayout called with an invalid or already-destroyed handle");
        return;
    }
    FluxionRHIVulkanBindGroupLayout* layoutState = &s_layouts[layout.index];
    FLUXION_ASSERT_MSG(layoutState->refCount > 0, "Fluxion RHI Vulkan backend: BindGroupLayout refcount underflow");
    if (--layoutState->refCount > 0) return;

    // Not GC-deferred: a caller must not destroy a layout while a
    // pipeline is still being created from it, but existing pipelines
    // and BindGroups remain valid Vulkan objects after this per spec, so
    // there is no in-flight-GPU-read hazard to defer against here (unlike
    // Buffer/Texture/Pipeline, which the GPU itself reads).
    if (layoutState->nativeLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Fluxion_RHIVulkan_GetOwningDevice(), layoutState->nativeLayout, nullptr);
    *layoutState = FluxionRHIVulkanBindGroupLayout{};
    Fluxion_RHIVulkan_PoolFree(s_layoutSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS, layout.index, layout.generation);
}

VkDescriptorSetLayout Fluxion_RHIVulkan_ResolveBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_layoutSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS, layout.index, layout.generation)) return VK_NULL_HANDLE;
    return s_layouts[layout.index].nativeLayout;
}

VkDescriptorSetLayout Fluxion_RHIVulkan_GetOrCreateEmptyBindGroupLayout(FluxionRHIVulkanDevice* deviceState)
{
    if (deviceState->emptyBindGroupLayout != VK_NULL_HANDLE) return deviceState->emptyBindGroupLayout;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 0;
    layoutInfo.pBindings = nullptr;
    vkCreateDescriptorSetLayout(deviceState->device, &layoutInfo, nullptr, &deviceState->emptyBindGroupLayout);
    return deviceState->emptyBindGroupLayout;
}

// --- BindGroups ----------------------------------------------------------

FluxionRHIBindGroupHandle Fluxion_RHIVulkan_CreateBindGroup(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc)
{
    FluxionRHIBindGroupHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;
    if (!Fluxion_RHIVulkan_PoolIsValid(s_layoutSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS, desc->layout.index, desc->layout.generation)) return invalid;
    if (!Fluxion_RHIVulkan_EnsureBindGroupPool(deviceState)) return invalid;

    FluxionRHIVulkanBindGroupLayout* layoutState = &s_layouts[desc->layout.index];

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_bindGroupSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUPS, &index, &generation)) return invalid;

    // A bindless layout's variable-count binding is sized to the widest
    // arrayCount the layout itself declares -- the concrete entries this
    // BindGroup actually fills in may cover fewer than that many slots
    // (VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT makes the unfilled ones
    // legal to leave unused).
    u32 variableCount = 0;
    for (u32 i = 0; i < layoutState->entryCount; ++i)
        if (layoutState->entries[i].arrayCount > variableCount) variableCount = layoutState->entries[i].arrayCount;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo = {};
    variableCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableCountInfo.descriptorSetCount = 1;
    variableCountInfo.pDescriptorCounts = &variableCount;

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = deviceState->bindGroupPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layoutState->nativeLayout;
    if (layoutState->bindless && variableCount > 0) allocInfo.pNext = &variableCountInfo;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(deviceState->device, &allocInfo, &set) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_bindGroupSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUPS, index, generation);
        return invalid;
    }

    VkWriteDescriptorSet writes[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    VkDescriptorBufferInfo bufferInfos[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    VkDescriptorImageInfo imageInfos[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    u32 writeCount = 0;
    u32 entryCount = desc->entryCount > FLUXION_RHI_MAX_BIND_GROUP_ENTRIES ? FLUXION_RHI_MAX_BIND_GROUP_ENTRIES : desc->entryCount;

    for (u32 i = 0; i < entryCount; ++i)
    {
        const FluxionRHIBindGroupEntry& entry = desc->entries[i];
        writes[writeCount] = {};
        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].dstSet = set;
        writes[writeCount].dstBinding = entry.binding;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = Fluxion_RHIVulkan_MapBindingType(entry.type);

        if (entry.type == FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER || entry.type == FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER)
        {
            FluxionRHIVulkanBuffer* bufferState = Fluxion_RHIVulkan_ResolveBuffer(entry.buffer);
            if (bufferState == nullptr) continue;
            bufferInfos[writeCount].buffer = bufferState->buffer;
            bufferInfos[writeCount].offset = entry.bufferOffset;
            bufferInfos[writeCount].range = entry.bufferSize > 0 ? entry.bufferSize : VK_WHOLE_SIZE;
            writes[writeCount].pBufferInfo = &bufferInfos[writeCount];
        }
        else if (entry.type == FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE)
        {
            FluxionRHIVulkanTextureView* viewState = Fluxion_RHIVulkan_ResolveTextureView(entry.textureView);
            if (viewState == nullptr) continue;
            imageInfos[writeCount] = {};
            imageInfos[writeCount].imageView = viewState->view;
            imageInfos[writeCount].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[writeCount].pImageInfo = &imageInfos[writeCount];
        }
        else // FLUXION_RHI_BINDING_TYPE_SAMPLER
        {
            VkSampler sampler = Fluxion_RHIVulkan_ResolveSampler(entry.sampler);
            if (sampler == VK_NULL_HANDLE) continue;
            imageInfos[writeCount] = {};
            imageInfos[writeCount].sampler = sampler;
            writes[writeCount].pImageInfo = &imageInfos[writeCount];
        }
        ++writeCount;
    }

    if (writeCount > 0) vkUpdateDescriptorSets(deviceState->device, writeCount, writes, 0, nullptr);

    FluxionRHIVulkanBindGroup* bindGroup = &s_bindGroups[index];
    bindGroup->set = set;
    bindGroup->layout = desc->layout;

    FluxionRHIBindGroupHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_FinalizeBindGroupSlot(u32 index)
{
    FluxionRHIVulkanBindGroup* bindGroup = &s_bindGroups[index];
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (bindGroup->set != VK_NULL_HANDLE && deviceState != nullptr && deviceState->bindGroupPool != VK_NULL_HANDLE)
        vkFreeDescriptorSets(deviceState->device, deviceState->bindGroupPool, 1, &bindGroup->set);
    *bindGroup = FluxionRHIVulkanBindGroup{};
    Fluxion_RHIVulkan_PoolFinalize(s_bindGroupSlots, index);
}

void Fluxion_RHIVulkan_DestroyBindGroup(FluxionRHIBindGroupHandle bindGroup)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_bindGroupSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUPS, bindGroup.index, bindGroup.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyBindGroup called with an invalid or already-destroyed handle");
        return;
    }
    // See VulkanMemory.cpp's DestroyBuffer comment -- PoolRetire, not PoolFree.
    Fluxion_RHIVulkan_PoolRetire(s_bindGroupSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUPS, bindGroup.index, bindGroup.generation);
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHIVulkan_Retire(deviceState, FluxionRHIVulkanRetiredEntry::Kind::BindGroup, bindGroup.index, deviceState->gcCounter);
    else
        Fluxion_RHIVulkan_FinalizeBindGroupSlot(bindGroup.index);
}

VkDescriptorSet Fluxion_RHIVulkan_ResolveBindGroup(FluxionRHIBindGroupHandle bindGroup)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_bindGroupSlots, FLUXION_RHI_VULKAN_MAX_BIND_GROUPS, bindGroup.index, bindGroup.generation)) return VK_NULL_HANDLE;
    return s_bindGroups[bindGroup.index].set;
}
