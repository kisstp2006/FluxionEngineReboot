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

// Command lists, format/resource-state mapping, barriers, queue
// submission, fence/semaphore/query-pool, and the swapchain. Every
// FluxionRHICommandListHandle owns its own ID3D12CommandAllocator +
// ID3D12GraphicsCommandList (mirrors the Vulkan backend's one-
// VkCommandPool-per-command-list design). Resource state tracking
// is explicit -- the caller's FluxionRHIBarrier calls map straight onto
// D3D12_RESOURCE_BARRIER transitions, the same "no automatic tracking"
// choice the Vulkan backend already makes (see D3D12Common.h's own
// comment on this).

#include "D3D12Common.h"

#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Log.h>

#include <cstring>

// --- Format mapping -----------------------------------------------------

DXGI_FORMAT Fluxion_RHID3D12_MapFormat(FluxionRHIFormat format)
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case FLUXION_RHI_FORMAT_R8G8B8A8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case FLUXION_RHI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case FLUXION_RHI_FORMAT_B8G8R8A8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case FLUXION_RHI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case FLUXION_RHI_FORMAT_R32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
        case FLUXION_RHI_FORMAT_R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case FLUXION_RHI_FORMAT_D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
        case FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;

        case FLUXION_RHI_FORMAT_BC4_UNORM: return DXGI_FORMAT_BC4_UNORM;
        case FLUXION_RHI_FORMAT_BC5_UNORM: return DXGI_FORMAT_BC5_UNORM;
        case FLUXION_RHI_FORMAT_BC6H_UFLOAT: return DXGI_FORMAT_BC6H_UF16;
        case FLUXION_RHI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM;
        case FLUXION_RHI_FORMAT_BC7_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;

        // ASTC is deliberately absent, not forgotten: D3D12 has no ASTC
        // formats at all, so there is nothing to map one to. UNKNOWN makes
        // CreateTexture refuse it here, which is the truth -- a cook
        // targeting this backend picks BC instead, and that choice is made
        // where the cook target is known rather than papered over here.
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

FluxionRHIFormat Fluxion_RHID3D12_MapFormatBack(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return FLUXION_RHI_FORMAT_R8G8B8A8_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return FLUXION_RHI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return FLUXION_RHI_FORMAT_B8G8R8A8_SRGB;
        case DXGI_FORMAT_R32_FLOAT: return FLUXION_RHI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R32G32B32_FLOAT: return FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
        case DXGI_FORMAT_R32G32_FLOAT: return FLUXION_RHI_FORMAT_R32G32_FLOAT;
        case DXGI_FORMAT_D32_FLOAT: return FLUXION_RHI_FORMAT_D32_FLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT;

        case DXGI_FORMAT_BC4_UNORM: return FLUXION_RHI_FORMAT_BC4_UNORM;
        case DXGI_FORMAT_BC5_UNORM: return FLUXION_RHI_FORMAT_BC5_UNORM;
        case DXGI_FORMAT_BC6H_UF16: return FLUXION_RHI_FORMAT_BC6H_UFLOAT;
        case DXGI_FORMAT_BC7_UNORM: return FLUXION_RHI_FORMAT_BC7_UNORM;
        case DXGI_FORMAT_BC7_UNORM_SRGB: return FLUXION_RHI_FORMAT_BC7_SRGB;

        default: return FLUXION_RHI_FORMAT_UNKNOWN;
    }
}

D3D12_RESOURCE_STATES Fluxion_RHID3D12_MapResourceState(FluxionRHIResourceState state)
{
    switch (state)
    {
        case FLUXION_RHI_RESOURCE_STATE_COMMON: return D3D12_RESOURCE_STATE_COMMON;
        case FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE: return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION: return D3D12_RESOURCE_STATE_COPY_DEST;
        case FLUXION_RHI_RESOURCE_STATE_VERTEX_BUFFER: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case FLUXION_RHI_RESOURCE_STATE_INDEX_BUFFER: return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case FLUXION_RHI_RESOURCE_STATE_CONSTANT_BUFFER: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case FLUXION_RHI_RESOURCE_STATE_SHADER_READ: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET: return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case FLUXION_RHI_RESOURCE_STATE_DEPTH_READ: return D3D12_RESOURCE_STATE_DEPTH_READ;
        case FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case FLUXION_RHI_RESOURCE_STATE_PRESENT: return D3D12_RESOURCE_STATE_PRESENT;
        default: return D3D12_RESOURCE_STATE_COMMON; // FLUXION_RHI_RESOURCE_STATE_UNDEFINED has no D3D12 equivalent -- COMMON is always a legal starting point
    }
}

// --- Command lists -------------------------------------------------------

struct FluxionRHID3D12CommandList
{
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    FluxionRHIQueueType queueType = FLUXION_RHI_QUEUE_TYPE_GRAPHICS;
    bool recording = false;
    bool insideRendering = false;
    // Set by SetPipeline, consumed by SetBindGroup -- mirrors the
    // Vulkan backend's identical cl->currentPipelineLayout/bindPoint.
    FluxionRHID3D12Pipeline* currentPipeline = nullptr;
};

static FluxionRHID3D12Slot s_commandListSlots[FLUXION_RHI_D3D12_MAX_COMMAND_LISTS];
static FluxionRHID3D12CommandList s_commandLists[FLUXION_RHI_D3D12_MAX_COMMAND_LISTS];

FluxionRHICommandListHandle Fluxion_RHID3D12_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHICommandListHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_commandListSlots, FLUXION_RHI_D3D12_MAX_COMMAND_LISTS, &index, &generation)) return invalid;

    FluxionRHID3D12CommandList* cl = &s_commandLists[index];
    *cl = FluxionRHID3D12CommandList{};
    cl->queueType = type;

    if (FAILED(deviceState->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cl->allocator))))
    {
        Fluxion_RHID3D12_PoolFree(s_commandListSlots, FLUXION_RHI_D3D12_MAX_COMMAND_LISTS, index, generation);
        return invalid;
    }
    if (FAILED(deviceState->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cl->allocator.Get(), nullptr, IID_PPV_ARGS(&cl->list))))
    {
        Fluxion_RHID3D12_PoolFree(s_commandListSlots, FLUXION_RHI_D3D12_MAX_COMMAND_LISTS, index, generation);
        return invalid;
    }
    cl->list->Close(); // created open; Begin() below always starts from a closed+reset state, matching the Vulkan backend's own Begin contract

    FluxionRHICommandListHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_DestroyCommandList(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_commandListSlots, FLUXION_RHI_D3D12_MAX_COMMAND_LISTS, commandList.index, commandList.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroyCommandList called with an invalid or already-destroyed handle");
        return;
    }
    // Not GC-deferred -- same reasoning as the Vulkan backend: a command
    // list is expected to be idle before the caller destroys it.
    s_commandLists[commandList.index] = FluxionRHID3D12CommandList{};
    Fluxion_RHID3D12_PoolFree(s_commandListSlots, FLUXION_RHI_D3D12_MAX_COMMAND_LISTS, commandList.index, commandList.generation);
}

ID3D12GraphicsCommandList* Fluxion_RHID3D12_ResolveCommandList(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_commandListSlots, FLUXION_RHI_D3D12_MAX_COMMAND_LISTS, commandList.index, commandList.generation)) return nullptr;
    return s_commandLists[commandList.index].list.Get();
}

static FluxionRHID3D12CommandList* Fluxion_RHID3D12_RequireRecording(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_commandListSlots, FLUXION_RHI_D3D12_MAX_COMMAND_LISTS, commandList.index, commandList.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: command list handle is invalid or destroyed");
        return nullptr;
    }
    FluxionRHID3D12CommandList* cl = &s_commandLists[commandList.index];
    if (!cl->recording)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: command recorded outside Begin/End");
        return nullptr;
    }
    return cl;
}

void Fluxion_RHID3D12_CommandListBegin(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_commandListSlots, FLUXION_RHI_D3D12_MAX_COMMAND_LISTS, commandList.index, commandList.generation)) return;
    FluxionRHID3D12CommandList* cl = &s_commandLists[commandList.index];
    FLUXION_ASSERT_MSG(!cl->recording, "Fluxion RHI D3D12 backend: Begin called while already recording");

    cl->allocator->Reset();
    cl->list->Reset(cl->allocator.Get(), nullptr);
    cl->recording = true;
    cl->currentPipeline = nullptr;

    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
    {
        ID3D12DescriptorHeap* heaps[2] = { deviceState->cbvSrvUavHeap.Get(), deviceState->samplerHeap.Get() };
        cl->list->SetDescriptorHeaps(2, heaps);
    }
}

void Fluxion_RHID3D12_CommandListEnd(FluxionRHICommandListHandle commandList)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    if (cl == nullptr) return;
    FLUXION_ASSERT_MSG(!cl->insideRendering, "Fluxion RHI D3D12 backend: End called while still inside BeginRendering/EndRendering");
    cl->list->Close();
    cl->recording = false;
}

void Fluxion_RHID3D12_CommandListBeginRendering(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    if (cl == nullptr || desc == nullptr) return;
    FLUXION_ASSERT_MSG(!cl->insideRendering, "Fluxion RHI D3D12 backend: BeginRendering called twice without an EndRendering");

    D3D12_CPU_DESCRIPTOR_HANDLE colorHandles[FLUXION_RHI_MAX_RENDER_TARGETS];
    u32 colorCount = desc->colorAttachmentCount > FLUXION_RHI_MAX_RENDER_TARGETS ? FLUXION_RHI_MAX_RENDER_TARGETS : desc->colorAttachmentCount;
    for (u32 i = 0; i < colorCount; ++i)
    {
        FluxionRHID3D12TextureView* view = Fluxion_RHID3D12_ResolveTextureView(desc->colorAttachments[i].view);
        colorHandles[i] = (view != nullptr && view->hasRtv) ? view->rtv : D3D12_CPU_DESCRIPTOR_HANDLE{};
        if (desc->colorAttachments[i].clear && view != nullptr && view->hasRtv)
            cl->list->ClearRenderTargetView(view->rtv, desc->colorAttachments[i].clearColor, 0, nullptr);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE depthHandle = {};
    bool hasDepth = desc->depthAttachment != nullptr;
    FluxionRHID3D12TextureView* depthView = hasDepth ? Fluxion_RHID3D12_ResolveTextureView(desc->depthAttachment->view) : nullptr;
    if (hasDepth && depthView != nullptr && depthView->hasDsv)
    {
        depthHandle = depthView->dsv;
        if (desc->depthAttachment->clear)
            cl->list->ClearDepthStencilView(depthView->dsv, D3D12_CLEAR_FLAG_DEPTH, desc->depthAttachment->clearColor[0], (UINT8)desc->depthAttachment->clearColor[1], 0, nullptr);
    }

    cl->list->OMSetRenderTargets(colorCount, colorCount > 0 ? colorHandles : nullptr, FALSE, (hasDepth && depthView != nullptr && depthView->hasDsv) ? &depthHandle : nullptr);

    // Every pipeline in this backend uses dynamic viewport/scissor state
    // (D3D12_GRAPHICS_PIPELINE_STATE_DESC never fixes them), same reasoning
    // as the Vulkan backend's identical VK_DYNAMIC_STATE_VIEWPORT/SCISSOR
    // choice: a single full-render-area default, set once per
    // BeginRendering, is exactly what every caller wants today.
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (f32)desc->width, (f32)desc->height, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, (LONG)desc->width, (LONG)desc->height };
    cl->list->RSSetViewports(1, &viewport);
    cl->list->RSSetScissorRects(1, &scissor);

    cl->insideRendering = true;
}

void Fluxion_RHID3D12_CommandListEndRendering(FluxionRHICommandListHandle commandList)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    if (cl == nullptr) return;
    FLUXION_ASSERT_MSG(cl->insideRendering, "Fluxion RHI D3D12 backend: EndRendering called without a matching BeginRendering");
    cl->insideRendering = false; // D3D12 has no explicit "end render pass" call to make -- OMSetRenderTargets at the next BeginRendering simply replaces the bound targets
}

void Fluxion_RHID3D12_CommandListSetPipeline(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12Pipeline* pipelineState = Fluxion_RHID3D12_ResolvePipeline(pipeline);
    if (cl == nullptr || pipelineState == nullptr) return;
    cl->list->SetPipelineState(pipelineState->pipelineState.Get());
    if (pipelineState->isCompute) cl->list->SetComputeRootSignature(pipelineState->rootSignature.Get());
    else
    {
        cl->list->SetGraphicsRootSignature(pipelineState->rootSignature.Get());
        cl->list->IASetPrimitiveTopology(pipelineState->topology);
    }
    cl->currentPipeline = pipelineState;
}

void Fluxion_RHID3D12_CommandListSetBindGroup(FluxionRHICommandListHandle commandList, u32 groupIndex, FluxionRHIBindGroupHandle bindGroup)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12BindGroup* bindGroupState = Fluxion_RHID3D12_ResolveBindGroup(bindGroup);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (cl == nullptr || bindGroupState == nullptr || cl->currentPipeline == nullptr || deviceState == nullptr || groupIndex >= FLUXION_RHI_MAX_BIND_GROUPS) return;

    const FluxionRHID3D12RootParamSlot& slot = cl->currentPipeline->groupSlots[groupIndex];
    bool isCompute = cl->currentPipeline->isCompute;

    if (slot.cbvSrvUavRootParam >= 0 && bindGroupState->cbvSrvUavRange.count > 0)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = Fluxion_RHID3D12_HeapGpuHandle(&deviceState->cbvSrvUavAllocator, deviceState->cbvSrvUavHeap.Get(), bindGroupState->cbvSrvUavRange.offset);
        if (isCompute) cl->list->SetComputeRootDescriptorTable((UINT)slot.cbvSrvUavRootParam, handle);
        else cl->list->SetGraphicsRootDescriptorTable((UINT)slot.cbvSrvUavRootParam, handle);
    }
    if (slot.samplerRootParam >= 0 && bindGroupState->samplerRange.count > 0)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = Fluxion_RHID3D12_HeapGpuHandle(&deviceState->samplerAllocator, deviceState->samplerHeap.Get(), bindGroupState->samplerRange.offset);
        if (isCompute) cl->list->SetComputeRootDescriptorTable((UINT)slot.samplerRootParam, handle);
        else cl->list->SetGraphicsRootDescriptorTable((UINT)slot.samplerRootParam, handle);
    }
}

void Fluxion_RHID3D12_CommandListSetVertexBuffer(FluxionRHICommandListHandle commandList, u32 slot, FluxionRHIBufferHandle buffer, usize offset)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12Buffer* bufferState = Fluxion_RHID3D12_ResolveBuffer(buffer);
    if (cl == nullptr || bufferState == nullptr) return;
    // The RHI contract passes vertex stride via FluxionRHIVertexLayout at
    // pipeline-creation time, not per SetVertexBuffer call -- the
    // currently-bound pipeline (set by a prior SetPipeline in this same
    // recording) is where that stride is stashed.
    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.BufferLocation = bufferState->resource->GetGPUVirtualAddress() + offset;
    view.SizeInBytes = (UINT)(bufferState->size - offset);
    view.StrideInBytes = cl->currentPipeline != nullptr ? cl->currentPipeline->vertexStride : 0;
    cl->list->IASetVertexBuffers(slot, 1, &view);
}

void Fluxion_RHID3D12_CommandListSetIndexBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle buffer, usize offset, bool use16BitIndices)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12Buffer* bufferState = Fluxion_RHID3D12_ResolveBuffer(buffer);
    if (cl == nullptr || bufferState == nullptr) return;
    D3D12_INDEX_BUFFER_VIEW view = {};
    view.BufferLocation = bufferState->resource->GetGPUVirtualAddress() + offset;
    view.SizeInBytes = (UINT)(bufferState->size - offset);
    view.Format = use16BitIndices ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    cl->list->IASetIndexBuffer(&view);
}

void Fluxion_RHID3D12_CommandListDraw(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    if (cl == nullptr) return;
    FLUXION_ASSERT_MSG(cl->insideRendering, "Fluxion RHI D3D12 backend: Draw called outside BeginRendering/EndRendering");
    cl->list->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void Fluxion_RHID3D12_CommandListDrawIndexed(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    if (cl == nullptr) return;
    FLUXION_ASSERT_MSG(cl->insideRendering, "Fluxion RHI D3D12 backend: DrawIndexed called outside BeginRendering/EndRendering");
    cl->list->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void Fluxion_RHID3D12_CommandListDrawIndirect(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride)
{
    FLUXION_UNUSED(stride);
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12Buffer* bufferState = Fluxion_RHID3D12_ResolveBuffer(argsBuffer);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (cl == nullptr || bufferState == nullptr || deviceState == nullptr) return;
    FLUXION_ASSERT_MSG(cl->insideRendering, "Fluxion RHI D3D12 backend: DrawIndirect called outside BeginRendering/EndRendering");

    // A minimal, always-available indirect signature (draw-only, no
    // per-command root-argument updates) -- created lazily and cached
    // for the process lifetime, mirroring how the pipeline cache is
    // lazily created on first use.
    static ComPtr<ID3D12CommandSignature> s_drawIndirectSignature;
    if (s_drawIndirectSignature == nullptr)
    {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
        sigDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        deviceState->device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&s_drawIndirectSignature));
    }
    cl->list->ExecuteIndirect(s_drawIndirectSignature.Get(), drawCount, bufferState->resource.Get(), offset, nullptr, 0);
}

void Fluxion_RHID3D12_CommandListDispatch(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    if (cl == nullptr) return;
    cl->list->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void Fluxion_RHID3D12_CommandListCopyBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHIBufferHandle dst, usize dstOffset, usize size)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12Buffer* srcState = Fluxion_RHID3D12_ResolveBuffer(src);
    FluxionRHID3D12Buffer* dstState = Fluxion_RHID3D12_ResolveBuffer(dst);
    if (cl == nullptr || srcState == nullptr || dstState == nullptr) return;
    cl->list->CopyBufferRegion(dstState->resource.Get(), dstOffset, srcState->resource.Get(), srcOffset, size);
}

void Fluxion_RHID3D12_CommandListCopyTexture(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, FluxionRHITextureHandle dst)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12Texture* srcState = Fluxion_RHID3D12_ResolveTexture(src);
    FluxionRHID3D12Texture* dstState = Fluxion_RHID3D12_ResolveTexture(dst);
    if (cl == nullptr || srcState == nullptr || dstState == nullptr) return;
    cl->list->CopyResource(dstState->resource.Get(), srcState->resource.Get()); // always a full-resource copy, same "no sub-region" contract as CopyBufferToTexture
}

void Fluxion_RHID3D12_CommandListCopyBufferToTexture(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHITextureHandle dst, u32 mipLevel, u32 arrayLayer)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12Buffer* srcState = Fluxion_RHID3D12_ResolveBuffer(src);
    FluxionRHID3D12Texture* dstState = Fluxion_RHID3D12_ResolveTexture(dst);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (cl == nullptr || srcState == nullptr || dstState == nullptr || deviceState == nullptr) return;

    D3D12_RESOURCE_DESC textureDesc = dstState->resource->GetDesc();
    // The standard D3D12CalcSubresource formula (normally pulled in via
    // the community d3dx12.h helper header, which this backend doesn't
    // vendor for a single one-line function) -- MipSlice + ArraySlice *
    // MipLevels + PlaneSlice * MipLevels * ArraySize, PlaneSlice always 0
    // here (no planar/YUV formats in this contract).
    UINT subresource = mipLevel + arrayLayer * textureDesc.MipLevels;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    deviceState->device->GetCopyableFootprints(&textureDesc, subresource, 1, srcOffset, &footprint, nullptr, nullptr, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = dstState->resource.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = subresource;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = srcState->resource.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;

    cl->list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
}

void Fluxion_RHID3D12_CommandListCopyTextureToBuffer(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, u32 mipLevel, u32 arrayLayer, FluxionRHIBufferHandle dst, usize dstOffset)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    FluxionRHID3D12Texture* srcState = Fluxion_RHID3D12_ResolveTexture(src);
    FluxionRHID3D12Buffer* dstState = Fluxion_RHID3D12_ResolveBuffer(dst);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (cl == nullptr || srcState == nullptr || dstState == nullptr || deviceState == nullptr) return;

    D3D12_RESOURCE_DESC textureDesc = srcState->resource->GetDesc();
    UINT subresource = mipLevel + arrayLayer * textureDesc.MipLevels;

    // GetCopyableFootprints is the same source of truth the upload uses,
    // and it already rounds a row up to D3D12's 256-byte pitch -- which is
    // where FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT came from in the first
    // place.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    deviceState->device->GetCopyableFootprints(&textureDesc, subresource, 1, dstOffset, &footprint, nullptr, nullptr, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = dstState->resource.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = srcState->resource.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = subresource;

    cl->list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
}

bool Fluxion_RHID3D12_DeviceIsFormatSupported(FluxionRHIDeviceHandle device, FluxionRHIFormat format)
{
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr) return false;

    const DXGI_FORMAT native = Fluxion_RHID3D12_MapFormat(format);
    if (native == DXGI_FORMAT_UNKNOWN) return false;

    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = native;
    if (FAILED(deviceState->device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) return false;

    return (support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0;
}

void Fluxion_RHID3D12_CommandListBarrier(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount)
{
    FluxionRHID3D12CommandList* cl = Fluxion_RHID3D12_RequireRecording(commandList);
    if (cl == nullptr || barriers == nullptr || barrierCount == 0) return;

    D3D12_RESOURCE_BARRIER nativeBarriers[16];
    u32 count = 0;
    for (u32 i = 0; i < barrierCount && count < 16; ++i)
    {
        ID3D12Resource* resource = nullptr;
        if (FLUXION_HANDLE_IS_VALID(barriers[i].texture))
        {
            FluxionRHID3D12Texture* textureState = Fluxion_RHID3D12_ResolveTexture(barriers[i].texture);
            if (textureState == nullptr) continue;
            resource = textureState->resource.Get();
        }
        else if (FLUXION_HANDLE_IS_VALID(barriers[i].buffer))
        {
            FluxionRHID3D12Buffer* bufferState = Fluxion_RHID3D12_ResolveBuffer(barriers[i].buffer);
            if (bufferState == nullptr) continue;
            resource = bufferState->resource.Get();
        }
        else continue;

        D3D12_RESOURCE_STATES before = Fluxion_RHID3D12_MapResourceState(barriers[i].before);
        D3D12_RESOURCE_STATES after = Fluxion_RHID3D12_MapResourceState(barriers[i].after);
        if (before == after) continue; // D3D12 rejects a same-state transition barrier

        D3D12_RESOURCE_BARRIER& barrier = nativeBarriers[count++];
        barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
    }
    if (count > 0) cl->list->ResourceBarrier(count, nativeBarriers);
}

// --- Submission -------------------------------------------------------------

void Fluxion_RHID3D12_QueueSubmit(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence)
{
    extern ID3D12CommandQueue* Fluxion_RHID3D12_ResolveQueue(FluxionRHIQueueHandle);
    extern FluxionRHID3D12Device* Fluxion_RHID3D12_ResolveDeviceFromQueue(FluxionRHIQueueHandle);
    ID3D12CommandQueue* nativeQueue = Fluxion_RHID3D12_ResolveQueue(queue);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDeviceFromQueue(queue);
    if (nativeQueue == nullptr || deviceState == nullptr) return;

    ID3D12CommandList* lists[FLUXION_RHI_D3D12_MAX_COMMAND_LISTS];
    u32 count = commandListCount > FLUXION_RHI_D3D12_MAX_COMMAND_LISTS ? FLUXION_RHI_D3D12_MAX_COMMAND_LISTS : commandListCount;
    for (u32 i = 0; i < count; ++i) lists[i] = Fluxion_RHID3D12_ResolveCommandList(commandLists[i]);
    nativeQueue->ExecuteCommandLists(count, lists);

    // Every submit signals the device-internal GC fence (see the
    // retirement-queue comment in D3D12Common.h) in addition to whatever
    // user fence was requested.
    u64 gcValue = Fluxion_RHID3D12_NextGCValue(deviceState);
    nativeQueue->Signal(deviceState->gcFence.Get(), gcValue);

    if (FLUXION_HANDLE_IS_VALID(signalFence))
    {
        u64 fenceValue = 0;
        ID3D12Fence* fence = Fluxion_RHID3D12_FenceBeginSignal(signalFence, &fenceValue);
        if (fence != nullptr) nativeQueue->Signal(fence, fenceValue);
    }
}

// --- Synchronization ----------------------------------------------------------

struct FluxionRHID3D12FenceState
{
    ComPtr<ID3D12Fence> fence;
    u64 targetValue = 0;
};

static FluxionRHID3D12Slot s_fenceSlots[FLUXION_RHI_D3D12_MAX_FENCES];
static FluxionRHID3D12FenceState s_fences[FLUXION_RHI_D3D12_MAX_FENCES];

FluxionRHIFenceHandle Fluxion_RHID3D12_CreateFence(FluxionRHIDeviceHandle device, bool signaled)
{
    FluxionRHIFenceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_fenceSlots, FLUXION_RHI_D3D12_MAX_FENCES, &index, &generation)) return invalid;

    FluxionRHID3D12FenceState* fenceState = &s_fences[index];
    *fenceState = FluxionRHID3D12FenceState{};
    if (FAILED(deviceState->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fenceState->fence))))
    {
        Fluxion_RHID3D12_PoolFree(s_fenceSlots, FLUXION_RHI_D3D12_MAX_FENCES, index, generation);
        return invalid;
    }
    if (signaled)
    {
        fenceState->targetValue = 1;
        fenceState->fence->Signal(1);
    }

    FluxionRHIFenceHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_DestroyFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_fenceSlots, FLUXION_RHI_D3D12_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: destroy called with an invalid or already-destroyed fence handle");
        return;
    }
    s_fences[fence.index] = FluxionRHID3D12FenceState{};
    Fluxion_RHID3D12_PoolFree(s_fenceSlots, FLUXION_RHI_D3D12_MAX_FENCES, fence.index, fence.generation);
}

bool Fluxion_RHID3D12_WaitForFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_fenceSlots, FLUXION_RHI_D3D12_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: WaitForFence called with an invalid fence handle");
        return false;
    }
    FluxionRHID3D12FenceState* fenceState = &s_fences[fence.index];
    if (fenceState->fence->GetCompletedValue() >= fenceState->targetValue) return true;
    HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    fenceState->fence->SetEventOnCompletion(fenceState->targetValue, event);
    DWORD waitResult = WaitForSingleObject(event, 4000);
    CloseHandle(event);
    if (waitResult != WAIT_OBJECT_0)
    {
        FLUXION_LOG_ERROR("RHID3D12", "WaitForFence timed out: targetValue=%llu, completedValue=%llu",
            (unsigned long long)fenceState->targetValue, (unsigned long long)fenceState->fence->GetCompletedValue());
        return false;
    }
    return true;
}

void Fluxion_RHID3D12_ResetFence(FluxionRHIFenceHandle fence)
{
    // A D3D12 fence's completed value only ever increases -- "reset" is
    // therefore implicit: the next QueueSubmit that targets this fence
    // (via Fluxion_RHID3D12_FenceBeginSignal) always bumps targetValue
    // past whatever was already signaled, so there's nothing to do here.
    // Mirrors the Vulkan backend's identical timeline-semaphore-based
    // fence, which has the same "no real reset" property.
    if (!Fluxion_RHID3D12_PoolIsValid(s_fenceSlots, FLUXION_RHI_D3D12_MAX_FENCES, fence.index, fence.generation))
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: ResetFence called with an invalid fence handle");
}

ID3D12Fence* Fluxion_RHID3D12_FenceBeginSignal(FluxionRHIFenceHandle fence, u64* outValue)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_fenceSlots, FLUXION_RHI_D3D12_MAX_FENCES, fence.index, fence.generation)) return nullptr;
    FluxionRHID3D12FenceState* fenceState = &s_fences[fence.index];
    *outValue = ++fenceState->targetValue;
    return fenceState->fence.Get();
}

// D3D12 has no separate binary-semaphore object this backend's simple,
// single-in-flight-swapchain-acquire model needs -- IDXGISwapChain's own
// Present/GetCurrentBackBufferIndex already handle GPU-side presentation
// ordering internally, and this backend's AcquireNextImage (like the
// Vulkan backend's) never actually needs a semaphore to signal. Kept as
// a real pool purely so a caller's handle-validity/lifetime code paths
// work identically across every backend.
static FluxionRHID3D12Slot s_semaphoreSlots[FLUXION_RHI_D3D12_MAX_SEMAPHORES];

FluxionRHISemaphoreHandle Fluxion_RHID3D12_CreateSemaphore(FluxionRHIDeviceHandle device)
{
    FluxionRHISemaphoreHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHID3D12_ResolveDevice(device) == nullptr) return handle;
    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_semaphoreSlots, FLUXION_RHI_D3D12_MAX_SEMAPHORES, &index, &generation)) return handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_DestroySemaphore(FluxionRHISemaphoreHandle semaphore)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_semaphoreSlots, FLUXION_RHI_D3D12_MAX_SEMAPHORES, semaphore.index, semaphore.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: destroy called with an invalid or already-destroyed semaphore handle");
        return;
    }
    Fluxion_RHID3D12_PoolFree(s_semaphoreSlots, FLUXION_RHI_D3D12_MAX_SEMAPHORES, semaphore.index, semaphore.generation);
}

// D3D12 query heaps aren't exercised by any caller yet (nothing resolves
// A real ID3D12QueryHeap plus a readback buffer per pool. Unlike Vulkan,
// a query result here is not read out of the heap: the command list has
// to resolve it into an ordinary buffer, so each pool carries a
// READBACK-heap buffer sized one u64 per query, persistently mapped for
// GetResults to copy out of.
struct FluxionRHID3D12QueryPool
{
    ComPtr<ID3D12QueryHeap> heap;
    ComPtr<ID3D12Resource> readback;
    void* mapped = nullptr;
    u32 queryCount = 0;
};

static FluxionRHID3D12Slot s_queryPoolSlots[FLUXION_RHI_D3D12_MAX_QUERY_POOLS];
static FluxionRHID3D12QueryPool s_queryPools[FLUXION_RHI_D3D12_MAX_QUERY_POOLS];

FluxionRHIQueryPoolHandle Fluxion_RHID3D12_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount)
{
    FluxionRHIQueryPoolHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr || queryCount == 0) return handle;
    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_queryPoolSlots, FLUXION_RHI_D3D12_MAX_QUERY_POOLS, &index, &generation)) return handle;

    FluxionRHID3D12QueryPool* pool = &s_queryPools[index];
    *pool = FluxionRHID3D12QueryPool{};

    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = queryCount;
    if (FAILED(deviceState->device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&pool->heap))))
    {
        Fluxion_RHID3D12_PoolFree(s_queryPoolSlots, FLUXION_RHI_D3D12_MAX_QUERY_POOLS, index, generation);
        return handle;
    }

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = (u64)queryCount * sizeof(u64);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(deviceState->device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pool->readback))))
    {
        *pool = FluxionRHID3D12QueryPool{};
        Fluxion_RHID3D12_PoolFree(s_queryPoolSlots, FLUXION_RHI_D3D12_MAX_QUERY_POOLS, index, generation);
        return handle;
    }
    pool->readback->Map(0, nullptr, &pool->mapped);
    pool->queryCount = queryCount;

    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_D3D12_MAX_QUERY_POOLS, queryPool.index, queryPool.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: destroy called with an invalid or already-destroyed query pool handle");
        return;
    }
    FluxionRHID3D12QueryPool* pool = &s_queryPools[queryPool.index];
    if (pool->readback != nullptr && pool->mapped != nullptr) pool->readback->Unmap(0, nullptr);
    *pool = FluxionRHID3D12QueryPool{};
    Fluxion_RHID3D12_PoolFree(s_queryPoolSlots, FLUXION_RHI_D3D12_MAX_QUERY_POOLS, queryPool.index, queryPool.generation);
}

void Fluxion_RHID3D12_CommandListResetQueryPool(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount)
{
    // Nothing to record: a D3D12 timestamp query needs no reset between
    // uses -- EndQuery simply overwrites. The call exists so a caller can
    // write one portable frame loop.
    FLUXION_UNUSED(commandList); FLUXION_UNUSED(queryPool); FLUXION_UNUSED(firstQuery); FLUXION_UNUSED(queryCount);
}

void Fluxion_RHID3D12_CommandListWriteTimestamp(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 queryIndex)
{
    ID3D12GraphicsCommandList* cmd = Fluxion_RHID3D12_ResolveCommandList(commandList);
    if (cmd == nullptr) return;
    if (!Fluxion_RHID3D12_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_D3D12_MAX_QUERY_POOLS, queryPool.index, queryPool.generation)) return;
    FluxionRHID3D12QueryPool* pool = &s_queryPools[queryPool.index];
    if (queryIndex >= pool->queryCount) return;

    cmd->EndQuery(pool->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
    // Resolved immediately, one query at a time. Batching every resolve
    // to the end of the frame would be fewer commands, but this keeps
    // WriteTimestamp self-contained -- there is no "frame end" hook in
    // this contract to hang a batched resolve on.
    cmd->ResolveQueryData(pool->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex, 1, pool->readback.Get(), (u64)queryIndex * sizeof(u64));
}

bool Fluxion_RHID3D12_QueryPoolGetResults(FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount, u64* outTicks)
{
    if (outTicks == nullptr || queryCount == 0) return false;
    if (!Fluxion_RHID3D12_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_D3D12_MAX_QUERY_POOLS, queryPool.index, queryPool.generation)) return false;
    FluxionRHID3D12QueryPool* pool = &s_queryPools[queryPool.index];
    if (pool->mapped == nullptr || firstQuery + queryCount > pool->queryCount) return false;

    // No availability tracking here -- the readback buffer holds whatever
    // the last completed resolve wrote. The contract puts "wait the
    // frame's fence first" on the caller, and this backend cannot check
    // it the way the availability-bit backends can.
    memcpy(outTicks, (const u8*)pool->mapped + (u64)firstQuery * sizeof(u64), (u64)queryCount * sizeof(u64));
    return true;
}

u64 Fluxion_RHID3D12_GetTimestampFrequency(FluxionRHIDeviceHandle device)
{
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr || deviceState->graphicsQueue == nullptr) return 0;
    u64 frequency = 0;
    if (FAILED(deviceState->graphicsQueue->GetTimestampFrequency(&frequency))) return 0;
    return frequency;
}

// --- Swapchain -----------------------------------------------------------

struct FluxionRHID3D12SwapchainState
{
    ComPtr<IDXGISwapChain3> swapChain;
    FluxionRHITextureHandle images[FLUXION_RHI_D3D12_MAX_SWAPCHAIN_IMAGES];
    u32 imageCount = 0;
    u32 width = 0, height = 0;
    bool vsync = true;

    // Kept so the buffers can be resized to follow the window later --
    // the window to measure, and the format and count to recreate with.
    FluxionWindowHandle window = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

// Blocks until the queue has finished everything submitted so far. The
// back buffers cannot be released while the GPU might still be reading
// them, and ResizeBuffers requires every reference to them to be gone.
static void Fluxion_RHID3D12_WaitForGraphicsQueueIdle(FluxionRHID3D12Device* deviceState)
{
    if (deviceState == nullptr || !deviceState->graphicsQueue || !deviceState->gcFence) return;
    const u64 waitValue = ++deviceState->gcCounter;
    deviceState->graphicsQueue->Signal(deviceState->gcFence.Get(), waitValue);
    deviceState->gcFence->SetEventOnCompletion(waitValue, deviceState->gcEvent);
    WaitForSingleObject(deviceState->gcEvent, INFINITE);
}

// Points the swapchain's existing texture slots at the buffers the swap
// chain holds right now. The slots themselves are reused rather than
// reallocated: a caller may be holding a texture handle from an earlier
// frame, and handing back a different one for the same image would turn
// a resize into a dangling handle. Only what is behind the handle moves.
static void Fluxion_RHID3D12_BindSwapchainBuffers(FluxionRHID3D12SwapchainState* state)
{
    for (u32 i = 0; i < state->imageCount; ++i)
    {
        FluxionRHID3D12Texture* textureState = Fluxion_RHID3D12_ResolveTexture(state->images[i]);
        if (textureState == nullptr) continue;

        ComPtr<ID3D12Resource> backbuffer;
        if (FAILED(state->swapChain->GetBuffer(i, IID_PPV_ARGS(&backbuffer)))) continue;

        textureState->resource = backbuffer;
        textureState->format = state->format;
        textureState->width = state->width;
        textureState->height = state->height;
        textureState->usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET;
        textureState->ownedBySwapchain = true;
    }
}

// A swap chain does not follow its window on its own: without this the
// buffers keep their creation size for the life of the program and the
// presented image is simply stretched over whatever the window has
// become. Called from Acquire, which is where a caller is about to
// commit to a size for the frame.
static void Fluxion_RHID3D12_ResizeSwapchainIfNeeded(FluxionRHID3D12SwapchainState* state)
{
    u32 windowWidth = 0, windowHeight = 0;
    Fluxion_Window_GetSize(state->window, &windowWidth, &windowHeight);

    // A minimised window has no size to resize to, and ResizeBuffers
    // rejects zero outright. Recording the zero is what lets GetExtent
    // report "nothing to draw into" rather than the last real size, and
    // what makes the window coming back register as a change again.
    if (windowWidth == 0 || windowHeight == 0)
    {
        state->width = 0;
        state->height = 0;
        return;
    }

    if (windowWidth == state->width && windowHeight == state->height) return;

    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    Fluxion_RHID3D12_WaitForGraphicsQueueIdle(deviceState);

    // Every outstanding reference has to go before ResizeBuffers, and
    // these are the only long-lived ones: a render target view is built
    // per FluxionRHITextureView and released with it, never cached here.
    for (u32 i = 0; i < state->imageCount; ++i)
    {
        FluxionRHID3D12Texture* textureState = Fluxion_RHID3D12_ResolveTexture(state->images[i]);
        if (textureState != nullptr) textureState->resource.Reset();
    }

    // Zero for count and format means "keep what the swap chain already
    // has", which is exactly right -- only the size is changing.
    if (FAILED(state->swapChain->ResizeBuffers(0, windowWidth, windowHeight, DXGI_FORMAT_UNKNOWN, 0)))
    {
        // The old buffers are gone either way, so the slots have to be
        // pointed at whatever the swap chain still has rather than left
        // holding nothing.
        Fluxion_RHID3D12_BindSwapchainBuffers(state);
        return;
    }

    state->width = windowWidth;
    state->height = windowHeight;
    Fluxion_RHID3D12_BindSwapchainBuffers(state);
}

static FluxionRHID3D12Slot s_swapchainSlots[FLUXION_RHI_D3D12_MAX_SWAPCHAINS];
static FluxionRHID3D12SwapchainState s_swapchains[FLUXION_RHI_D3D12_MAX_SWAPCHAINS];

FluxionRHISwapchainHandle Fluxion_RHID3D12_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc)
{
    FluxionRHISwapchainHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr || !FLUXION_HANDLE_IS_VALID(window)) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_swapchainSlots, FLUXION_RHI_D3D12_MAX_SWAPCHAINS, &index, &generation)) return invalid;

    HWND hwnd = (HWND)Fluxion_Window_GetNativeHandle(window).value;
    u32 imageCount = desc->bufferCount;
    if (imageCount == 0) imageCount = 2;
    if (imageCount > FLUXION_RHI_D3D12_MAX_SWAPCHAIN_IMAGES) imageCount = FLUXION_RHI_D3D12_MAX_SWAPCHAIN_IMAGES;

    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.Width = desc->width;
    swapDesc.Height = desc->height;
    swapDesc.Format = Fluxion_RHID3D12_MapFormat(desc->format);
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = imageCount;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(Fluxion_RHID3D12_GetFactory()->CreateSwapChainForHwnd(deviceState->graphicsQueue.Get(), hwnd, &swapDesc, nullptr, nullptr, &swapChain1)))
    {
        Fluxion_RHID3D12_PoolFree(s_swapchainSlots, FLUXION_RHI_D3D12_MAX_SWAPCHAINS, index, generation);
        return invalid;
    }
    Fluxion_RHID3D12_GetFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    FluxionRHID3D12SwapchainState* state = &s_swapchains[index];
    *state = FluxionRHID3D12SwapchainState{};
    swapChain1.As(&state->swapChain);
    state->width = desc->width;
    state->height = desc->height;
    state->vsync = desc->vsync;
    state->window = window;
    state->format = swapDesc.Format;

    for (u32 i = 0; i < imageCount; ++i)
    {
        ComPtr<ID3D12Resource> backbuffer;
        if (FAILED(state->swapChain->GetBuffer(i, IID_PPV_ARGS(&backbuffer)))) break;

        FluxionRHITextureHandle imageHandle;
        u32 textureIndex;
        if (!Fluxion_RHID3D12_AllocateTextureSlot(&imageHandle, &textureIndex)) break;

        FluxionRHID3D12Texture* textureState = Fluxion_RHID3D12_ResolveTexture(imageHandle);
        textureState->resource = backbuffer;
        textureState->format = swapDesc.Format;
        textureState->width = desc->width;
        textureState->height = desc->height;
        textureState->usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET;
        textureState->ownedBySwapchain = true;

        state->images[i] = imageHandle;
        ++state->imageCount;
    }

    FluxionRHISwapchainHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_DestroySwapchain(FluxionRHISwapchainHandle swapchain)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_swapchainSlots, FLUXION_RHI_D3D12_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: destroy called with an invalid or already-destroyed swapchain handle");
        return;
    }
    // Wait for the GPU to be fully idle before releasing the back-buffer
    // resources -- a Present() just issued against this swap chain may
    // still be in flight on the presentation engine/command queue, and
    // final-releasing an ID3D12Resource while GPU work still references
    // it is corruption the D3D12 debug layer treats as fatal (matches
    // the Vulkan backend's own vkDeviceWaitIdle before
    // DestroySwapchainImages).
    Fluxion_RHID3D12_WaitForGraphicsQueueIdle(Fluxion_RHID3D12_SoleDevice());

    FluxionRHID3D12SwapchainState* state = &s_swapchains[swapchain.index];
    for (u32 i = 0; i < state->imageCount; ++i) Fluxion_RHID3D12_FreeTextureSlotDirect(state->images[i]);
    *state = FluxionRHID3D12SwapchainState{};
    Fluxion_RHID3D12_PoolFree(s_swapchainSlots, FLUXION_RHI_D3D12_MAX_SWAPCHAINS, swapchain.index, swapchain.generation);
}

u32 Fluxion_RHID3D12_SwapchainAcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore)
{
    FLUXION_UNUSED(signalSemaphore); // see the semaphore pool's own comment above -- nothing to signal in this backend's model
    if (!Fluxion_RHID3D12_PoolIsValid(s_swapchainSlots, FLUXION_RHI_D3D12_MAX_SWAPCHAINS, swapchain.index, swapchain.generation)) return 0;

    // Here rather than on Present: this is the point at which the caller
    // is about to ask how big the frame is and build its own attachments
    // to match, so the buffers have to already be the size the answer
    // will describe.
    FluxionRHID3D12SwapchainState* state = &s_swapchains[swapchain.index];
    Fluxion_RHID3D12_ResizeSwapchainIfNeeded(state);
    return state->swapChain->GetCurrentBackBufferIndex();
}

FluxionRHITextureHandle Fluxion_RHID3D12_SwapchainGetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex)
{
    FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHID3D12_PoolIsValid(s_swapchainSlots, FLUXION_RHI_D3D12_MAX_SWAPCHAINS, swapchain.index, swapchain.generation)) return invalid;
    FluxionRHID3D12SwapchainState* state = &s_swapchains[swapchain.index];
    if (imageIndex >= state->imageCount) return invalid;
    return state->images[imageIndex];
}

void Fluxion_RHID3D12_SwapchainPresent(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore)
{
    FLUXION_UNUSED(imageIndex);
    FLUXION_UNUSED(waitSemaphore); // see the semaphore pool's own comment above
    if (!Fluxion_RHID3D12_PoolIsValid(s_swapchainSlots, FLUXION_RHI_D3D12_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: Present called with an invalid swapchain handle");
        return;
    }
    FluxionRHID3D12SwapchainState* state = &s_swapchains[swapchain.index];
    state->swapChain->Present(state->vsync ? 1 : 0, 0);
}

void Fluxion_RHID3D12_SwapchainGetExtent(FluxionRHISwapchainHandle swapchain, u32* outWidth, u32* outHeight)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_swapchainSlots, FLUXION_RHI_D3D12_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = 0;
        return;
    }
    // Zero here means the window has no drawable area at all -- see the
    // minimised case in ResizeSwapchainIfNeeded. Reporting the last real
    // size instead would look entirely ordinary to a caller, which would
    // then size a frame to a surface that is not there.
    if (outWidth) *outWidth = s_swapchains[swapchain.index].width;
    if (outHeight) *outHeight = s_swapchains[swapchain.index].height;
}
