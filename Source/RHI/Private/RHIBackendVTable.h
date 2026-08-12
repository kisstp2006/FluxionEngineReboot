#pragma once

// Module-private: never included from Include/. One function pointer per
// public Fluxion_RHI_* operation (device creation onward -- CreateInstance
// itself is special, see below), mirroring the public signatures exactly
// so RHI.c's dispatch layer is a trivial pass-through. Every backend
// (Null now, Vulkan/OpenGL/D3D12 later) fills one of these and is
// selected at RUNTIME by Fluxion_RHI_CreateInstance's backend parameter --
// unlike Platform's Windows/Linux split (a compile-time #if), every
// backend is compiled into the same RHI library and chosen per-run, which
// is what lets a shipping build support `--graphics=vulkan` and
// `--graphics=d3d12` from the same binary.

#include <Fluxion/RHI/NativeHandle.h>
#include <Fluxion/RHI/RHI.h>

typedef struct FluxionRHIBackendVTable
{
    void (*DestroyInstance)(FluxionRHIInstanceHandle instance);
    u32 (*EnumerateAdapters)(FluxionRHIInstanceHandle instance, FluxionRHIAdapterHandle* outAdapters, u32 maxAdapters);
    bool (*GetAdapterInfo)(FluxionRHIAdapterHandle adapter, FluxionRHIAdapterInfo* outInfo);

    FluxionRHIDeviceHandle (*CreateDevice)(FluxionRHIAdapterHandle adapter, const FluxionRHIDeviceDesc* desc);
    void (*DestroyDevice)(FluxionRHIDeviceHandle device);
    void (*CollectGarbage)(FluxionRHIDeviceHandle device);
    FluxionRHIQueueHandle (*GetQueue)(FluxionRHIDeviceHandle device, FluxionRHIQueueType type);

    FluxionRHICommandListHandle (*CreateCommandList)(FluxionRHIDeviceHandle device, FluxionRHIQueueType type);
    void (*DestroyCommandList)(FluxionRHICommandListHandle commandList);
    void (*CommandListBegin)(FluxionRHICommandListHandle commandList);
    void (*CommandListEnd)(FluxionRHICommandListHandle commandList);
    void (*CommandListBeginRendering)(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc);
    void (*CommandListEndRendering)(FluxionRHICommandListHandle commandList);
    void (*CommandListSetPipeline)(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline);
    void (*CommandListSetVertexBuffer)(FluxionRHICommandListHandle commandList, u32 slot, FluxionRHIBufferHandle buffer, usize offset);
    void (*CommandListSetIndexBuffer)(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle buffer, usize offset, bool use16BitIndices);
    void (*CommandListDraw)(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance);
    void (*CommandListDrawIndexed)(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance);
    void (*CommandListDrawIndirect)(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride);
    void (*CommandListDispatch)(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ);
    void (*CommandListCopyBuffer)(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHIBufferHandle dst, usize dstOffset, usize size);
    void (*CommandListCopyTexture)(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, FluxionRHITextureHandle dst);
    void (*CommandListCopyBufferToTexture)(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHITextureHandle dst, u32 mipLevel, u32 arrayLayer);
    void (*CommandListBarrier)(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount);
    void (*CommandListSetBindGroup)(FluxionRHICommandListHandle commandList, u32 groupIndex, FluxionRHIBindGroupHandle bindGroup);

    void (*QueueSubmit)(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence);

    FluxionRHIBufferHandle (*CreateBuffer)(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc);
    void (*DestroyBuffer)(FluxionRHIBufferHandle buffer);
    void* (*MapBuffer)(FluxionRHIBufferHandle buffer);
    void (*UnmapBuffer)(FluxionRHIBufferHandle buffer);
    FluxionRHITextureHandle (*CreateTexture)(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc);
    void (*DestroyTexture)(FluxionRHITextureHandle texture);
    FluxionRHITextureViewHandle (*CreateTextureView)(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc);
    void (*DestroyTextureView)(FluxionRHITextureViewHandle view);
    FluxionRHISamplerHandle (*CreateSampler)(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc);
    void (*DestroySampler)(FluxionRHISamplerHandle sampler);

    FluxionRHIBindGroupLayoutHandle (*CreateBindGroupLayout)(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc);
    void (*DestroyBindGroupLayout)(FluxionRHIBindGroupLayoutHandle layout);
    FluxionRHIBindGroupHandle (*CreateBindGroup)(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc);
    void (*DestroyBindGroup)(FluxionRHIBindGroupHandle bindGroup);

    FluxionRHIShaderHandle (*CreateShader)(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc);
    void (*DestroyShader)(FluxionRHIShaderHandle shader);
    FluxionRHIPipelineHandle (*CreateGraphicsPipeline)(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc);
    FluxionRHIPipelineHandle (*CreateComputePipeline)(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc);
    void (*DestroyPipeline)(FluxionRHIPipelineHandle pipeline);
    bool (*SavePipelineCacheToFile)(FluxionRHIDeviceHandle device, const char* path);
    bool (*LoadPipelineCacheFromFile)(FluxionRHIDeviceHandle device, const char* path);

    FluxionRHISwapchainHandle (*CreateSwapchain)(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc);
    void (*DestroySwapchain)(FluxionRHISwapchainHandle swapchain);
    u32 (*SwapchainAcquireNextImage)(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore);
    FluxionRHITextureHandle (*SwapchainGetTexture)(FluxionRHISwapchainHandle swapchain, u32 imageIndex);
    void (*SwapchainPresent)(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore);
    void (*SwapchainGetExtent)(FluxionRHISwapchainHandle swapchain, u32* outWidth, u32* outHeight);

    FluxionRHIFenceHandle (*CreateFence)(FluxionRHIDeviceHandle device, bool signaled);
    void (*DestroyFence)(FluxionRHIFenceHandle fence);
    bool (*WaitForFence)(FluxionRHIFenceHandle fence);
    void (*ResetFence)(FluxionRHIFenceHandle fence);
    FluxionRHISemaphoreHandle (*CreateSemaphore)(FluxionRHIDeviceHandle device);
    void (*DestroySemaphore)(FluxionRHISemaphoreHandle semaphore);
    FluxionRHIQueryPoolHandle (*CreateQueryPool)(FluxionRHIDeviceHandle device, u32 queryCount);
    void (*DestroyQueryPool)(FluxionRHIQueryPoolHandle queryPool);

    FluxionRHINativeHandle (*GetNativeDeviceHandle)(FluxionRHIDeviceHandle device);
    FluxionRHINativeHandle (*GetNativeBufferHandle)(FluxionRHIBufferHandle buffer);
    FluxionRHINativeHandle (*GetNativeTextureHandle)(FluxionRHITextureHandle texture);
} FluxionRHIBackendVTable;

// Each backend exposes exactly one of these, matching the naming used in
// Private/Null/NullBackend.c (Fluxion_RHI_Null_CreateInstance) and
// Private/Vulkan/VulkanBackend.c (Fluxion_RHI_Vulkan_CreateInstance).
// Fills outInstance and returns the
// vtable to dispatch every subsequent call on this instance through, or
// NULL (outInstance left invalid) if the backend failed to initialize.
typedef const FluxionRHIBackendVTable* (*FluxionRHIBackendCreateInstanceFn)(const FluxionRHIInstanceDesc* desc, FluxionRHIInstanceHandle* outInstance);
