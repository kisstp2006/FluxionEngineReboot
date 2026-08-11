#include <Fluxion/RHI/RHI.h>

#include "RHIBackendVTable.h"

#include <Fluxion/Foundation/Assert.h>

extern const FluxionRHIBackendVTable* Fluxion_RHI_Null_CreateInstance(const FluxionRHIInstanceDesc* desc, FluxionRHIInstanceHandle* outInstance);

// One active backend at a time, matching the doc's own framing (a
// shipping run picks one backend via --graphics=X, not several
// simultaneously) -- this keeps every object handle unambiguous without
// each one needing to carry its own back-reference to which instance/
// backend created it. Attach/detach is startup/shutdown-shaped, same
// unsynchronized-global pattern as the Service/Subsystem Registry and the
// Profiler's backend slot.
static const FluxionRHIBackendVTable* s_backend = NULL;
static bool s_instanceActive = false;

FluxionRHIInstanceHandle Fluxion_RHI_CreateInstance(FluxionRHIBackendType backend, const FluxionRHIInstanceDesc* desc)
{
    FLUXION_ASSERT_MSG(!s_instanceActive, "Fluxion_RHI_CreateInstance: an instance is already active -- only one RHI backend can be active per process at a time");
    if (s_instanceActive)
    {
        FluxionRHIInstanceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return invalid;
    }

    FluxionRHIInstanceHandle instance = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIBackendVTable* vtable = NULL;

    switch (backend)
    {
        case FLUXION_RHI_BACKEND_NULL:
            vtable = Fluxion_RHI_Null_CreateInstance(desc, &instance);
            break;
        default:
            FLUXION_ASSERT_MSG(false, "Fluxion_RHI_CreateInstance: unknown or not-yet-implemented backend");
            break;
    }

    if (vtable == NULL || !FLUXION_HANDLE_IS_VALID(instance))
    {
        FluxionRHIInstanceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        return invalid;
    }

    s_backend = vtable;
    s_instanceActive = true;
    return instance;
}

void Fluxion_RHI_DestroyInstance(FluxionRHIInstanceHandle instance)
{
    if (!s_instanceActive || s_backend == NULL) return;
    s_backend->DestroyInstance(instance);
    s_backend = NULL;
    s_instanceActive = false;
}

u32 Fluxion_RHI_EnumerateAdapters(FluxionRHIInstanceHandle instance, FluxionRHIAdapterHandle* outAdapters, u32 maxAdapters)
{
    if (s_backend == NULL) return 0;
    return s_backend->EnumerateAdapters(instance, outAdapters, maxAdapters);
}

bool Fluxion_RHI_GetAdapterInfo(FluxionRHIAdapterHandle adapter, FluxionRHIAdapterInfo* outInfo)
{
    if (s_backend == NULL) return false;
    return s_backend->GetAdapterInfo(adapter, outInfo);
}

FluxionRHIDeviceHandle Fluxion_RHI_CreateDevice(FluxionRHIAdapterHandle adapter, const FluxionRHIDeviceDesc* desc)
{
    if (s_backend == NULL) { FluxionRHIDeviceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateDevice(adapter, desc);
}

void Fluxion_RHI_DestroyDevice(FluxionRHIDeviceHandle device)
{
    if (s_backend == NULL) return;
    s_backend->DestroyDevice(device);
}

FluxionRHIQueueHandle Fluxion_RHI_GetQueue(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    if (s_backend == NULL) { FluxionRHIQueueHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->GetQueue(device, type);
}

FluxionRHICommandListHandle Fluxion_RHI_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    if (s_backend == NULL) { FluxionRHICommandListHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateCommandList(device, type);
}

void Fluxion_RHI_DestroyCommandList(FluxionRHICommandListHandle commandList)
{
    if (s_backend == NULL) return;
    s_backend->DestroyCommandList(commandList);
}

void Fluxion_RHI_CommandList_Begin(FluxionRHICommandListHandle commandList)
{
    if (s_backend == NULL) return;
    s_backend->CommandListBegin(commandList);
}

void Fluxion_RHI_CommandList_End(FluxionRHICommandListHandle commandList)
{
    if (s_backend == NULL) return;
    s_backend->CommandListEnd(commandList);
}

void Fluxion_RHI_CommandList_BeginRendering(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc)
{
    if (s_backend == NULL) return;
    s_backend->CommandListBeginRendering(commandList, desc);
}

void Fluxion_RHI_CommandList_EndRendering(FluxionRHICommandListHandle commandList)
{
    if (s_backend == NULL) return;
    s_backend->CommandListEndRendering(commandList);
}

void Fluxion_RHI_CommandList_SetPipeline(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline)
{
    if (s_backend == NULL) return;
    s_backend->CommandListSetPipeline(commandList, pipeline);
}

void Fluxion_RHI_CommandList_SetVertexBuffer(FluxionRHICommandListHandle commandList, u32 slot, FluxionRHIBufferHandle buffer, usize offset)
{
    if (s_backend == NULL) return;
    s_backend->CommandListSetVertexBuffer(commandList, slot, buffer, offset);
}

void Fluxion_RHI_CommandList_SetIndexBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle buffer, usize offset, bool use16BitIndices)
{
    if (s_backend == NULL) return;
    s_backend->CommandListSetIndexBuffer(commandList, buffer, offset, use16BitIndices);
}

void Fluxion_RHI_CommandList_Draw(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    if (s_backend == NULL) return;
    s_backend->CommandListDraw(commandList, vertexCount, instanceCount, firstVertex, firstInstance);
}

void Fluxion_RHI_CommandList_DrawIndexed(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
    if (s_backend == NULL) return;
    s_backend->CommandListDrawIndexed(commandList, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void Fluxion_RHI_CommandList_DrawIndirect(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride)
{
    if (s_backend == NULL) return;
    s_backend->CommandListDrawIndirect(commandList, argsBuffer, offset, drawCount, stride);
}

void Fluxion_RHI_CommandList_Dispatch(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    if (s_backend == NULL) return;
    s_backend->CommandListDispatch(commandList, groupCountX, groupCountY, groupCountZ);
}

void Fluxion_RHI_CommandList_CopyBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHIBufferHandle dst, usize dstOffset, usize size)
{
    if (s_backend == NULL) return;
    s_backend->CommandListCopyBuffer(commandList, src, srcOffset, dst, dstOffset, size);
}

void Fluxion_RHI_CommandList_CopyTexture(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, FluxionRHITextureHandle dst)
{
    if (s_backend == NULL) return;
    s_backend->CommandListCopyTexture(commandList, src, dst);
}

void Fluxion_RHI_CommandList_Barrier(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount)
{
    if (s_backend == NULL) return;
    s_backend->CommandListBarrier(commandList, barriers, barrierCount);
}

void Fluxion_RHI_Queue_Submit(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence)
{
    if (s_backend == NULL) return;
    s_backend->QueueSubmit(queue, commandLists, commandListCount, signalFence);
}

FluxionRHIBufferHandle Fluxion_RHI_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc)
{
    if (s_backend == NULL) { FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateBuffer(device, desc);
}

void Fluxion_RHI_DestroyBuffer(FluxionRHIBufferHandle buffer)
{
    if (s_backend == NULL) return;
    s_backend->DestroyBuffer(buffer);
}

FluxionRHITextureHandle Fluxion_RHI_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc)
{
    if (s_backend == NULL) { FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateTexture(device, desc);
}

void Fluxion_RHI_DestroyTexture(FluxionRHITextureHandle texture)
{
    if (s_backend == NULL) return;
    s_backend->DestroyTexture(texture);
}

FluxionRHITextureViewHandle Fluxion_RHI_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc)
{
    if (s_backend == NULL) { FluxionRHITextureViewHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateTextureView(device, desc);
}

void Fluxion_RHI_DestroyTextureView(FluxionRHITextureViewHandle view)
{
    if (s_backend == NULL) return;
    s_backend->DestroyTextureView(view);
}

FluxionRHISamplerHandle Fluxion_RHI_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc)
{
    if (s_backend == NULL) { FluxionRHISamplerHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateSampler(device, desc);
}

void Fluxion_RHI_DestroySampler(FluxionRHISamplerHandle sampler)
{
    if (s_backend == NULL) return;
    s_backend->DestroySampler(sampler);
}

FluxionRHIShaderHandle Fluxion_RHI_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc)
{
    if (s_backend == NULL) { FluxionRHIShaderHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateShader(device, desc);
}

void Fluxion_RHI_DestroyShader(FluxionRHIShaderHandle shader)
{
    if (s_backend == NULL) return;
    s_backend->DestroyShader(shader);
}

FluxionRHIPipelineHandle Fluxion_RHI_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc)
{
    if (s_backend == NULL) { FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateGraphicsPipeline(device, desc);
}

void Fluxion_RHI_DestroyPipeline(FluxionRHIPipelineHandle pipeline)
{
    if (s_backend == NULL) return;
    s_backend->DestroyPipeline(pipeline);
}

FluxionRHISwapchainHandle Fluxion_RHI_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc)
{
    if (s_backend == NULL) { FluxionRHISwapchainHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateSwapchain(device, window, desc);
}

void Fluxion_RHI_DestroySwapchain(FluxionRHISwapchainHandle swapchain)
{
    if (s_backend == NULL) return;
    s_backend->DestroySwapchain(swapchain);
}

u32 Fluxion_RHI_Swapchain_AcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore)
{
    if (s_backend == NULL) return 0;
    return s_backend->SwapchainAcquireNextImage(swapchain, signalSemaphore);
}

FluxionRHITextureHandle Fluxion_RHI_Swapchain_GetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex)
{
    if (s_backend == NULL) { FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->SwapchainGetTexture(swapchain, imageIndex);
}

void Fluxion_RHI_Swapchain_Present(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore)
{
    if (s_backend == NULL) return;
    s_backend->SwapchainPresent(swapchain, imageIndex, waitSemaphore);
}

FluxionRHIFenceHandle Fluxion_RHI_CreateFence(FluxionRHIDeviceHandle device, bool signaled)
{
    if (s_backend == NULL) { FluxionRHIFenceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateFence(device, signaled);
}

void Fluxion_RHI_DestroyFence(FluxionRHIFenceHandle fence)
{
    if (s_backend == NULL) return;
    s_backend->DestroyFence(fence);
}

void Fluxion_RHI_WaitForFence(FluxionRHIFenceHandle fence)
{
    if (s_backend == NULL) return;
    s_backend->WaitForFence(fence);
}

void Fluxion_RHI_ResetFence(FluxionRHIFenceHandle fence)
{
    if (s_backend == NULL) return;
    s_backend->ResetFence(fence);
}

FluxionRHISemaphoreHandle Fluxion_RHI_CreateSemaphore(FluxionRHIDeviceHandle device)
{
    if (s_backend == NULL) { FluxionRHISemaphoreHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateSemaphore(device);
}

void Fluxion_RHI_DestroySemaphore(FluxionRHISemaphoreHandle semaphore)
{
    if (s_backend == NULL) return;
    s_backend->DestroySemaphore(semaphore);
}

FluxionRHIQueryPoolHandle Fluxion_RHI_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount)
{
    if (s_backend == NULL) { FluxionRHIQueryPoolHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 }; return invalid; }
    return s_backend->CreateQueryPool(device, queryCount);
}

void Fluxion_RHI_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool)
{
    if (s_backend == NULL) return;
    s_backend->DestroyQueryPool(queryPool);
}

FluxionRHINativeHandle Fluxion_RHI_GetNativeDeviceHandle(FluxionRHIDeviceHandle device)
{
    if (s_backend == NULL) { FluxionRHINativeHandle invalid = { NULL }; return invalid; }
    return s_backend->GetNativeDeviceHandle(device);
}

FluxionRHINativeHandle Fluxion_RHI_GetNativeBufferHandle(FluxionRHIBufferHandle buffer)
{
    if (s_backend == NULL) { FluxionRHINativeHandle invalid = { NULL }; return invalid; }
    return s_backend->GetNativeBufferHandle(buffer);
}

FluxionRHINativeHandle Fluxion_RHI_GetNativeTextureHandle(FluxionRHITextureHandle texture)
{
    if (s_backend == NULL) { FluxionRHINativeHandle invalid = { NULL }; return invalid; }
    return s_backend->GetNativeTextureHandle(texture);
}
