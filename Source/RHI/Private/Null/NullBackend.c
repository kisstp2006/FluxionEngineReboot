// The Null backend renders nothing -- its entire value is validating the
// RHI contract itself (handle lifetime, command-list recording order)
// with zero GPU/driver dependency, so RHITests can run everywhere,
// including CI with no graphics hardware. Deeper resource-state-mismatch
// validation is Milestone 28's job (RHI Validation Layer); this backend
// only covers handle validity, double-free/use-after-destroy, and
// command-list recording order.

#include "../RHIBackendVTable.h"

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

#define FLUXION_RHI_NULL_MAX_DEVICES 2
#define FLUXION_RHI_NULL_MAX_COMMAND_LISTS 64
#define FLUXION_RHI_NULL_MAX_BUFFERS 512
#define FLUXION_RHI_NULL_MAX_TEXTURES 512
#define FLUXION_RHI_NULL_MAX_TEXTURE_VIEWS 512
#define FLUXION_RHI_NULL_MAX_SAMPLERS 64
#define FLUXION_RHI_NULL_MAX_SHADERS 128
#define FLUXION_RHI_NULL_MAX_PIPELINES 128
#define FLUXION_RHI_NULL_MAX_SWAPCHAINS 4
#define FLUXION_RHI_NULL_MAX_SWAPCHAIN_IMAGES 4
#define FLUXION_RHI_NULL_MAX_FENCES 64
#define FLUXION_RHI_NULL_MAX_SEMAPHORES 64
#define FLUXION_RHI_NULL_MAX_QUERY_POOLS 16

// --- Generic slot pool -------------------------------------------------------
//
// Shared allocate/validate/free bookkeeping (alive flag + generation
// counter per slot) for every RHI object kind below -- the same pattern
// the Service/Subsystem Registry use for their own fixed-capacity tables.

typedef struct FluxionRHINullSlot
{
    bool alive;
    u32 generation;
} FluxionRHINullSlot;

static bool Fluxion_RHINull_Allocate(FluxionRHINullSlot* slots, u32 capacity, u32* outIndex, u32* outGeneration)
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

static bool Fluxion_RHINull_IsValid(const FluxionRHINullSlot* slots, u32 capacity, u32 index, u32 generation)
{
    return index < capacity && slots[index].alive && slots[index].generation == generation;
}

static void Fluxion_RHINull_Free(FluxionRHINullSlot* slots, u32 capacity, u32 index, u32 generation)
{
    if (!Fluxion_RHINull_IsValid(slots, capacity, index, generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: destroy called with an invalid or already-destroyed handle");
        return;
    }
    slots[index].alive = false;
    ++slots[index].generation;
}

// One pool + Allocate/IsValid/Free trio per object kind that needs no
// extra payload beyond "does this handle still refer to a live object" --
// Device/CommandList/Fence/Swapchain carry real state and are handled by
// hand further down instead of through this macro.
#define FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(HandleType, PoolVar, Capacity) \
    static FluxionRHINullSlot PoolVar[Capacity]; \
    static bool PoolVar##_Allocate(HandleType* outHandle) \
    { \
        u32 index, generation; \
        if (!Fluxion_RHINull_Allocate(PoolVar, (Capacity), &index, &generation)) return false; \
        outHandle->index = index; \
        outHandle->generation = generation; \
        return true; \
    } \
    static bool PoolVar##_IsValid(HandleType handle) \
    { \
        return Fluxion_RHINull_IsValid(PoolVar, (Capacity), handle.index, handle.generation); \
    } \
    static void PoolVar##_Free(HandleType handle) \
    { \
        Fluxion_RHINull_Free(PoolVar, (Capacity), handle.index, handle.generation); \
    }

FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(FluxionRHIBufferHandle, s_bufferPool, FLUXION_RHI_NULL_MAX_BUFFERS)
FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(FluxionRHITextureHandle, s_texturePool, FLUXION_RHI_NULL_MAX_TEXTURES)
FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(FluxionRHITextureViewHandle, s_textureViewPool, FLUXION_RHI_NULL_MAX_TEXTURE_VIEWS)
FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(FluxionRHISamplerHandle, s_samplerPool, FLUXION_RHI_NULL_MAX_SAMPLERS)
FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(FluxionRHIShaderHandle, s_shaderPool, FLUXION_RHI_NULL_MAX_SHADERS)
FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(FluxionRHIPipelineHandle, s_pipelinePool, FLUXION_RHI_NULL_MAX_PIPELINES)
FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(FluxionRHISemaphoreHandle, s_semaphorePool, FLUXION_RHI_NULL_MAX_SEMAPHORES)
FLUXION_RHI_NULL_DEFINE_SIMPLE_POOL(FluxionRHIQueryPoolHandle, s_queryPoolPool, FLUXION_RHI_NULL_MAX_QUERY_POOLS)

// --- Devices (no extra payload -- just an alive slot) -----------------------

static FluxionRHINullSlot s_deviceSlots[FLUXION_RHI_NULL_MAX_DEVICES];

// --- Command lists (real state machine: recording / inside-rendering) ------

typedef struct FluxionRHINullCommandListState
{
    FluxionRHIQueueType queueType;
    bool recording;
    bool insideRendering;
} FluxionRHINullCommandListState;

static FluxionRHINullSlot s_commandListSlots[FLUXION_RHI_NULL_MAX_COMMAND_LISTS];
static FluxionRHINullCommandListState s_commandListState[FLUXION_RHI_NULL_MAX_COMMAND_LISTS];

static bool Fluxion_RHI_Null_RequireRecording(FluxionRHICommandListHandle commandList, const char* what)
{
    if (!Fluxion_RHINull_IsValid(s_commandListSlots, FLUXION_RHI_NULL_MAX_COMMAND_LISTS, commandList.index, commandList.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: command list handle is invalid or destroyed");
        FLUXION_UNUSED(what);
        return false;
    }
    if (!s_commandListState[commandList.index].recording)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: command recorded outside Begin/End");
        FLUXION_UNUSED(what);
        return false;
    }
    return true;
}

// --- Fences (real state: signaled) ------------------------------------------

static FluxionRHINullSlot s_fenceSlots[FLUXION_RHI_NULL_MAX_FENCES];
static bool s_fenceSignaled[FLUXION_RHI_NULL_MAX_FENCES];

// --- Swapchains (real state: a fixed set of fake image textures) -----------

typedef struct FluxionRHINullSwapchainState
{
    FluxionRHITextureHandle images[FLUXION_RHI_NULL_MAX_SWAPCHAIN_IMAGES];
    u32 imageCount;
    u32 currentImageIndex;
} FluxionRHINullSwapchainState;

static FluxionRHINullSlot s_swapchainSlots[FLUXION_RHI_NULL_MAX_SWAPCHAINS];
static FluxionRHINullSwapchainState s_swapchainState[FLUXION_RHI_NULL_MAX_SWAPCHAINS];

// --- Adapter -----------------------------------------------------------------
//
// The Null backend reports exactly one fake adapter, with every
// capability true and generous limits, so device creation never fails a
// capability check regardless of what a test/caller asks for.

static bool Fluxion_RHI_Null_IsFakeAdapter(FluxionRHIAdapterHandle adapter)
{
    return adapter.index == 0 && adapter.generation == 0;
}

// --- Instance lifetime -------------------------------------------------------

static void Fluxion_RHI_Null_ResetAllState(void)
{
    memset(s_bufferPool, 0, sizeof(s_bufferPool));
    memset(s_texturePool, 0, sizeof(s_texturePool));
    memset(s_textureViewPool, 0, sizeof(s_textureViewPool));
    memset(s_samplerPool, 0, sizeof(s_samplerPool));
    memset(s_shaderPool, 0, sizeof(s_shaderPool));
    memset(s_pipelinePool, 0, sizeof(s_pipelinePool));
    memset(s_semaphorePool, 0, sizeof(s_semaphorePool));
    memset(s_queryPoolPool, 0, sizeof(s_queryPoolPool));
    memset(s_deviceSlots, 0, sizeof(s_deviceSlots));
    memset(s_commandListSlots, 0, sizeof(s_commandListSlots));
    memset(s_commandListState, 0, sizeof(s_commandListState));
    memset(s_fenceSlots, 0, sizeof(s_fenceSlots));
    memset(s_fenceSignaled, 0, sizeof(s_fenceSignaled));
    memset(s_swapchainSlots, 0, sizeof(s_swapchainSlots));
    memset(s_swapchainState, 0, sizeof(s_swapchainState));
}

static void Fluxion_RHI_Null_DestroyInstance(FluxionRHIInstanceHandle instance)
{
    FLUXION_UNUSED(instance);
    Fluxion_RHI_Null_ResetAllState();
}

// --- Adapters / devices / queues ---------------------------------------------

static u32 Fluxion_RHI_Null_EnumerateAdapters(FluxionRHIInstanceHandle instance, FluxionRHIAdapterHandle* outAdapters, u32 maxAdapters)
{
    FLUXION_UNUSED(instance);
    if (outAdapters == NULL) return 1;
    if (maxAdapters == 0) return 0;
    outAdapters[0].index = 0;
    outAdapters[0].generation = 0;
    return 1;
}

static bool Fluxion_RHI_Null_GetAdapterInfo(FluxionRHIAdapterHandle adapter, FluxionRHIAdapterInfo* outInfo)
{
    if (!Fluxion_RHI_Null_IsFakeAdapter(adapter) || outInfo == NULL) return false;

    memset(outInfo, 0, sizeof(*outInfo));
#if defined(_MSC_VER)
    strncpy_s(outInfo->name, sizeof(outInfo->name), "Null Adapter", sizeof(outInfo->name) - 1);
#else
    strncpy(outInfo->name, "Null Adapter", sizeof(outInfo->name) - 1);
#endif
    outInfo->vendorId = 0;
    outInfo->deviceId = 0;
    outInfo->type = FLUXION_RHI_ADAPTER_TYPE_CPU;
    outInfo->dedicatedVRAM = 0;
    outInfo->sharedMemory = 0;
    outInfo->driverVersion = 0;
    outInfo->apiVersion = 0;

    // Every capability reads as available -- the Null backend never has
    // to reject a device-creation request for a missing feature, since
    // it doesn't implement any of them for real either way.
    outInfo->capabilities = FLUXION_RHI_CAPABILITY_BINDLESS_RESOURCES | FLUXION_RHI_CAPABILITY_DESCRIPTOR_INDEXING |
        FLUXION_RHI_CAPABILITY_MESH_SHADERS | FLUXION_RHI_CAPABILITY_RAY_TRACING | FLUXION_RHI_CAPABILITY_RAY_QUERY |
        FLUXION_RHI_CAPABILITY_VARIABLE_RATE_SHADING | FLUXION_RHI_CAPABILITY_ASYNC_COMPUTE | FLUXION_RHI_CAPABILITY_ASYNC_TRANSFER |
        FLUXION_RHI_CAPABILITY_BUFFER_DEVICE_ADDRESS | FLUXION_RHI_CAPABILITY_INDIRECT_COUNT | FLUXION_RHI_CAPABILITY_TIMELINE_SYNC |
        FLUXION_RHI_CAPABILITY_COMPUTE_SHADERS | FLUXION_RHI_CAPABILITY_TESSELLATION | FLUXION_RHI_CAPABILITY_GEOMETRY_SHADERS |
        FLUXION_RHI_CAPABILITY_FP16 | FLUXION_RHI_CAPABILITY_INT64 | FLUXION_RHI_CAPABILITY_WAVE_OPERATIONS | FLUXION_RHI_CAPABILITY_SPARSE_RESOURCES;

    outInfo->limits.maxTextureDimension1D = 16384;
    outInfo->limits.maxTextureDimension2D = 16384;
    outInfo->limits.maxTextureDimension3D = 2048;
    outInfo->limits.maxTextureArrayLayers = 2048;
    outInfo->limits.maxColorAttachments = 8;
    outInfo->limits.maxBoundDescriptorSets = 8;
    outInfo->limits.maxPushConstantSize = 256;
    outInfo->limits.minUniformBufferOffsetAlignment = 256;
    outInfo->limits.maxComputeWorkGroupSize[0] = 1024;
    outInfo->limits.maxComputeWorkGroupSize[1] = 1024;
    outInfo->limits.maxComputeWorkGroupSize[2] = 64;
    outInfo->limits.maxComputeWorkGroupInvocations = 1024;
    return true;
}

static FluxionRHIDeviceHandle Fluxion_RHI_Null_CreateDevice(FluxionRHIAdapterHandle adapter, const FluxionRHIDeviceDesc* desc)
{
    FLUXION_UNUSED(desc); // the fake adapter reports every capability, so nothing to reject
    FluxionRHIDeviceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHI_Null_IsFakeAdapter(adapter)) return invalid;

    u32 index, generation;
    if (!Fluxion_RHINull_Allocate(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, &index, &generation)) return invalid;

    FluxionRHIDeviceHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHI_Null_DestroyDevice(FluxionRHIDeviceHandle device)
{
    Fluxion_RHINull_Free(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation);
}

static FluxionRHIQueueHandle Fluxion_RHI_Null_GetQueue(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHIQueueHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return invalid;

    // Derived deterministically from the device, not separately pooled --
    // a device always has exactly the 3 queue types, so there is nothing
    // to allocate/free independently of the device itself.
    FluxionRHIQueueHandle queue;
    queue.index = device.index * 3u + (u32)type;
    queue.generation = device.generation;
    return queue;
}

// --- Command lists -------------------------------------------------------

static FluxionRHICommandListHandle Fluxion_RHI_Null_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHICommandListHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return invalid;

    u32 index, generation;
    if (!Fluxion_RHINull_Allocate(s_commandListSlots, FLUXION_RHI_NULL_MAX_COMMAND_LISTS, &index, &generation)) return invalid;

    s_commandListState[index].queueType = type;
    s_commandListState[index].recording = false;
    s_commandListState[index].insideRendering = false;

    FluxionRHICommandListHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHI_Null_DestroyCommandList(FluxionRHICommandListHandle commandList)
{
    Fluxion_RHINull_Free(s_commandListSlots, FLUXION_RHI_NULL_MAX_COMMAND_LISTS, commandList.index, commandList.generation);
}

static void Fluxion_RHI_Null_CommandListBegin(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHINull_IsValid(s_commandListSlots, FLUXION_RHI_NULL_MAX_COMMAND_LISTS, commandList.index, commandList.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: Begin called with an invalid command list handle");
        return;
    }
    FLUXION_ASSERT_MSG(!s_commandListState[commandList.index].recording, "Fluxion RHI Null backend: Begin called while already recording");
    s_commandListState[commandList.index].recording = true;
}

static void Fluxion_RHI_Null_CommandListEnd(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHI_Null_RequireRecording(commandList, "End")) return;
    FLUXION_ASSERT_MSG(!s_commandListState[commandList.index].insideRendering, "Fluxion RHI Null backend: End called while still inside BeginRendering/EndRendering");
    s_commandListState[commandList.index].recording = false;
}

static void Fluxion_RHI_Null_CommandListBeginRendering(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc)
{
    FLUXION_UNUSED(desc);
    if (!Fluxion_RHI_Null_RequireRecording(commandList, "BeginRendering")) return;
    FLUXION_ASSERT_MSG(!s_commandListState[commandList.index].insideRendering, "Fluxion RHI Null backend: BeginRendering called twice without an EndRendering");
    s_commandListState[commandList.index].insideRendering = true;
}

static void Fluxion_RHI_Null_CommandListEndRendering(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHI_Null_RequireRecording(commandList, "EndRendering")) return;
    FLUXION_ASSERT_MSG(s_commandListState[commandList.index].insideRendering, "Fluxion RHI Null backend: EndRendering called without a matching BeginRendering");
    s_commandListState[commandList.index].insideRendering = false;
}

static void Fluxion_RHI_Null_CommandListSetPipeline(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline)
{
    FLUXION_UNUSED(pipeline);
    Fluxion_RHI_Null_RequireRecording(commandList, "SetPipeline");
}

static void Fluxion_RHI_Null_CommandListSetVertexBuffer(FluxionRHICommandListHandle commandList, u32 slot, FluxionRHIBufferHandle buffer, usize offset)
{
    FLUXION_UNUSED(slot);
    FLUXION_UNUSED(buffer);
    FLUXION_UNUSED(offset);
    Fluxion_RHI_Null_RequireRecording(commandList, "SetVertexBuffer");
}

static void Fluxion_RHI_Null_CommandListSetIndexBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle buffer, usize offset, bool use16BitIndices)
{
    FLUXION_UNUSED(buffer);
    FLUXION_UNUSED(offset);
    FLUXION_UNUSED(use16BitIndices);
    Fluxion_RHI_Null_RequireRecording(commandList, "SetIndexBuffer");
}

static void Fluxion_RHI_Null_CommandListDraw(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    FLUXION_UNUSED(vertexCount);
    FLUXION_UNUSED(instanceCount);
    FLUXION_UNUSED(firstVertex);
    FLUXION_UNUSED(firstInstance);
    if (!Fluxion_RHI_Null_RequireRecording(commandList, "Draw")) return;
    FLUXION_ASSERT_MSG(s_commandListState[commandList.index].insideRendering, "Fluxion RHI Null backend: Draw called outside BeginRendering/EndRendering");
}

static void Fluxion_RHI_Null_CommandListDrawIndexed(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
    FLUXION_UNUSED(indexCount);
    FLUXION_UNUSED(instanceCount);
    FLUXION_UNUSED(firstIndex);
    FLUXION_UNUSED(vertexOffset);
    FLUXION_UNUSED(firstInstance);
    if (!Fluxion_RHI_Null_RequireRecording(commandList, "DrawIndexed")) return;
    FLUXION_ASSERT_MSG(s_commandListState[commandList.index].insideRendering, "Fluxion RHI Null backend: DrawIndexed called outside BeginRendering/EndRendering");
}

static void Fluxion_RHI_Null_CommandListDrawIndirect(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride)
{
    FLUXION_UNUSED(argsBuffer);
    FLUXION_UNUSED(offset);
    FLUXION_UNUSED(drawCount);
    FLUXION_UNUSED(stride);
    if (!Fluxion_RHI_Null_RequireRecording(commandList, "DrawIndirect")) return;
    FLUXION_ASSERT_MSG(s_commandListState[commandList.index].insideRendering, "Fluxion RHI Null backend: DrawIndirect called outside BeginRendering/EndRendering");
}

static void Fluxion_RHI_Null_CommandListDispatch(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    FLUXION_UNUSED(groupCountX);
    FLUXION_UNUSED(groupCountY);
    FLUXION_UNUSED(groupCountZ);
    Fluxion_RHI_Null_RequireRecording(commandList, "Dispatch");
}

static void Fluxion_RHI_Null_CommandListCopyBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHIBufferHandle dst, usize dstOffset, usize size)
{
    FLUXION_UNUSED(src);
    FLUXION_UNUSED(srcOffset);
    FLUXION_UNUSED(dst);
    FLUXION_UNUSED(dstOffset);
    FLUXION_UNUSED(size);
    Fluxion_RHI_Null_RequireRecording(commandList, "CopyBuffer");
}

static void Fluxion_RHI_Null_CommandListCopyTexture(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, FluxionRHITextureHandle dst)
{
    FLUXION_UNUSED(src);
    FLUXION_UNUSED(dst);
    Fluxion_RHI_Null_RequireRecording(commandList, "CopyTexture");
}

static void Fluxion_RHI_Null_CommandListBarrier(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount)
{
    FLUXION_UNUSED(barriers);
    FLUXION_UNUSED(barrierCount);
    Fluxion_RHI_Null_RequireRecording(commandList, "Barrier");
}

// --- Submission ------------------------------------------------------------

static void Fluxion_RHI_Null_QueueSubmit(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence)
{
    FLUXION_UNUSED(queue);
    for (u32 i = 0; i < commandListCount; ++i)
    {
        FluxionRHICommandListHandle cl = commandLists[i];
        if (!Fluxion_RHINull_IsValid(s_commandListSlots, FLUXION_RHI_NULL_MAX_COMMAND_LISTS, cl.index, cl.generation))
        {
            FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: Submit called with an invalid command list handle");
            continue;
        }
        FLUXION_ASSERT_MSG(!s_commandListState[cl.index].recording, "Fluxion RHI Null backend: Submit called on a command list still between Begin and End");
    }

    // The Null backend "executes" instantly (there is no GPU timeline to
    // wait on), so a signal fence is immediately marked signaled.
    if (FLUXION_HANDLE_IS_VALID(signalFence) && Fluxion_RHINull_IsValid(s_fenceSlots, FLUXION_RHI_NULL_MAX_FENCES, signalFence.index, signalFence.generation))
    {
        s_fenceSignaled[signalFence.index] = true;
    }
}

// --- Buffers / textures / views / samplers ------------------------------

static FluxionRHIBufferHandle Fluxion_RHI_Null_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc)
{
    FLUXION_UNUSED(desc);
    FluxionRHIBufferHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return handle;
    s_bufferPool_Allocate(&handle);
    return handle;
}

static void Fluxion_RHI_Null_DestroyBuffer(FluxionRHIBufferHandle buffer) { s_bufferPool_Free(buffer); }

static FluxionRHITextureHandle Fluxion_RHI_Null_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc)
{
    FLUXION_UNUSED(desc);
    FluxionRHITextureHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return handle;
    s_texturePool_Allocate(&handle);
    return handle;
}

static void Fluxion_RHI_Null_DestroyTexture(FluxionRHITextureHandle texture) { s_texturePool_Free(texture); }

static FluxionRHITextureViewHandle Fluxion_RHI_Null_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc)
{
    FluxionRHITextureViewHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (desc == NULL || !s_texturePool_IsValid(desc->texture)) return handle;
    s_textureViewPool_Allocate(&handle);
    return handle;
}

static void Fluxion_RHI_Null_DestroyTextureView(FluxionRHITextureViewHandle view) { s_textureViewPool_Free(view); }

static FluxionRHISamplerHandle Fluxion_RHI_Null_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc)
{
    FLUXION_UNUSED(desc);
    FluxionRHISamplerHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return handle;
    s_samplerPool_Allocate(&handle);
    return handle;
}

static void Fluxion_RHI_Null_DestroySampler(FluxionRHISamplerHandle sampler) { s_samplerPool_Free(sampler); }

// --- Shaders / pipelines -------------------------------------------------

static FluxionRHIShaderHandle Fluxion_RHI_Null_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc)
{
    FLUXION_UNUSED(desc);
    FluxionRHIShaderHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return handle;
    s_shaderPool_Allocate(&handle);
    return handle;
}

static void Fluxion_RHI_Null_DestroyShader(FluxionRHIShaderHandle shader) { s_shaderPool_Free(shader); }

static FluxionRHIPipelineHandle Fluxion_RHI_Null_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc)
{
    FluxionRHIPipelineHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return handle;
    if (desc == NULL || !s_shaderPool_IsValid(desc->vertexShader) || !s_shaderPool_IsValid(desc->fragmentShader)) return handle;
    s_pipelinePool_Allocate(&handle);
    return handle;
}

static void Fluxion_RHI_Null_DestroyPipeline(FluxionRHIPipelineHandle pipeline) { s_pipelinePool_Free(pipeline); }

// --- Swapchain -----------------------------------------------------------

static FluxionRHISwapchainHandle Fluxion_RHI_Null_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc)
{
    FluxionRHISwapchainHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return invalid;
    if (!FLUXION_HANDLE_IS_VALID(window) || desc == NULL) return invalid;

    u32 index, generation;
    if (!Fluxion_RHINull_Allocate(s_swapchainSlots, FLUXION_RHI_NULL_MAX_SWAPCHAINS, &index, &generation)) return invalid;

    u32 imageCount = desc->bufferCount;
    if (imageCount == 0) imageCount = 1;
    if (imageCount > FLUXION_RHI_NULL_MAX_SWAPCHAIN_IMAGES) imageCount = FLUXION_RHI_NULL_MAX_SWAPCHAIN_IMAGES;

    FluxionRHINullSwapchainState* state = &s_swapchainState[index];
    state->imageCount = 0;
    state->currentImageIndex = 0;
    for (u32 i = 0; i < imageCount; ++i)
    {
        FluxionRHITextureHandle image = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        if (!s_texturePool_Allocate(&image)) break;
        state->images[i] = image;
        ++state->imageCount;
    }

    FluxionRHISwapchainHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHI_Null_DestroySwapchain(FluxionRHISwapchainHandle swapchain)
{
    if (!Fluxion_RHINull_IsValid(s_swapchainSlots, FLUXION_RHI_NULL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: destroy called with an invalid or already-destroyed swapchain handle");
        return;
    }
    FluxionRHINullSwapchainState* state = &s_swapchainState[swapchain.index];
    for (u32 i = 0; i < state->imageCount; ++i)
    {
        s_texturePool_Free(state->images[i]);
    }
    state->imageCount = 0;
    Fluxion_RHINull_Free(s_swapchainSlots, FLUXION_RHI_NULL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation);
}

static u32 Fluxion_RHI_Null_SwapchainAcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore)
{
    FLUXION_UNUSED(signalSemaphore);
    if (!Fluxion_RHINull_IsValid(s_swapchainSlots, FLUXION_RHI_NULL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation)) return 0;
    FluxionRHINullSwapchainState* state = &s_swapchainState[swapchain.index];
    if (state->imageCount == 0) return 0;
    u32 imageIndex = state->currentImageIndex;
    state->currentImageIndex = (state->currentImageIndex + 1) % state->imageCount;
    return imageIndex;
}

static FluxionRHITextureHandle Fluxion_RHI_Null_SwapchainGetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex)
{
    FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_swapchainSlots, FLUXION_RHI_NULL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation)) return invalid;
    if (imageIndex >= s_swapchainState[swapchain.index].imageCount) return invalid;
    return s_swapchainState[swapchain.index].images[imageIndex];
}

static void Fluxion_RHI_Null_SwapchainPresent(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore)
{
    FLUXION_UNUSED(imageIndex);
    FLUXION_UNUSED(waitSemaphore);
    if (!Fluxion_RHINull_IsValid(s_swapchainSlots, FLUXION_RHI_NULL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: Present called with an invalid swapchain handle");
    }
}

// --- Synchronization ----------------------------------------------------------

static FluxionRHIFenceHandle Fluxion_RHI_Null_CreateFence(FluxionRHIDeviceHandle device, bool signaled)
{
    FluxionRHIFenceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return invalid;

    u32 index, generation;
    if (!Fluxion_RHINull_Allocate(s_fenceSlots, FLUXION_RHI_NULL_MAX_FENCES, &index, &generation)) return invalid;
    s_fenceSignaled[index] = signaled;

    FluxionRHIFenceHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHI_Null_DestroyFence(FluxionRHIFenceHandle fence)
{
    Fluxion_RHINull_Free(s_fenceSlots, FLUXION_RHI_NULL_MAX_FENCES, fence.index, fence.generation);
}

static void Fluxion_RHI_Null_WaitForFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHINull_IsValid(s_fenceSlots, FLUXION_RHI_NULL_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: WaitForFence called with an invalid fence handle");
        return;
    }
    // Nothing is ever actually submitted to a GPU timeline here, so
    // "waiting" completes instantly.
    s_fenceSignaled[fence.index] = true;
}

static void Fluxion_RHI_Null_ResetFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHINull_IsValid(s_fenceSlots, FLUXION_RHI_NULL_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Null backend: ResetFence called with an invalid fence handle");
        return;
    }
    s_fenceSignaled[fence.index] = false;
}

static FluxionRHISemaphoreHandle Fluxion_RHI_Null_CreateSemaphore(FluxionRHIDeviceHandle device)
{
    FluxionRHISemaphoreHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return handle;
    s_semaphorePool_Allocate(&handle);
    return handle;
}

static void Fluxion_RHI_Null_DestroySemaphore(FluxionRHISemaphoreHandle semaphore) { s_semaphorePool_Free(semaphore); }

static FluxionRHIQueryPoolHandle Fluxion_RHI_Null_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount)
{
    FLUXION_UNUSED(queryCount);
    FluxionRHIQueryPoolHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHINull_IsValid(s_deviceSlots, FLUXION_RHI_NULL_MAX_DEVICES, device.index, device.generation)) return handle;
    s_queryPoolPool_Allocate(&handle);
    return handle;
}

static void Fluxion_RHI_Null_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool) { s_queryPoolPool_Free(queryPool); }

// --- Native handle escape hatch -----------------------------------------

// Nothing native to hand out -- the Null backend has no real GPU objects
// behind any handle, so every getter returns an empty native handle.
static FluxionRHINativeHandle Fluxion_RHI_Null_GetNativeDeviceHandle(FluxionRHIDeviceHandle device)
{
    FLUXION_UNUSED(device);
    FluxionRHINativeHandle handle = { NULL };
    return handle;
}

static FluxionRHINativeHandle Fluxion_RHI_Null_GetNativeBufferHandle(FluxionRHIBufferHandle buffer)
{
    FLUXION_UNUSED(buffer);
    FluxionRHINativeHandle handle = { NULL };
    return handle;
}

static FluxionRHINativeHandle Fluxion_RHI_Null_GetNativeTextureHandle(FluxionRHITextureHandle texture)
{
    FLUXION_UNUSED(texture);
    FluxionRHINativeHandle handle = { NULL };
    return handle;
}

// --- Backend entry point -------------------------------------------------

static const FluxionRHIBackendVTable s_nullVTable = {
    Fluxion_RHI_Null_DestroyInstance,
    Fluxion_RHI_Null_EnumerateAdapters,
    Fluxion_RHI_Null_GetAdapterInfo,

    Fluxion_RHI_Null_CreateDevice,
    Fluxion_RHI_Null_DestroyDevice,
    Fluxion_RHI_Null_GetQueue,

    Fluxion_RHI_Null_CreateCommandList,
    Fluxion_RHI_Null_DestroyCommandList,
    Fluxion_RHI_Null_CommandListBegin,
    Fluxion_RHI_Null_CommandListEnd,
    Fluxion_RHI_Null_CommandListBeginRendering,
    Fluxion_RHI_Null_CommandListEndRendering,
    Fluxion_RHI_Null_CommandListSetPipeline,
    Fluxion_RHI_Null_CommandListSetVertexBuffer,
    Fluxion_RHI_Null_CommandListSetIndexBuffer,
    Fluxion_RHI_Null_CommandListDraw,
    Fluxion_RHI_Null_CommandListDrawIndexed,
    Fluxion_RHI_Null_CommandListDrawIndirect,
    Fluxion_RHI_Null_CommandListDispatch,
    Fluxion_RHI_Null_CommandListCopyBuffer,
    Fluxion_RHI_Null_CommandListCopyTexture,
    Fluxion_RHI_Null_CommandListBarrier,

    Fluxion_RHI_Null_QueueSubmit,

    Fluxion_RHI_Null_CreateBuffer,
    Fluxion_RHI_Null_DestroyBuffer,
    Fluxion_RHI_Null_CreateTexture,
    Fluxion_RHI_Null_DestroyTexture,
    Fluxion_RHI_Null_CreateTextureView,
    Fluxion_RHI_Null_DestroyTextureView,
    Fluxion_RHI_Null_CreateSampler,
    Fluxion_RHI_Null_DestroySampler,

    Fluxion_RHI_Null_CreateShader,
    Fluxion_RHI_Null_DestroyShader,
    Fluxion_RHI_Null_CreateGraphicsPipeline,
    Fluxion_RHI_Null_DestroyPipeline,

    Fluxion_RHI_Null_CreateSwapchain,
    Fluxion_RHI_Null_DestroySwapchain,
    Fluxion_RHI_Null_SwapchainAcquireNextImage,
    Fluxion_RHI_Null_SwapchainGetTexture,
    Fluxion_RHI_Null_SwapchainPresent,

    Fluxion_RHI_Null_CreateFence,
    Fluxion_RHI_Null_DestroyFence,
    Fluxion_RHI_Null_WaitForFence,
    Fluxion_RHI_Null_ResetFence,
    Fluxion_RHI_Null_CreateSemaphore,
    Fluxion_RHI_Null_DestroySemaphore,
    Fluxion_RHI_Null_CreateQueryPool,
    Fluxion_RHI_Null_DestroyQueryPool,

    Fluxion_RHI_Null_GetNativeDeviceHandle,
    Fluxion_RHI_Null_GetNativeBufferHandle,
    Fluxion_RHI_Null_GetNativeTextureHandle,
};

const FluxionRHIBackendVTable* Fluxion_RHI_Null_CreateInstance(const FluxionRHIInstanceDesc* desc, FluxionRHIInstanceHandle* outInstance)
{
    FLUXION_UNUSED(desc);
    Fluxion_RHI_Null_ResetAllState();
    outInstance->index = 0;
    outInstance->generation = 0;
    return &s_nullVTable;
}
