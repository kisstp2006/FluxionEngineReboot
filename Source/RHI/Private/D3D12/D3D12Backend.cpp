// D3D12 RHI backend entry point: instance/adapter enumeration, device +
// queue creation, the device-shared descriptor heaps, and the vtable
// that dispatches every other Fluxion_RHI_* call into the rest of
// Private/D3D12/*.cpp. Graphics/compute/transfer all alias the same
// ID3D12CommandQueue in this backend (v1 never requests dedicated
// compute/copy queues -- same "simplicity wins in v1" choice the OpenGL
// backend already makes for its single implicit context).

#include "D3D12Common.h"

#include <cstring>

// --- Generic slot pool -------------------------------------------------------

bool Fluxion_RHID3D12_PoolAllocate(FluxionRHID3D12Slot* slots, u32 capacity, u32* outIndex, u32* outGeneration)
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

bool Fluxion_RHID3D12_PoolIsValid(const FluxionRHID3D12Slot* slots, u32 capacity, u32 index, u32 generation)
{
    return index < capacity && slots[index].alive && slots[index].generation == generation;
}

void Fluxion_RHID3D12_PoolFree(FluxionRHID3D12Slot* slots, u32 capacity, u32 index, u32 generation)
{
    if (!Fluxion_RHID3D12_PoolIsValid(slots, capacity, index, generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: destroy called with an invalid or already-destroyed handle");
        return;
    }
    slots[index].alive = false;
    ++slots[index].generation;
}

// --- Descriptor heap free-list allocator ------------------------------------

bool Fluxion_RHID3D12_HeapAllocatorInit(FluxionRHID3D12HeapAllocator* allocator, ID3D12DescriptorHeap* heap, u32 capacity, u32 descriptorSize)
{
    allocator->heap = heap;
    allocator->capacity = capacity;
    allocator->descriptorSize = descriptorSize;
    allocator->freeRanges[0] = { 0, capacity };
    allocator->freeRangeCount = 1;
    return true;
}

bool Fluxion_RHID3D12_HeapAllocatorAllocate(FluxionRHID3D12HeapAllocator* allocator, u32 count, u32* outOffset)
{
    if (count == 0) { *outOffset = 0; return true; }
    for (u32 i = 0; i < allocator->freeRangeCount; ++i)
    {
        FluxionRHID3D12HeapRange& range = allocator->freeRanges[i];
        if (range.count < count) continue;
        *outOffset = range.offset;
        range.offset += count;
        range.count -= count;
        if (range.count == 0)
        {
            for (u32 j = i; j + 1 < allocator->freeRangeCount; ++j) allocator->freeRanges[j] = allocator->freeRanges[j + 1];
            --allocator->freeRangeCount;
        }
        return true;
    }
    return false;
}

void Fluxion_RHID3D12_HeapAllocatorFree(FluxionRHID3D12HeapAllocator* allocator, u32 offset, u32 count)
{
    if (count == 0) return;
    // No coalescing with neighboring free ranges -- a v1 simplification
    // (fragmentation is an accepted cost, see the struct's own comment in
    // D3D12Common.h); still correct, just doesn't reclaim adjacency.
    if (allocator->freeRangeCount < 64)
        allocator->freeRanges[allocator->freeRangeCount++] = { offset, count };
}

D3D12_CPU_DESCRIPTOR_HANDLE Fluxion_RHID3D12_HeapCpuHandle(const FluxionRHID3D12HeapAllocator* allocator, u32 offset)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = allocator->heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (SIZE_T)offset * allocator->descriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Fluxion_RHID3D12_HeapGpuHandle(const FluxionRHID3D12HeapAllocator* allocator, ID3D12DescriptorHeap* gpuHeap, u32 offset)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = gpuHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += (UINT64)offset * allocator->descriptorSize;
    return handle;
}

// --- Instance / adapters -----------------------------------------------------

static ComPtr<IDXGIFactory6> s_factory;
static ComPtr<ID3D12Debug> s_debugController;

static ComPtr<IDXGIAdapter1> s_adapters[FLUXION_RHI_D3D12_MAX_ADAPTERS];
static u32 s_adapterCount = 0;

// --- Devices -----------------------------------------------------------------

static FluxionRHID3D12Slot s_deviceSlots[FLUXION_RHI_D3D12_MAX_DEVICES];
static FluxionRHID3D12Device s_devices[FLUXION_RHI_D3D12_MAX_DEVICES];
static FluxionRHID3D12Device* s_activeDevice = nullptr;

IDXGIFactory6* Fluxion_RHID3D12_GetFactory(void) { return s_factory.Get(); }

FluxionRHID3D12Device* Fluxion_RHID3D12_SoleDevice(void) { return s_activeDevice; }
ID3D12Device* Fluxion_RHID3D12_GetOwningDevice(void) { return s_activeDevice != nullptr ? s_activeDevice->device.Get() : nullptr; }
D3D12MA::Allocator* Fluxion_RHID3D12_GetOwningAllocator(void) { return s_activeDevice != nullptr ? s_activeDevice->allocator.Get() : nullptr; }

IDXGIAdapter1* Fluxion_RHID3D12_ResolveAdapter(FluxionRHIAdapterHandle adapter)
{
    if (adapter.generation != 0 || adapter.index >= s_adapterCount) return nullptr;
    return s_adapters[adapter.index].Get();
}

FluxionRHID3D12Device* Fluxion_RHID3D12_ResolveDevice(FluxionRHIDeviceHandle device)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_deviceSlots, FLUXION_RHI_D3D12_MAX_DEVICES, device.index, device.generation)) return nullptr;
    return &s_devices[device.index];
}

u64 Fluxion_RHID3D12_NextGCValue(FluxionRHID3D12Device* deviceState)
{
    return ++deviceState->gcCounter;
}

void Fluxion_RHID3D12_Retire(FluxionRHID3D12Device* deviceState, FluxionRHID3D12RetiredEntry::Kind kind, u32 index, u64 retireAfterValue)
{
    if (deviceState->retiredCount >= FLUXION_RHI_D3D12_MAX_RETIRED)
    {
        // The retirement list is full -- flush now rather than leak the
        // slot, same fallback as the Vulkan backend's identical case.
        deviceState->graphicsQueue->Signal(deviceState->gcFence.Get(), deviceState->gcCounter);
        deviceState->gcFence->SetEventOnCompletion(deviceState->gcCounter, deviceState->gcEvent);
        WaitForSingleObject(deviceState->gcEvent, INFINITE);
        deviceState->retiredCount = 0;
    }
    deviceState->retired[deviceState->retiredCount].retireAfterValue = retireAfterValue;
    deviceState->retired[deviceState->retiredCount].kind = kind;
    deviceState->retired[deviceState->retiredCount].index = index;
    ++deviceState->retiredCount;
}

// --- Adapter info / capabilities ---------------------------------------------

static FluxionRHICapabilityFlags Fluxion_RHID3D12_QueryCapabilities(ID3D12Device* device)
{
    FluxionRHICapabilityFlags caps = FLUXION_RHI_CAPABILITY_TIMELINE_SYNC | FLUXION_RHI_CAPABILITY_COMPUTE_SHADERS;

    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        if (options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2)
            caps |= FLUXION_RHI_CAPABILITY_DESCRIPTOR_INDEXING;
        if (options.TypedUAVLoadAdditionalFormats)
            caps |= FLUXION_RHI_CAPABILITY_SPARSE_RESOURCES; // approximate mapping, no closer portable bit exists
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1))) && options1.WaveOps)
        caps |= FLUXION_RHI_CAPABILITY_WAVE_OPERATIONS;

    return caps;
}

static bool Fluxion_RHID3D12_IsFakeAdapterInvalid(FluxionRHIAdapterHandle adapter)
{
    return adapter.generation != 0 || adapter.index >= s_adapterCount;
}

static bool Fluxion_RHID3D12_GetAdapterInfo(FluxionRHIAdapterHandle adapter, FluxionRHIAdapterInfo* outInfo)
{
    if (Fluxion_RHID3D12_IsFakeAdapterInvalid(adapter) || outInfo == nullptr) return false;

    std::memset(outInfo, 0, sizeof(*outInfo));

    DXGI_ADAPTER_DESC1 desc = {};
    s_adapters[adapter.index]->GetDesc1(&desc);

    int written = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, outInfo->name, FLUXION_RHI_ADAPTER_MAX_NAME_LENGTH, nullptr, nullptr);
    if (written <= 0) outInfo->name[0] = '\0';

    outInfo->vendorId = desc.VendorId;
    outInfo->deviceId = desc.DeviceId;
    outInfo->type = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? FLUXION_RHI_ADAPTER_TYPE_CPU : FLUXION_RHI_ADAPTER_TYPE_DISCRETE;
    outInfo->dedicatedVRAM = desc.DedicatedVideoMemory;
    outInfo->sharedMemory = desc.SharedSystemMemory;
    outInfo->driverVersion = 0; // no single portable-sized integer exposes this via DXGI; left unset like the OpenGL backend's driver-string field
    outInfo->apiVersion = 0;

    // Capabilities/limits require a live ID3D12Device to query
    // (D3D12_FEATURE_* is device-scoped, unlike Vulkan's
    // vkGetPhysicalDeviceFeatures2, which works against just the
    // VkPhysicalDevice) -- report the fixed baseline until a device
    // actually exists on this adapter, same "adapter reports what's
    // reasonably knowable before CreateDevice" approach as the OpenGL
    // backend's placeholder adapter info.
    if (s_activeDevice != nullptr && s_activeDevice->adapter.Get() == s_adapters[adapter.index].Get())
    {
        outInfo->capabilities = Fluxion_RHID3D12_QueryCapabilities(s_activeDevice->device.Get());
    }

    outInfo->limits.maxTextureDimension1D = D3D12_REQ_TEXTURE1D_U_DIMENSION;
    outInfo->limits.maxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    outInfo->limits.maxTextureDimension3D = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    outInfo->limits.maxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
    outInfo->limits.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    outInfo->limits.maxBoundDescriptorSets = FLUXION_RHI_MAX_BIND_GROUPS;
    outInfo->limits.maxPushConstantSize = D3D12_MAX_ROOT_COST * 4; // root constants are 1 DWORD each, in bytes
    outInfo->limits.minUniformBufferOffsetAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    outInfo->limits.maxComputeWorkGroupSize[0] = D3D12_CS_THREAD_GROUP_MAX_X;
    outInfo->limits.maxComputeWorkGroupSize[1] = D3D12_CS_THREAD_GROUP_MAX_Y;
    outInfo->limits.maxComputeWorkGroupSize[2] = D3D12_CS_THREAD_GROUP_MAX_Z;
    outInfo->limits.maxComputeWorkGroupInvocations = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
    return true;
}

static u32 Fluxion_RHID3D12_EnumerateAdapters(FluxionRHIInstanceHandle instance, FluxionRHIAdapterHandle* outAdapters, u32 maxAdapters)
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

static FluxionRHIDeviceHandle Fluxion_RHID3D12_CreateDevice(FluxionRHIAdapterHandle adapter, const FluxionRHIDeviceDesc* desc)
{
    FluxionRHIDeviceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHID3D12_IsFakeAdapterInvalid(adapter)) return invalid;
    IDXGIAdapter1* nativeAdapter = s_adapters[adapter.index].Get();

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_deviceSlots, FLUXION_RHI_D3D12_MAX_DEVICES, &index, &generation)) return invalid;

    FluxionRHID3D12Device* deviceState = &s_devices[index];
    *deviceState = FluxionRHID3D12Device{};
    deviceState->adapter = nativeAdapter;

    if (FAILED(D3D12CreateDevice(nativeAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&deviceState->device))))
    {
        Fluxion_RHID3D12_PoolFree(s_deviceSlots, FLUXION_RHI_D3D12_MAX_DEVICES, index, generation);
        return invalid;
    }

    FluxionRHICapabilityFlags available = Fluxion_RHID3D12_QueryCapabilities(deviceState->device.Get());
    if (desc != nullptr && (available & desc->requiredCapabilities) != desc->requiredCapabilities)
    {
        Fluxion_RHID3D12_PoolFree(s_deviceSlots, FLUXION_RHI_D3D12_MAX_DEVICES, index, generation);
        return invalid;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(deviceState->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&deviceState->graphicsQueue))))
    {
        Fluxion_RHID3D12_PoolFree(s_deviceSlots, FLUXION_RHI_D3D12_MAX_DEVICES, index, generation);
        return invalid;
    }
    // v1 aliases compute/transfer onto the same direct queue as graphics
    // (matches the OpenGL backend's DedicatedComputeQueue=false story,
    // and this engine's own documented queue-aliasing rule in RHI.h) --
    // a dedicated D3D12_COMMAND_LIST_TYPE_COMPUTE/COPY queue is future work.
    deviceState->computeQueue = deviceState->graphicsQueue;
    deviceState->transferQueue = deviceState->graphicsQueue;

    D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
    allocatorDesc.pDevice = deviceState->device.Get();
    allocatorDesc.pAdapter = nativeAdapter;
    if (FAILED(D3D12MA::CreateAllocator(&allocatorDesc, &deviceState->allocator)))
    {
        Fluxion_RHID3D12_PoolFree(s_deviceSlots, FLUXION_RHI_D3D12_MAX_DEVICES, index, generation);
        return invalid;
    }

    if (FAILED(deviceState->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&deviceState->gcFence))))
    {
        Fluxion_RHID3D12_PoolFree(s_deviceSlots, FLUXION_RHI_D3D12_MAX_DEVICES, index, generation);
        return invalid;
    }
    deviceState->gcEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
    cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvUavHeapDesc.NumDescriptors = FLUXION_RHI_D3D12_CBV_SRV_UAV_HEAP_SIZE;
    cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    deviceState->device->CreateDescriptorHeap(&cbvSrvUavHeapDesc, IID_PPV_ARGS(&deviceState->cbvSrvUavHeap));
    Fluxion_RHID3D12_HeapAllocatorInit(&deviceState->cbvSrvUavAllocator, deviceState->cbvSrvUavHeap.Get(), FLUXION_RHI_D3D12_CBV_SRV_UAV_HEAP_SIZE,
        deviceState->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.NumDescriptors = FLUXION_RHI_D3D12_SAMPLER_HEAP_SIZE;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    deviceState->device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&deviceState->samplerHeap));
    Fluxion_RHID3D12_HeapAllocatorInit(&deviceState->samplerAllocator, deviceState->samplerHeap.Get(), FLUXION_RHI_D3D12_SAMPLER_HEAP_SIZE,
        deviceState->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS;
    deviceState->device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&deviceState->rtvHeap));
    deviceState->rtvDescriptorSize = deviceState->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS;
    deviceState->device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&deviceState->dsvHeap));
    deviceState->dsvDescriptorSize = deviceState->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    s_activeDevice = deviceState; // only one device is ever active at a time (see D3D12Common.h)

    FluxionRHIDeviceHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHID3D12_DestroyDevice(FluxionRHIDeviceHandle device)
{
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr) return;

    if (deviceState->graphicsQueue && deviceState->gcFence)
    {
        u64 finalValue = ++deviceState->gcCounter;
        deviceState->graphicsQueue->Signal(deviceState->gcFence.Get(), finalValue);
        deviceState->gcFence->SetEventOnCompletion(finalValue, deviceState->gcEvent);
        WaitForSingleObject(deviceState->gcEvent, INFINITE);
    }
    Fluxion_RHID3D12_CollectGarbage(device); // flush everything now that the device is idle

    if (deviceState->gcEvent != nullptr) CloseHandle(deviceState->gcEvent);

    if (s_activeDevice == deviceState) s_activeDevice = nullptr;
    *deviceState = FluxionRHID3D12Device{};
    Fluxion_RHID3D12_PoolFree(s_deviceSlots, FLUXION_RHI_D3D12_MAX_DEVICES, device.index, device.generation);
}

static FluxionRHIQueueHandle Fluxion_RHID3D12_GetQueue(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHIQueueHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHID3D12_ResolveDevice(device) == nullptr) return invalid;
    // Derived deterministically from the device (same pattern as
    // VulkanBackend.cpp/OpenGLBackend.cpp) -- the actual ID3D12CommandQueue
    // is fetched from the device state each time a queue-consuming call
    // resolves this handle, not stored per-handle.
    FluxionRHIQueueHandle queue;
    queue.index = device.index * 3u + (u32)type;
    queue.generation = device.generation;
    return queue;
}

ID3D12CommandQueue* Fluxion_RHID3D12_ResolveQueue(FluxionRHIQueueHandle queue)
{
    u32 deviceIndex = queue.index / 3u;
    FluxionRHIQueueType type = (FluxionRHIQueueType)(queue.index % 3u);
    FluxionRHIDeviceHandle device = { deviceIndex, queue.generation };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr) return nullptr;
    switch (type)
    {
        case FLUXION_RHI_QUEUE_TYPE_GRAPHICS: return deviceState->graphicsQueue.Get();
        case FLUXION_RHI_QUEUE_TYPE_COMPUTE: return deviceState->computeQueue.Get();
        case FLUXION_RHI_QUEUE_TYPE_TRANSFER: return deviceState->transferQueue.Get();
    }
    return nullptr;
}

FluxionRHID3D12Device* Fluxion_RHID3D12_ResolveDeviceFromQueue(FluxionRHIQueueHandle queue)
{
    u32 deviceIndex = queue.index / 3u;
    FluxionRHIDeviceHandle device = { deviceIndex, queue.generation };
    return Fluxion_RHID3D12_ResolveDevice(device);
}

// --- Garbage collection ------------------------------------------------------

void Fluxion_RHID3D12_CollectGarbage(FluxionRHIDeviceHandle device)
{
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr) return;

    u64 completed = deviceState->gcFence->GetCompletedValue();

    u32 writeIndex = 0;
    for (u32 i = 0; i < deviceState->retiredCount; ++i)
    {
        FluxionRHID3D12RetiredEntry entry = deviceState->retired[i];
        if (entry.retireAfterValue > completed)
        {
            deviceState->retired[writeIndex++] = entry;
            continue;
        }
        extern void Fluxion_RHID3D12_FinalizeRetired(FluxionRHID3D12Device* deviceState, FluxionRHID3D12RetiredEntry::Kind kind, u32 index);
        Fluxion_RHID3D12_FinalizeRetired(deviceState, entry.kind, entry.index);
    }
    deviceState->retiredCount = writeIndex;
}

// --- Vtable / entry point ----------------------------------------------------

extern void Fluxion_RHID3D12_CommandListBegin(FluxionRHICommandListHandle);
extern void Fluxion_RHID3D12_CommandListEnd(FluxionRHICommandListHandle);
extern void Fluxion_RHID3D12_CommandListBeginRendering(FluxionRHICommandListHandle, const FluxionRHIRenderingDesc*);
extern void Fluxion_RHID3D12_CommandListEndRendering(FluxionRHICommandListHandle);
extern void Fluxion_RHID3D12_CommandListSetPipeline(FluxionRHICommandListHandle, FluxionRHIPipelineHandle);
extern void Fluxion_RHID3D12_CommandListSetVertexBuffer(FluxionRHICommandListHandle, u32, FluxionRHIBufferHandle, usize);
extern void Fluxion_RHID3D12_CommandListSetIndexBuffer(FluxionRHICommandListHandle, FluxionRHIBufferHandle, usize, bool);
extern void Fluxion_RHID3D12_CommandListDraw(FluxionRHICommandListHandle, u32, u32, u32, u32);
extern void Fluxion_RHID3D12_CommandListDrawIndexed(FluxionRHICommandListHandle, u32, u32, u32, i32, u32);
extern void Fluxion_RHID3D12_CommandListDrawIndirect(FluxionRHICommandListHandle, FluxionRHIBufferHandle, usize, u32, u32);
extern void Fluxion_RHID3D12_CommandListDispatch(FluxionRHICommandListHandle, u32, u32, u32);
extern void Fluxion_RHID3D12_CommandListCopyBuffer(FluxionRHICommandListHandle, FluxionRHIBufferHandle, usize, FluxionRHIBufferHandle, usize, usize);
extern void Fluxion_RHID3D12_CommandListCopyTexture(FluxionRHICommandListHandle, FluxionRHITextureHandle, FluxionRHITextureHandle);
extern void Fluxion_RHID3D12_CommandListBarrier(FluxionRHICommandListHandle, const FluxionRHIBarrier*, u32);
extern void Fluxion_RHID3D12_CommandListSetBindGroup(FluxionRHICommandListHandle, u32, FluxionRHIBindGroupHandle);
extern void Fluxion_RHID3D12_QueueSubmit(FluxionRHIQueueHandle, const FluxionRHICommandListHandle*, u32, FluxionRHIFenceHandle);

// --- Native handle escape hatch ----------------------------------------------

FluxionRHINativeHandle Fluxion_RHID3D12_GetNativeDeviceHandle(FluxionRHIDeviceHandle device)
{
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    FluxionRHINativeHandle handle = { deviceState != nullptr ? (void*)deviceState->device.Get() : nullptr };
    return handle;
}

FluxionRHINativeHandle Fluxion_RHID3D12_GetNativeBufferHandle(FluxionRHIBufferHandle buffer)
{
    FluxionRHID3D12Buffer* bufferState = Fluxion_RHID3D12_ResolveBuffer(buffer);
    FluxionRHINativeHandle handle = { bufferState != nullptr ? (void*)bufferState->resource.Get() : nullptr };
    return handle;
}

FluxionRHINativeHandle Fluxion_RHID3D12_GetNativeTextureHandle(FluxionRHITextureHandle texture)
{
    FluxionRHID3D12Texture* textureState = Fluxion_RHID3D12_ResolveTexture(texture);
    FluxionRHINativeHandle handle = { textureState != nullptr ? (void*)textureState->resource.Get() : nullptr };
    return handle;
}

static void Fluxion_RHID3D12_DestroyInstance(FluxionRHIInstanceHandle instance)
{
    FLUXION_UNUSED(instance);
    for (u32 i = 0; i < FLUXION_RHI_D3D12_MAX_DEVICES; ++i)
    {
        if (s_deviceSlots[i].alive)
        {
            FluxionRHIDeviceHandle handle = { i, s_deviceSlots[i].generation };
            Fluxion_RHID3D12_DestroyDevice(handle);
        }
    }
    for (u32 i = 0; i < s_adapterCount; ++i) s_adapters[i].Reset();
    s_adapterCount = 0;
    s_debugController.Reset();
    s_factory.Reset();
}

static const FluxionRHIBackendVTable s_d3d12VTable = {
    Fluxion_RHID3D12_DestroyInstance,
    Fluxion_RHID3D12_EnumerateAdapters,
    Fluxion_RHID3D12_GetAdapterInfo,

    Fluxion_RHID3D12_CreateDevice,
    Fluxion_RHID3D12_DestroyDevice,
    Fluxion_RHID3D12_CollectGarbage,
    Fluxion_RHID3D12_GetQueue,

    Fluxion_RHID3D12_CreateCommandList,
    Fluxion_RHID3D12_DestroyCommandList,
    Fluxion_RHID3D12_CommandListBegin,
    Fluxion_RHID3D12_CommandListEnd,
    Fluxion_RHID3D12_CommandListBeginRendering,
    Fluxion_RHID3D12_CommandListEndRendering,
    Fluxion_RHID3D12_CommandListSetPipeline,
    Fluxion_RHID3D12_CommandListSetVertexBuffer,
    Fluxion_RHID3D12_CommandListSetIndexBuffer,
    Fluxion_RHID3D12_CommandListDraw,
    Fluxion_RHID3D12_CommandListDrawIndexed,
    Fluxion_RHID3D12_CommandListDrawIndirect,
    Fluxion_RHID3D12_CommandListDispatch,
    Fluxion_RHID3D12_CommandListCopyBuffer,
    Fluxion_RHID3D12_CommandListCopyTexture,
    Fluxion_RHID3D12_CommandListCopyBufferToTexture,
    Fluxion_RHID3D12_CommandListBarrier,
    Fluxion_RHID3D12_CommandListSetBindGroup,

    Fluxion_RHID3D12_QueueSubmit,

    Fluxion_RHID3D12_CreateBuffer,
    Fluxion_RHID3D12_DestroyBuffer,
    Fluxion_RHID3D12_MapBuffer,
    Fluxion_RHID3D12_UnmapBuffer,
    Fluxion_RHID3D12_CreateTexture,
    Fluxion_RHID3D12_DestroyTexture,
    Fluxion_RHID3D12_CreateTextureView,
    Fluxion_RHID3D12_DestroyTextureView,
    Fluxion_RHID3D12_CreateSampler,
    Fluxion_RHID3D12_DestroySampler,

    Fluxion_RHID3D12_CreateBindGroupLayout,
    Fluxion_RHID3D12_DestroyBindGroupLayout,
    Fluxion_RHID3D12_CreateBindGroup,
    Fluxion_RHID3D12_DestroyBindGroup,

    Fluxion_RHID3D12_CreateShader,
    Fluxion_RHID3D12_DestroyShader,
    Fluxion_RHID3D12_CreateGraphicsPipeline,
    Fluxion_RHID3D12_CreateComputePipeline,
    Fluxion_RHID3D12_DestroyPipeline,
    Fluxion_RHID3D12_SavePipelineCacheToFile,
    Fluxion_RHID3D12_LoadPipelineCacheFromFile,

    Fluxion_RHID3D12_CreateSwapchain,
    Fluxion_RHID3D12_DestroySwapchain,
    Fluxion_RHID3D12_SwapchainAcquireNextImage,
    Fluxion_RHID3D12_SwapchainGetTexture,
    Fluxion_RHID3D12_SwapchainPresent,
    Fluxion_RHID3D12_SwapchainGetExtent,

    Fluxion_RHID3D12_CreateFence,
    Fluxion_RHID3D12_DestroyFence,
    Fluxion_RHID3D12_WaitForFence,
    Fluxion_RHID3D12_ResetFence,
    Fluxion_RHID3D12_CreateSemaphore,
    Fluxion_RHID3D12_DestroySemaphore,
    Fluxion_RHID3D12_CreateQueryPool,
    Fluxion_RHID3D12_DestroyQueryPool,

    Fluxion_RHID3D12_GetNativeDeviceHandle,
    Fluxion_RHID3D12_GetNativeBufferHandle,
    Fluxion_RHID3D12_GetNativeTextureHandle,
};

extern "C" const FluxionRHIBackendVTable* Fluxion_RHI_D3D12_CreateInstance(const FluxionRHIInstanceDesc* desc, FluxionRHIInstanceHandle* outInstance)
{
    UINT factoryFlags = 0;
    if (desc != nullptr && desc->enableValidation)
    {
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&s_debugController))))
        {
            s_debugController->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }

    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&s_factory)))) return nullptr;

    s_adapterCount = 0;
    for (UINT i = 0; i < FLUXION_RHI_D3D12_MAX_ADAPTERS; ++i)
    {
        ComPtr<IDXGIAdapter1> candidate;
        if (s_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;

        DXGI_ADAPTER_DESC1 desc1 = {};
        candidate->GetDesc1(&desc1);
        if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue; // skip the WARP software adapter, same as the Vulkan/OpenGL backends only reporting real hardware
        if (FAILED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) continue; // adapter can't actually create a 12_0 device

        s_adapters[s_adapterCount++] = candidate;
    }

    for (u32 i = 0; i < FLUXION_RHI_D3D12_MAX_DEVICES; ++i) s_deviceSlots[i] = {};

    outInstance->index = 0;
    outInstance->generation = 0;
    return &s_d3d12VTable;
}
