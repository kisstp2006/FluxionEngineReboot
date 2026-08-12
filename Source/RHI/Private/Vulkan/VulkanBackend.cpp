// Vulkan RHI backend entry point: instance/adapter enumeration, logical
// device + queue creation, and the vtable that dispatches every other
// Fluxion_RHI_* call into the rest of Private/Vulkan/*.cpp. Requires
// Vulkan 1.2 with the timelineSemaphore feature as a hard minimum and
// prefers Dynamic Rendering + Synchronization2 (1.3 core, or 1.2 + the
// matching KHR extensions) so no VkRenderPass/VkFramebuffer object exists
// anywhere in this backend.

#include "VulkanCommon.h"

#include <cstring>

// --- Generic slot pool -------------------------------------------------------

bool Fluxion_RHIVulkan_PoolAllocate(FluxionRHIVulkanSlot* slots, u32 capacity, u32* outIndex, u32* outGeneration)
{
    for (u32 i = 0; i < capacity; ++i)
    {
        if (!slots[i].alive)
        {
            slots[i].alive = true;
            *outIndex = i;
            *outGeneration = slots[i].generation;
            return true;
        }
    }
    return false;
}

bool Fluxion_RHIVulkan_PoolIsValid(const FluxionRHIVulkanSlot* slots, u32 capacity, u32 index, u32 generation)
{
    return index < capacity && slots[index].alive && slots[index].generation == generation;
}

void Fluxion_RHIVulkan_PoolFree(FluxionRHIVulkanSlot* slots, u32 capacity, u32 index, u32 generation)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(slots, capacity, index, generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: destroy called with an invalid or already-destroyed handle");
        return;
    }
    slots[index].alive = false;
    ++slots[index].generation;
}

// --- Instance / adapters -----------------------------------------------------

static VkInstance s_instance = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT s_debugMessenger = VK_NULL_HANDLE;

static VkPhysicalDevice s_adapters[FLUXION_RHI_VULKAN_MAX_ADAPTERS];
static u32 s_adapterCount = 0;

// --- Devices -----------------------------------------------------------------

static FluxionRHIVulkanSlot s_deviceSlots[FLUXION_RHI_VULKAN_MAX_DEVICES];
static FluxionRHIVulkanDevice s_devices[FLUXION_RHI_VULKAN_MAX_DEVICES];
static FluxionRHIVulkanDevice* s_activeDevice = nullptr;

VkInstance Fluxion_RHIVulkan_GetInstance(void) { return s_instance; }

FluxionRHIVulkanDevice* Fluxion_RHIVulkan_SoleDevice(void) { return s_activeDevice; }
VkDevice Fluxion_RHIVulkan_GetOwningDevice(void) { return s_activeDevice != nullptr ? s_activeDevice->device : VK_NULL_HANDLE; }
VmaAllocator Fluxion_RHIVulkan_GetOwningAllocator(void) { return s_activeDevice != nullptr ? s_activeDevice->allocator : VK_NULL_HANDLE; }

VkPhysicalDevice Fluxion_RHIVulkan_ResolvePhysicalDevice(FluxionRHIAdapterHandle adapter)
{
    if (adapter.generation != 0 || adapter.index >= s_adapterCount) return VK_NULL_HANDLE;
    return s_adapters[adapter.index];
}

FluxionRHIVulkanDevice* Fluxion_RHIVulkan_ResolveDevice(FluxionRHIDeviceHandle device)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_deviceSlots, FLUXION_RHI_VULKAN_MAX_DEVICES, device.index, device.generation)) return nullptr;
    return &s_devices[device.index];
}

u64 Fluxion_RHIVulkan_NextGCValue(FluxionRHIVulkanDevice* deviceState)
{
    return ++deviceState->gcCounter;
}

void Fluxion_RHIVulkan_Retire(FluxionRHIVulkanDevice* deviceState, FluxionRHIVulkanRetiredEntry::Kind kind, u32 index, u64 retireAfterValue)
{
    if (deviceState->retiredCount >= FLUXION_RHI_VULKAN_MAX_RETIRED)
    {
        // The retirement list is full -- fall back to collecting now
        // rather than leaking the slot; this stalls the caller, but only
        // in the pathological case of hundreds of pending destroys with
        // no CollectGarbage call in between.
        vkDeviceWaitIdle(deviceState->device);
        deviceState->retiredCount = 0;
    }
    deviceState->retired[deviceState->retiredCount].retireAfterValue = retireAfterValue;
    deviceState->retired[deviceState->retiredCount].kind = kind;
    deviceState->retired[deviceState->retiredCount].index = index;
    ++deviceState->retiredCount;
}

// --- Adapter info / capabilities ---------------------------------------------

static FluxionRHICapabilityFlags Fluxion_RHIVulkan_QueryCapabilities(VkPhysicalDevice physicalDevice)
{
    FluxionRHICapabilityFlags caps = FLUXION_RHI_CAPABILITY_TIMELINE_SYNC | FLUXION_RHI_CAPABILITY_COMPUTE_SHADERS;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing = {};
    descriptorIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress = {};
    bufferDeviceAddress.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferDeviceAddress.pNext = &descriptorIndexing;

    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &bufferDeviceAddress;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    if (descriptorIndexing.shaderSampledImageArrayNonUniformIndexing && descriptorIndexing.descriptorBindingPartiallyBound)
    {
        caps |= FLUXION_RHI_CAPABILITY_DESCRIPTOR_INDEXING | FLUXION_RHI_CAPABILITY_BINDLESS_RESOURCES;
    }
    if (bufferDeviceAddress.bufferDeviceAddress)
    {
        caps |= FLUXION_RHI_CAPABILITY_BUFFER_DEVICE_ADDRESS;
    }
    if (features2.features.tessellationShader) caps |= FLUXION_RHI_CAPABILITY_TESSELLATION;
    if (features2.features.geometryShader) caps |= FLUXION_RHI_CAPABILITY_GEOMETRY_SHADERS;
    if (features2.features.shaderInt64) caps |= FLUXION_RHI_CAPABILITY_INT64;
    if (features2.features.sparseBinding) caps |= FLUXION_RHI_CAPABILITY_SPARSE_RESOURCES;
    if (features2.features.shaderFloat64) caps |= 0; // no capability bit for this yet

    u32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    if (familyCount > 0)
    {
        VkQueueFamilyProperties families[32];
        familyCount = familyCount > 32 ? 32 : familyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families);
        bool hasGraphics = false, hasDedicatedCompute = false, hasDedicatedTransfer = false;
        for (u32 i = 0; i < familyCount; ++i)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) hasGraphics = true;
            if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) hasDedicatedCompute = true;
            if ((families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) hasDedicatedTransfer = true;
        }
        FLUXION_UNUSED(hasGraphics);
        if (hasDedicatedCompute) caps |= FLUXION_RHI_CAPABILITY_ASYNC_COMPUTE;
        if (hasDedicatedTransfer) caps |= FLUXION_RHI_CAPABILITY_ASYNC_TRANSFER;
    }

    return caps;
}

static bool Fluxion_RHIVulkan_GetAdapterInfo(FluxionRHIAdapterHandle adapter, FluxionRHIAdapterInfo* outInfo)
{
    VkPhysicalDevice physicalDevice = Fluxion_RHIVulkan_ResolvePhysicalDevice(adapter);
    if (physicalDevice == VK_NULL_HANDLE || outInfo == nullptr) return false;

    std::memset(outInfo, 0, sizeof(*outInfo));

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);

#if defined(_MSC_VER)
    strncpy_s(outInfo->name, sizeof(outInfo->name), props.deviceName, sizeof(outInfo->name) - 1);
#else
    std::strncpy(outInfo->name, props.deviceName, sizeof(outInfo->name) - 1);
#endif
    outInfo->vendorId = props.vendorID;
    outInfo->deviceId = props.deviceID;
    switch (props.deviceType)
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: outInfo->type = FLUXION_RHI_ADAPTER_TYPE_DISCRETE; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: outInfo->type = FLUXION_RHI_ADAPTER_TYPE_INTEGRATED; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: outInfo->type = FLUXION_RHI_ADAPTER_TYPE_VIRTUAL; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: outInfo->type = FLUXION_RHI_ADAPTER_TYPE_CPU; break;
        default: outInfo->type = FLUXION_RHI_ADAPTER_TYPE_OTHER; break;
    }
    outInfo->driverVersion = props.driverVersion;
    outInfo->apiVersion = props.apiVersion;

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (u32 i = 0; i < memProps.memoryHeapCount; ++i)
    {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            outInfo->dedicatedVRAM += memProps.memoryHeaps[i].size;
        else
            outInfo->sharedMemory += memProps.memoryHeaps[i].size;
    }

    outInfo->capabilities = Fluxion_RHIVulkan_QueryCapabilities(physicalDevice);

    outInfo->limits.maxTextureDimension1D = props.limits.maxImageDimension1D;
    outInfo->limits.maxTextureDimension2D = props.limits.maxImageDimension2D;
    outInfo->limits.maxTextureDimension3D = props.limits.maxImageDimension3D;
    outInfo->limits.maxTextureArrayLayers = props.limits.maxImageArrayLayers;
    outInfo->limits.maxColorAttachments = props.limits.maxColorAttachments;
    outInfo->limits.maxBoundDescriptorSets = props.limits.maxBoundDescriptorSets;
    outInfo->limits.maxPushConstantSize = props.limits.maxPushConstantsSize;
    outInfo->limits.minUniformBufferOffsetAlignment = (u32)props.limits.minUniformBufferOffsetAlignment;
    outInfo->limits.maxComputeWorkGroupSize[0] = props.limits.maxComputeWorkGroupSize[0];
    outInfo->limits.maxComputeWorkGroupSize[1] = props.limits.maxComputeWorkGroupSize[1];
    outInfo->limits.maxComputeWorkGroupSize[2] = props.limits.maxComputeWorkGroupSize[2];
    outInfo->limits.maxComputeWorkGroupInvocations = props.limits.maxComputeWorkGroupInvocations;
    return true;
}

static u32 Fluxion_RHIVulkan_EnumerateAdapters(FluxionRHIInstanceHandle instance, FluxionRHIAdapterHandle* outAdapters, u32 maxAdapters)
{
    FLUXION_UNUSED(instance);
    if (outAdapters == nullptr) return s_adapterCount;
    u32 count = maxAdapters < s_adapterCount ? maxAdapters : s_adapterCount;
    for (u32 i = 0; i < count; ++i)
    {
        outAdapters[i].index = i;
        outAdapters[i].generation = 0;
    }
    return count;
}

// --- Device / queue creation --------------------------------------------------

static bool Fluxion_RHIVulkan_SupportsVersion1_2Timeline(VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    if (props.apiVersion < VK_API_VERSION_1_2) return false;

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures = {};
    timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &timelineFeatures;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
    return timelineFeatures.timelineSemaphore == VK_TRUE;
}

// Dynamic Rendering + Synchronization2 -- 1.3 core, or 1.2 + both KHR
// extensions. Returns which extension names (if any, beyond the
// always-required ones) must be enabled at device-creation time.
static bool Fluxion_RHIVulkan_ResolveDynamicRenderingSupport(VkPhysicalDevice physicalDevice, bool* outNeedsDynamicRenderingExt, bool* outNeedsSync2Ext)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    *outNeedsDynamicRenderingExt = props.apiVersion < VK_API_VERSION_1_3;
    *outNeedsSync2Ext = props.apiVersion < VK_API_VERSION_1_3;

    VkPhysicalDeviceSynchronization2Features sync2Features = {};
    sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures = {};
    dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRenderingFeatures.pNext = &sync2Features;
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &dynamicRenderingFeatures;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    return dynamicRenderingFeatures.dynamicRendering == VK_TRUE && sync2Features.synchronization2 == VK_TRUE;
}

static void Fluxion_RHIVulkan_SelectQueueFamilies(VkPhysicalDevice physicalDevice, u32* outGraphics, u32* outCompute, u32* outTransfer, bool* outDedicatedCompute, bool* outDedicatedTransfer)
{
    u32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    VkQueueFamilyProperties families[32];
    familyCount = familyCount > 32 ? 32 : familyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families);

    u32 graphics = FLUXION_HANDLE_INVALID_INDEX, compute = FLUXION_HANDLE_INVALID_INDEX, transfer = FLUXION_HANDLE_INVALID_INDEX;
    *outDedicatedCompute = false;
    *outDedicatedTransfer = false;

    for (u32 i = 0; i < familyCount; ++i)
    {
        if (graphics == FLUXION_HANDLE_INVALID_INDEX && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) graphics = i;
    }
    // Prefer a queue family with compute but not graphics (a truly
    // dedicated async compute queue); fall back to the graphics family.
    for (u32 i = 0; i < familyCount; ++i)
    {
        if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            compute = i;
            *outDedicatedCompute = true;
            break;
        }
    }
    if (compute == FLUXION_HANDLE_INVALID_INDEX) compute = graphics;
    // Prefer a queue family with transfer but neither graphics nor
    // compute (a truly dedicated DMA-style transfer queue).
    for (u32 i = 0; i < familyCount; ++i)
    {
        if ((families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)))
        {
            transfer = i;
            *outDedicatedTransfer = true;
            break;
        }
    }
    if (transfer == FLUXION_HANDLE_INVALID_INDEX) transfer = graphics;

    *outGraphics = graphics;
    *outCompute = compute;
    *outTransfer = transfer;
}

static FluxionRHIDeviceHandle Fluxion_RHIVulkan_CreateDevice(FluxionRHIAdapterHandle adapter, const FluxionRHIDeviceDesc* desc)
{
    FluxionRHIDeviceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    VkPhysicalDevice physicalDevice = Fluxion_RHIVulkan_ResolvePhysicalDevice(adapter);
    if (physicalDevice == VK_NULL_HANDLE) return invalid;

    if (!Fluxion_RHIVulkan_SupportsVersion1_2Timeline(physicalDevice)) return invalid; // hard minimum for this backend

    FluxionRHICapabilityFlags available = Fluxion_RHIVulkan_QueryCapabilities(physicalDevice);
    if (desc != nullptr && (available & desc->requiredCapabilities) != desc->requiredCapabilities) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_deviceSlots, FLUXION_RHI_VULKAN_MAX_DEVICES, &index, &generation)) return invalid;

    FluxionRHIVulkanDevice* deviceState = &s_devices[index];
    *deviceState = FluxionRHIVulkanDevice{};
    deviceState->physicalDevice = physicalDevice;

    Fluxion_RHIVulkan_SelectQueueFamilies(physicalDevice, &deviceState->graphicsFamily, &deviceState->computeFamily, &deviceState->transferFamily,
        &deviceState->hasDedicatedCompute, &deviceState->hasDedicatedTransfer);

    bool needsDynamicRenderingExt = false, needsSync2Ext = false;
    bool hasDynamicRendering = Fluxion_RHIVulkan_ResolveDynamicRenderingSupport(physicalDevice, &needsDynamicRenderingExt, &needsSync2Ext);
    if (!hasDynamicRendering)
    {
        // Dynamic Rendering + Synchronization2 is required by this
        // backend (no VkRenderPass/VkFramebuffer path exists) -- devices
        // without it are rejected outright, same as the timeline
        // semaphore check above.
        Fluxion_RHIVulkan_PoolFree(s_deviceSlots, FLUXION_RHI_VULKAN_MAX_DEVICES, index, generation);
        return invalid;
    }

    f32 queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfos[3];
    u32 queueInfoCount = 0;
    u32 uniqueFamilies[3] = { deviceState->graphicsFamily, deviceState->computeFamily, deviceState->transferFamily };
    u32 uniqueCount = 0;
    for (u32 i = 0; i < 3; ++i)
    {
        bool seen = false;
        for (u32 j = 0; j < uniqueCount; ++j) if (uniqueFamilies[j] == uniqueFamilies[i]) seen = true;
        if (!seen) uniqueFamilies[uniqueCount++] = uniqueFamilies[i];
    }
    for (u32 i = 0; i < uniqueCount; ++i)
    {
        queueInfos[queueInfoCount] = {};
        queueInfos[queueInfoCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[queueInfoCount].queueFamilyIndex = uniqueFamilies[i];
        queueInfos[queueInfoCount].queueCount = 1;
        queueInfos[queueInfoCount].pQueuePriorities = &queuePriority;
        ++queueInfoCount;
    }

    const char* deviceExtensions[8];
    u32 deviceExtensionCount = 0;
    deviceExtensions[deviceExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (needsDynamicRenderingExt) deviceExtensions[deviceExtensionCount++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    if (needsSync2Ext) deviceExtensions[deviceExtensionCount++] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;

    VkPhysicalDeviceSynchronization2Features sync2Enable = {};
    sync2Enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2Enable.synchronization2 = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingEnable = {};
    dynamicRenderingEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRenderingEnable.pNext = &sync2Enable;
    dynamicRenderingEnable.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineEnable = {};
    timelineEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timelineEnable.pNext = &dynamicRenderingEnable;
    timelineEnable.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceFeatures2 featuresEnable = {};
    featuresEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    featuresEnable.pNext = &timelineEnable;

    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &featuresEnable;
    deviceInfo.queueCreateInfoCount = queueInfoCount;
    deviceInfo.pQueueCreateInfos = queueInfos;
    deviceInfo.enabledExtensionCount = deviceExtensionCount;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &deviceState->device) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_deviceSlots, FLUXION_RHI_VULKAN_MAX_DEVICES, index, generation);
        return invalid;
    }

    vkGetDeviceQueue(deviceState->device, deviceState->graphicsFamily, 0, &deviceState->graphicsQueue);
    vkGetDeviceQueue(deviceState->device, deviceState->computeFamily, 0, &deviceState->computeQueue);
    vkGetDeviceQueue(deviceState->device, deviceState->transferFamily, 0, &deviceState->transferQueue);

    // Resolve to the core-1.3 name when available, otherwise the
    // KHR-extension name that was actually enabled above.
    deviceState->cmdBeginRendering = needsDynamicRenderingExt
        ? (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(deviceState->device, "vkCmdBeginRenderingKHR")
        : (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(deviceState->device, "vkCmdBeginRendering");
    deviceState->cmdEndRendering = needsDynamicRenderingExt
        ? (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(deviceState->device, "vkCmdEndRenderingKHR")
        : (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(deviceState->device, "vkCmdEndRendering");
    deviceState->queueSubmit2 = needsSync2Ext
        ? (PFN_vkQueueSubmit2KHR)vkGetDeviceProcAddr(deviceState->device, "vkQueueSubmit2KHR")
        : (PFN_vkQueueSubmit2KHR)vkGetDeviceProcAddr(deviceState->device, "vkQueueSubmit2");
    deviceState->cmdPipelineBarrier2 = needsSync2Ext
        ? (PFN_vkCmdPipelineBarrier2KHR)vkGetDeviceProcAddr(deviceState->device, "vkCmdPipelineBarrier2KHR")
        : (PFN_vkCmdPipelineBarrier2KHR)vkGetDeviceProcAddr(deviceState->device, "vkCmdPipelineBarrier2");

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = deviceState->device;
    allocatorInfo.instance = s_instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    if (vmaCreateAllocator(&allocatorInfo, &deviceState->allocator) != VK_SUCCESS)
    {
        vkDestroyDevice(deviceState->device, nullptr);
        Fluxion_RHIVulkan_PoolFree(s_deviceSlots, FLUXION_RHI_VULKAN_MAX_DEVICES, index, generation);
        return invalid;
    }

    VkSemaphoreTypeCreateInfo timelineType = {};
    timelineType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineType.initialValue = 0;
    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &timelineType;
    vkCreateSemaphore(deviceState->device, &semInfo, nullptr, &deviceState->gcTimeline);

    s_activeDevice = deviceState; // only one device is ever active at a time (see VulkanCommon.h)

    FluxionRHIDeviceHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHIVulkan_DestroyDevice(FluxionRHIDeviceHandle device)
{
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr) return;

    vkDeviceWaitIdle(deviceState->device);
    Fluxion_RHIVulkan_CollectGarbage(device); // flush everything now that the device is idle

    if (deviceState->bindGroupPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(deviceState->device, deviceState->bindGroupPool, nullptr);
    if (deviceState->emptyBindGroupLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(deviceState->device, deviceState->emptyBindGroupLayout, nullptr);
    if (deviceState->pipelineCache != VK_NULL_HANDLE) vkDestroyPipelineCache(deviceState->device, deviceState->pipelineCache, nullptr);
    if (deviceState->gcTimeline != VK_NULL_HANDLE) vkDestroySemaphore(deviceState->device, deviceState->gcTimeline, nullptr);
    if (deviceState->allocator != VK_NULL_HANDLE) vmaDestroyAllocator(deviceState->allocator);
    if (deviceState->device != VK_NULL_HANDLE) vkDestroyDevice(deviceState->device, nullptr);

    if (s_activeDevice == deviceState) s_activeDevice = nullptr;
    Fluxion_RHIVulkan_PoolFree(s_deviceSlots, FLUXION_RHI_VULKAN_MAX_DEVICES, device.index, device.generation);
}

static FluxionRHIQueueHandle Fluxion_RHIVulkan_GetQueue(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHIQueueHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIVulkan_ResolveDevice(device) == nullptr) return invalid;
    // Derived deterministically from the device (same as NullBackend.c) --
    // the actual VkQueue is fetched from the device state each time a
    // queue-consuming call resolves this handle, not stored per-handle.
    FluxionRHIQueueHandle queue;
    queue.index = device.index * 3u + (u32)type;
    queue.generation = device.generation;
    return queue;
}

VkQueue Fluxion_RHIVulkan_ResolveQueue(FluxionRHIQueueHandle queue)
{
    u32 deviceIndex = queue.index / 3u;
    FluxionRHIQueueType type = (FluxionRHIQueueType)(queue.index % 3u);
    FluxionRHIDeviceHandle device = { deviceIndex, queue.generation };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr) return VK_NULL_HANDLE;
    switch (type)
    {
        case FLUXION_RHI_QUEUE_TYPE_GRAPHICS: return deviceState->graphicsQueue;
        case FLUXION_RHI_QUEUE_TYPE_COMPUTE: return deviceState->computeQueue;
        case FLUXION_RHI_QUEUE_TYPE_TRANSFER: return deviceState->transferQueue;
    }
    return VK_NULL_HANDLE;
}

FluxionRHIVulkanDevice* Fluxion_RHIVulkan_ResolveDeviceFromQueue(FluxionRHIQueueHandle queue)
{
    u32 deviceIndex = queue.index / 3u;
    FluxionRHIDeviceHandle device = { deviceIndex, queue.generation };
    return Fluxion_RHIVulkan_ResolveDevice(device);
}

VkQueue Fluxion_RHIVulkan_ResolvePresentQueue(FluxionRHIVulkanDevice* deviceState)
{
    return deviceState->graphicsQueue; // presentation always goes through the graphics queue in this backend
}

// --- Garbage collection ------------------------------------------------------

void Fluxion_RHIVulkan_CollectGarbage(FluxionRHIDeviceHandle device)
{
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr) return;

    u64 completed = 0;
    vkGetSemaphoreCounterValue(deviceState->device, deviceState->gcTimeline, &completed);

    u32 writeIndex = 0;
    for (u32 i = 0; i < deviceState->retiredCount; ++i)
    {
        FluxionRHIVulkanRetiredEntry entry = deviceState->retired[i];
        if (entry.retireAfterValue > completed)
        {
            deviceState->retired[writeIndex++] = entry;
            continue;
        }
        // The GPU has definitely finished with this resource -- free it
        // for real now, through the same pool the original Destroy* call
        // marked (not through this file, which doesn't own those pools).
        extern void Fluxion_RHIVulkan_FinalizeRetired(FluxionRHIVulkanDevice* deviceState, FluxionRHIVulkanRetiredEntry::Kind kind, u32 index);
        Fluxion_RHIVulkan_FinalizeRetired(deviceState, entry.kind, entry.index);
    }
    deviceState->retiredCount = writeIndex;
}

// --- Vtable / entry point ----------------------------------------------------

extern void Fluxion_RHIVulkan_CommandListBegin(FluxionRHICommandListHandle);
extern void Fluxion_RHIVulkan_CommandListEnd(FluxionRHICommandListHandle);
extern void Fluxion_RHIVulkan_CommandListBeginRendering(FluxionRHICommandListHandle, const FluxionRHIRenderingDesc*);
extern void Fluxion_RHIVulkan_CommandListEndRendering(FluxionRHICommandListHandle);
extern void Fluxion_RHIVulkan_CommandListSetPipeline(FluxionRHICommandListHandle, FluxionRHIPipelineHandle);
extern void Fluxion_RHIVulkan_CommandListSetVertexBuffer(FluxionRHICommandListHandle, u32, FluxionRHIBufferHandle, usize);
extern void Fluxion_RHIVulkan_CommandListSetIndexBuffer(FluxionRHICommandListHandle, FluxionRHIBufferHandle, usize, bool);
extern void Fluxion_RHIVulkan_CommandListDraw(FluxionRHICommandListHandle, u32, u32, u32, u32);
extern void Fluxion_RHIVulkan_CommandListDrawIndexed(FluxionRHICommandListHandle, u32, u32, u32, i32, u32);
extern void Fluxion_RHIVulkan_CommandListDrawIndirect(FluxionRHICommandListHandle, FluxionRHIBufferHandle, usize, u32, u32);
extern void Fluxion_RHIVulkan_CommandListDispatch(FluxionRHICommandListHandle, u32, u32, u32);
extern void Fluxion_RHIVulkan_CommandListCopyBuffer(FluxionRHICommandListHandle, FluxionRHIBufferHandle, usize, FluxionRHIBufferHandle, usize, usize);
extern void Fluxion_RHIVulkan_CommandListCopyTexture(FluxionRHICommandListHandle, FluxionRHITextureHandle, FluxionRHITextureHandle);
extern void Fluxion_RHIVulkan_CommandListCopyBufferToTexture(FluxionRHICommandListHandle, FluxionRHIBufferHandle, usize, FluxionRHITextureHandle, u32, u32);
extern void Fluxion_RHIVulkan_CommandListBarrier(FluxionRHICommandListHandle, const FluxionRHIBarrier*, u32);
extern void Fluxion_RHIVulkan_CommandListSetBindGroup(FluxionRHICommandListHandle, u32, FluxionRHIBindGroupHandle);
extern void Fluxion_RHIVulkan_QueueSubmit(FluxionRHIQueueHandle, const FluxionRHICommandListHandle*, u32, FluxionRHIFenceHandle);
// --- Native handle escape hatch (only the objects a caller has actually needed a native handle for so far) --

FluxionRHINativeHandle Fluxion_RHIVulkan_GetNativeDeviceHandle(FluxionRHIDeviceHandle device)
{
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    FluxionRHINativeHandle handle = { deviceState != nullptr ? (void*)deviceState->device : nullptr };
    return handle;
}

FluxionRHINativeHandle Fluxion_RHIVulkan_GetNativeBufferHandle(FluxionRHIBufferHandle buffer)
{
    FluxionRHIVulkanBuffer* bufferState = Fluxion_RHIVulkan_ResolveBuffer(buffer);
    FluxionRHINativeHandle handle = { bufferState != nullptr ? (void*)bufferState->buffer : nullptr };
    return handle;
}

FluxionRHINativeHandle Fluxion_RHIVulkan_GetNativeTextureHandle(FluxionRHITextureHandle texture)
{
    FluxionRHIVulkanTexture* textureState = Fluxion_RHIVulkan_ResolveTexture(texture);
    FluxionRHINativeHandle handle = { textureState != nullptr ? (void*)textureState->image : nullptr };
    return handle;
}

static void Fluxion_RHIVulkan_DestroyInstance(FluxionRHIInstanceHandle instance)
{
    FLUXION_UNUSED(instance);
    for (u32 i = 0; i < FLUXION_RHI_VULKAN_MAX_DEVICES; ++i)
    {
        if (s_deviceSlots[i].alive)
        {
            FluxionRHIDeviceHandle handle = { i, s_deviceSlots[i].generation };
            Fluxion_RHIVulkan_DestroyDevice(handle);
        }
    }
    if (s_debugMessenger != VK_NULL_HANDLE)
    {
        auto destroyFn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyFn != nullptr) destroyFn(s_instance, s_debugMessenger, nullptr);
        s_debugMessenger = VK_NULL_HANDLE;
    }
    if (s_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(s_instance, nullptr);
        s_instance = VK_NULL_HANDLE;
    }
    s_adapterCount = 0;
}

static const FluxionRHIBackendVTable s_vulkanVTable = {
    Fluxion_RHIVulkan_DestroyInstance,
    Fluxion_RHIVulkan_EnumerateAdapters,
    Fluxion_RHIVulkan_GetAdapterInfo,

    Fluxion_RHIVulkan_CreateDevice,
    Fluxion_RHIVulkan_DestroyDevice,
    Fluxion_RHIVulkan_CollectGarbage,
    Fluxion_RHIVulkan_GetQueue,

    Fluxion_RHIVulkan_CreateCommandList,
    Fluxion_RHIVulkan_DestroyCommandList,
    Fluxion_RHIVulkan_CommandListBegin,
    Fluxion_RHIVulkan_CommandListEnd,
    Fluxion_RHIVulkan_CommandListBeginRendering,
    Fluxion_RHIVulkan_CommandListEndRendering,
    Fluxion_RHIVulkan_CommandListSetPipeline,
    Fluxion_RHIVulkan_CommandListSetVertexBuffer,
    Fluxion_RHIVulkan_CommandListSetIndexBuffer,
    Fluxion_RHIVulkan_CommandListDraw,
    Fluxion_RHIVulkan_CommandListDrawIndexed,
    Fluxion_RHIVulkan_CommandListDrawIndirect,
    Fluxion_RHIVulkan_CommandListDispatch,
    Fluxion_RHIVulkan_CommandListCopyBuffer,
    Fluxion_RHIVulkan_CommandListCopyTexture,
    Fluxion_RHIVulkan_CommandListCopyBufferToTexture,
    Fluxion_RHIVulkan_CommandListBarrier,
    Fluxion_RHIVulkan_CommandListSetBindGroup,

    Fluxion_RHIVulkan_QueueSubmit,

    Fluxion_RHIVulkan_CreateBuffer,
    Fluxion_RHIVulkan_DestroyBuffer,
    Fluxion_RHIVulkan_MapBuffer,
    Fluxion_RHIVulkan_UnmapBuffer,
    Fluxion_RHIVulkan_CreateTexture,
    Fluxion_RHIVulkan_DestroyTexture,
    Fluxion_RHIVulkan_CreateTextureView,
    Fluxion_RHIVulkan_DestroyTextureView,
    Fluxion_RHIVulkan_CreateSampler,
    Fluxion_RHIVulkan_DestroySampler,

    Fluxion_RHIVulkan_CreateBindGroupLayout,
    Fluxion_RHIVulkan_DestroyBindGroupLayout,
    Fluxion_RHIVulkan_CreateBindGroup,
    Fluxion_RHIVulkan_DestroyBindGroup,

    Fluxion_RHIVulkan_CreateShader,
    Fluxion_RHIVulkan_DestroyShader,
    Fluxion_RHIVulkan_CreateGraphicsPipeline,
    Fluxion_RHIVulkan_CreateComputePipeline,
    Fluxion_RHIVulkan_DestroyPipeline,
    Fluxion_RHIVulkan_SavePipelineCacheToFile,
    Fluxion_RHIVulkan_LoadPipelineCacheFromFile,

    Fluxion_RHIVulkan_CreateSwapchain,
    Fluxion_RHIVulkan_DestroySwapchain,
    Fluxion_RHIVulkan_SwapchainAcquireNextImage,
    Fluxion_RHIVulkan_SwapchainGetTexture,
    Fluxion_RHIVulkan_SwapchainPresent,
    Fluxion_RHIVulkan_SwapchainGetExtent,

    Fluxion_RHIVulkan_CreateFence,
    Fluxion_RHIVulkan_DestroyFence,
    Fluxion_RHIVulkan_WaitForFence,
    Fluxion_RHIVulkan_ResetFence,
    Fluxion_RHIVulkan_CreateSemaphore,
    Fluxion_RHIVulkan_DestroySemaphore,
    Fluxion_RHIVulkan_CreateQueryPool,
    Fluxion_RHIVulkan_DestroyQueryPool,

    Fluxion_RHIVulkan_GetNativeDeviceHandle,
    Fluxion_RHIVulkan_GetNativeBufferHandle,
    Fluxion_RHIVulkan_GetNativeTextureHandle,
};

static VKAPI_ATTR VkBool32 VKAPI_CALL Fluxion_RHIVulkan_DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
{
    FLUXION_UNUSED(type);
    FLUXION_UNUSED(userData);
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        FLUXION_ASSERT_MSG(severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, callbackData->pMessage);
    }
    return VK_FALSE;
}

extern "C" const FluxionRHIBackendVTable* Fluxion_RHI_Vulkan_CreateInstance(const FluxionRHIInstanceDesc* desc, FluxionRHIInstanceHandle* outInstance)
{
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = (desc != nullptr && desc->applicationName != nullptr) ? desc->applicationName : "Fluxion Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Fluxion Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    bool wantValidation = desc != nullptr && desc->enableValidation;

    const char* instanceExtensions[8];
    u32 instanceExtensionCount = 0;
    instanceExtensions[instanceExtensionCount++] = VK_KHR_SURFACE_EXTENSION_NAME;
#if defined(_WIN32)
    instanceExtensions[instanceExtensionCount++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#else
    instanceExtensions[instanceExtensionCount++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif
    if (wantValidation) instanceExtensions[instanceExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";

    VkInstanceCreateInfo instanceInfo = {};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = instanceExtensionCount;
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    if (wantValidation)
    {
        instanceInfo.enabledLayerCount = 1;
        instanceInfo.ppEnabledLayerNames = &validationLayer;
    }

    if (vkCreateInstance(&instanceInfo, nullptr, &s_instance) != VK_SUCCESS)
    {
        // Retry once without validation -- a dev machine may not have the
        // Khronos validation layer installed even though the loader/ICD
        // itself is fine.
        if (wantValidation)
        {
            instanceInfo.enabledLayerCount = 0;
            instanceInfo.ppEnabledLayerNames = nullptr;
            if (vkCreateInstance(&instanceInfo, nullptr, &s_instance) != VK_SUCCESS) return nullptr;
            wantValidation = false;
        }
        else
        {
            return nullptr;
        }
    }

    if (wantValidation)
    {
        auto createFn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkCreateDebugUtilsMessengerEXT");
        if (createFn != nullptr)
        {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo = {};
            messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messengerInfo.pfnUserCallback = Fluxion_RHIVulkan_DebugCallback;
            createFn(s_instance, &messengerInfo, nullptr, &s_debugMessenger);
        }
    }

    u32 physicalDeviceCount = 0;
    vkEnumeratePhysicalDevices(s_instance, &physicalDeviceCount, nullptr);
    physicalDeviceCount = physicalDeviceCount > FLUXION_RHI_VULKAN_MAX_ADAPTERS ? FLUXION_RHI_VULKAN_MAX_ADAPTERS : physicalDeviceCount;
    vkEnumeratePhysicalDevices(s_instance, &physicalDeviceCount, s_adapters);
    s_adapterCount = physicalDeviceCount;

    for (u32 i = 0; i < FLUXION_RHI_VULKAN_MAX_DEVICES; ++i) s_deviceSlots[i] = {};

    outInstance->index = 0;
    outInstance->generation = 0;
    return &s_vulkanVTable;
}
