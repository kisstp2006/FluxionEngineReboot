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
    FluxionRHIBackendType (*GetDeviceBackendType)(FluxionRHIDeviceHandle device);
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
    void (*CommandListCopyTextureToBuffer)(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, u32 mipLevel, u32 arrayLayer, FluxionRHIBufferHandle dst, usize dstOffset);
    bool (*DeviceIsFormatSupported)(FluxionRHIDeviceHandle device, FluxionRHIFormat format);
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
    void (*CommandListResetQueryPool)(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount);
    void (*CommandListWriteTimestamp)(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 queryIndex);
    bool (*QueryPoolGetResults)(FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount, u64* outTicks);
    u64 (*GetTimestampFrequency)(FluxionRHIDeviceHandle device);

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

// The validation layer (RHIValidation.c): hands back a vtable that
// checks and forwards into `real`. Wrap when the caller asked for
// validation; Unwrap on instance destruction, clearing every piece of
// shadow state so the next instance starts clean.
const FluxionRHIBackendVTable* Fluxion_RHIValidation_Wrap(const FluxionRHIBackendVTable* real);
void Fluxion_RHIValidation_Unwrap(void);
