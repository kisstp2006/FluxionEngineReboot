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

// Buffer / Texture / TextureView / Sampler. GPU memory goes through
// D3D12MA (the same "vendor the sibling allocator" choice the Vulkan
// backend already made with VMA). Deferred destruction applies here too,
// same as every other pool in this backend -- see the retirement-queue
// comment in D3D12Common.h. A FluxionRHITextureView has no persistent
// native D3D12 object of its own (unlike Vulkan's VkImageView): its RTV/
// DSV descriptors (if the owning texture's usage flags call for them)
// are created directly into the device's non-shader-visible rtvHeap/
// dsvHeap at this view's own pool slot index; its SRV (if ever used in a
// BindGroup) is created on demand straight into the BindGroup's heap
// range in D3D12Binding.cpp, since SRVs must live in a shader-visible
// heap a BindGroup owns, not a per-view one.

#include "D3D12Common.h"

#include <Fluxion/Foundation/Log.h>

static D3D12_HEAP_TYPE Fluxion_RHID3D12_MapHeapType(FluxionRHIMemoryClass memoryClass)
{
    switch (memoryClass)
    {
        case FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU: return D3D12_HEAP_TYPE_UPLOAD;
        case FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU:
        case FLUXION_RHI_MEMORY_CLASS_READBACK: return D3D12_HEAP_TYPE_READBACK;
        default: return D3D12_HEAP_TYPE_DEFAULT; // GPU_ONLY, TRANSIENT
    }
}

static D3D12_RESOURCE_STATES Fluxion_RHID3D12_InitialStateForHeap(D3D12_HEAP_TYPE heapType)
{
    switch (heapType)
    {
        case D3D12_HEAP_TYPE_UPLOAD: return D3D12_RESOURCE_STATE_GENERIC_READ; // mandatory for upload-heap resources
        case D3D12_HEAP_TYPE_READBACK: return D3D12_RESOURCE_STATE_COPY_DEST; // mandatory for readback-heap resources
        default: return D3D12_RESOURCE_STATE_COMMON;
    }
}

// --- Buffers -----------------------------------------------------------------

static FluxionRHID3D12Slot s_bufferSlots[FLUXION_RHI_D3D12_MAX_BUFFERS];
static FluxionRHID3D12Buffer s_buffers[FLUXION_RHI_D3D12_MAX_BUFFERS];

FluxionRHIBufferHandle Fluxion_RHID3D12_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc)
{
    FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr || desc->size == 0) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_bufferSlots, FLUXION_RHI_D3D12_MAX_BUFFERS, &index, &generation))
    {
        // Said rather than returned in silence: a caller handed an
        // invalid handle has to work out for itself whether the pool ran
        // out, the device refused the size, or it asked for nothing at
        // all -- and the three want different answers.
        FLUXION_LOG_ERROR("RHI.D3D12", "no room for another buffer (the pool holds %d)", FLUXION_RHI_D3D12_MAX_BUFFERS);
        return invalid;
    }

    D3D12_HEAP_TYPE heapType = Fluxion_RHID3D12_MapHeapType(desc->memoryClass);

    // A constant-buffer-usable resource's allocated size must itself be
    // a 256-byte multiple -- D3D12_CONSTANT_BUFFER_VIEW_DESC::SizeInBytes
    // is required to be one (and this backend's BindGroup code, see
    // D3D12Binding.cpp, rounds the CBV's SizeInBytes up to the next 256
    // regardless of the caller's actual desc.size), so an unpadded
    // resource smaller than that would have its CBV's BufferLocation +
    // SizeInBytes read past the resource's own GPU VA range -- exactly
    // the D3D12 debug layer's CREATE_CONSTANT_BUFFER_VIEW_INVALID_RESOURCE
    // error this padding avoids.
    usize allocatedSize = desc->size;
    if (desc->usageFlags & FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER)
        allocatedSize = (allocatedSize + 255) & ~(usize)255;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = allocatedSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (desc->usageFlags & FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER)
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = heapType;

    FluxionRHID3D12Buffer* buffer = &s_buffers[index];
    *buffer = FluxionRHID3D12Buffer{};
    buffer->size = desc->size;

    HRESULT hr = deviceState->allocator->CreateResource(&allocDesc, &resourceDesc, Fluxion_RHID3D12_InitialStateForHeap(heapType),
        nullptr, &buffer->allocation, IID_PPV_ARGS(&buffer->resource));
    if (FAILED(hr))
    {
        FLUXION_LOG_ERROR("RHI.D3D12", "a %zu byte buffer (\"%s\") could not be allocated: 0x%08lX", (usize)allocatedSize,
                          desc->debugName != nullptr ? desc->debugName : "unnamed", (unsigned long)hr);
        Fluxion_RHID3D12_PoolFree(s_bufferSlots, FLUXION_RHI_D3D12_MAX_BUFFERS, index, generation);
        return invalid;
    }

    if (heapType != D3D12_HEAP_TYPE_DEFAULT)
        buffer->resource->Map(0, nullptr, &buffer->mappedPointer); // persistently mapped, same pattern as the Vulkan backend's VMA_ALLOCATION_CREATE_MAPPED_BIT buffers

    Fluxion_RHID3D12_SetName(buffer->resource.Get(), desc->debugName);

    FluxionRHIBufferHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_FinalizeBuffer(u32 index)
{
    FluxionRHID3D12Buffer* buffer = &s_buffers[index];
    if (buffer->resource != nullptr && buffer->mappedPointer != nullptr) buffer->resource->Unmap(0, nullptr);
    *buffer = FluxionRHID3D12Buffer{};
    Fluxion_RHID3D12_PoolFinalize(s_bufferSlots, index);
}

void Fluxion_RHID3D12_DestroyBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_bufferSlots, FLUXION_RHI_D3D12_MAX_BUFFERS, buffer.index, buffer.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroyBuffer called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHID3D12_PoolRetire(s_bufferSlots, FLUXION_RHI_D3D12_MAX_BUFFERS, buffer.index, buffer.generation);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHID3D12_Retire(deviceState, FluxionRHID3D12RetiredEntry::Kind::Buffer, buffer.index, deviceState->gcCounter);
    else
        Fluxion_RHID3D12_FinalizeBuffer(buffer.index);
}

void* Fluxion_RHID3D12_MapBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_bufferSlots, FLUXION_RHI_D3D12_MAX_BUFFERS, buffer.index, buffer.generation)) return nullptr;
    return s_buffers[buffer.index].mappedPointer;
}

void Fluxion_RHID3D12_UnmapBuffer(FluxionRHIBufferHandle buffer) { FLUXION_UNUSED(buffer); } // persistently mapped -- nothing to do

FluxionRHID3D12Buffer* Fluxion_RHID3D12_ResolveBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_bufferSlots, FLUXION_RHI_D3D12_MAX_BUFFERS, buffer.index, buffer.generation)) return nullptr;
    return &s_buffers[buffer.index];
}

void Fluxion_RHID3D12_SetName(ID3D12Object* object, const char* name)
{
    if (object == nullptr || name == nullptr || name[0] == '\0') return;
    wchar_t wide[192];
    const int written = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 191);
    if (written <= 0) return;
    wide[191] = L'\0';
    object->SetName(wide);
}

// --- Textures ------------------------------------------------------------

static FluxionRHID3D12Slot s_textureSlots[FLUXION_RHI_D3D12_MAX_TEXTURES];
static FluxionRHID3D12Texture s_textures[FLUXION_RHI_D3D12_MAX_TEXTURES];

FluxionRHITextureHandle Fluxion_RHID3D12_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc)
{
    FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;

    // Asked here even though nothing in this backend's resource needs
    // to know: a cube is six ordinary slices to D3D12, and only the view
    // makes it a cube. So a description with the wrong shape would create
    // perfectly well and fail later, at a view, with nothing left to say
    // which description was wrong. The other backends refuse it at
    // creation, and a rule that held on two backends out of three would
    // be worse than no rule.
    if (desc->dimension == FLUXION_RHI_TEXTURE_DIMENSION_CUBE &&
        (desc->arrayLayers != FLUXION_RHI_CUBE_FACE_COUNT || desc->width != desc->height))
    {
        FLUXION_LOG_ERROR("RHI.D3D12", "a cube texture needs six square layers; this one has %u layers at %ux%u",
            desc->arrayLayers, desc->width, desc->height);
        return invalid;
    }

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_textureSlots, FLUXION_RHI_D3D12_MAX_TEXTURES, &index, &generation)) return invalid;

    DXGI_FORMAT format = Fluxion_RHID3D12_MapFormat(desc->format);

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = desc->depth > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc->width;
    resourceDesc.Height = desc->height;
    resourceDesc.DepthOrArraySize = (UINT16)(desc->depth > 1 ? desc->depth : (desc->arrayLayers > 1 ? desc->arrayLayers : 1));
    resourceDesc.MipLevels = (UINT16)desc->mipLevels;
    // Without a type when it is both drawn into as depth and read as a
    // texture -- see Fluxion_RHID3D12_DepthAsTypeless. The views below
    // each name the way they read it.
    const bool depthAndSampled = (desc->usageFlags & FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL) != 0 &&
                                 (desc->usageFlags & FLUXION_RHI_TEXTURE_USAGE_SAMPLED) != 0;
    resourceDesc.Format = depthAndSampled ? Fluxion_RHID3D12_DepthAsTypeless(format) : format;
    resourceDesc.SampleDesc.Count = desc->sampleCount > 0 ? desc->sampleCount : 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (desc->usageFlags & FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET) resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (desc->usageFlags & FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL) resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (desc->usageFlags & FLUXION_RHI_TEXTURE_USAGE_STORAGE) resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};
    D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
    if (desc->usageFlags & FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL)
    {
        clearValue.Format = format;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValuePtr = &clearValue;
    }
    else if (desc->usageFlags & FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET)
    {
        clearValue.Format = format;
        clearValuePtr = &clearValue;
    }

    FluxionRHID3D12Texture* texture = &s_textures[index];
    *texture = FluxionRHID3D12Texture{};
    texture->format = format;
    texture->width = desc->width;
    texture->height = desc->height;
    texture->depth = desc->depth;
    texture->mipLevels = desc->mipLevels;
    texture->arrayLayers = desc->arrayLayers;
    texture->usageFlags = desc->usageFlags;

    HRESULT hr = deviceState->allocator->CreateResource(&allocDesc, &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
        clearValuePtr, &texture->allocation, IID_PPV_ARGS(&texture->resource));
    if (FAILED(hr))
    {
        Fluxion_RHID3D12_PoolFree(s_textureSlots, FLUXION_RHI_D3D12_MAX_TEXTURES, index, generation);
        return invalid;
    }

    Fluxion_RHID3D12_SetName(texture->resource.Get(), desc->debugName);

    FluxionRHITextureHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_FinalizeTexture(u32 index)
{
    s_textures[index] = FluxionRHID3D12Texture{};
    Fluxion_RHID3D12_PoolFinalize(s_textureSlots, index);
}

void Fluxion_RHID3D12_DestroyTexture(FluxionRHITextureHandle texture)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_textureSlots, FLUXION_RHI_D3D12_MAX_TEXTURES, texture.index, texture.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroyTexture called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHID3D12_PoolRetire(s_textureSlots, FLUXION_RHI_D3D12_MAX_TEXTURES, texture.index, texture.generation);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHID3D12_Retire(deviceState, FluxionRHID3D12RetiredEntry::Kind::Texture, texture.index, deviceState->gcCounter);
    else
        Fluxion_RHID3D12_FinalizeTexture(texture.index);
}

FluxionRHID3D12Texture* Fluxion_RHID3D12_ResolveTexture(FluxionRHITextureHandle texture)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_textureSlots, FLUXION_RHI_D3D12_MAX_TEXTURES, texture.index, texture.generation)) return nullptr;
    return &s_textures[texture.index];
}

bool Fluxion_RHID3D12_AllocateTextureSlot(FluxionRHITextureHandle* outHandle, u32* outIndex)
{
    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_textureSlots, FLUXION_RHI_D3D12_MAX_TEXTURES, &index, &generation)) return false;
    s_textures[index] = FluxionRHID3D12Texture{};
    outHandle->index = index;
    outHandle->generation = generation;
    if (outIndex != nullptr) *outIndex = index;
    return true;
}

void Fluxion_RHID3D12_FreeTextureSlotDirect(FluxionRHITextureHandle handle)
{
    s_textures[handle.index] = FluxionRHID3D12Texture{};
    Fluxion_RHID3D12_PoolFree(s_textureSlots, FLUXION_RHI_D3D12_MAX_TEXTURES, handle.index, handle.generation);
}

// --- Texture views ---------------------------------------------------------

static FluxionRHID3D12Slot s_textureViewSlots[FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS];
static FluxionRHID3D12TextureView s_textureViews[FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS];

FluxionRHITextureViewHandle Fluxion_RHID3D12_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc)
{
    FluxionRHITextureViewHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    FluxionRHID3D12Texture* textureState = desc != nullptr ? Fluxion_RHID3D12_ResolveTexture(desc->texture) : nullptr;
    if (deviceState == nullptr || textureState == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_textureViewSlots, FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS, &index, &generation)) return invalid;

    FluxionRHID3D12TextureView* view = &s_textureViews[index];
    *view = FluxionRHID3D12TextureView{};
    view->texture = desc->texture;
    view->format = desc->format;
    view->baseMipLevel = desc->baseMipLevel;
    view->mipLevelCount = desc->mipLevelCount;
    view->baseArrayLayer = desc->baseArrayLayer;
    view->arrayLayerCount = desc->arrayLayerCount;
    view->dimension = desc->dimension;

    DXGI_FORMAT nativeFormat = Fluxion_RHID3D12_MapFormat(desc->format);

    if (textureState->usageFlags & FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = nativeFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = desc->baseMipLevel;
        view->rtv.ptr = deviceState->rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T)index * deviceState->rtvDescriptorSize;
        deviceState->device->CreateRenderTargetView(textureState->resource.Get(), &rtvDesc, view->rtv);
        view->hasRtv = true;
    }
    if (textureState->usageFlags & FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = nativeFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = desc->baseMipLevel;
        view->dsv.ptr = deviceState->dsvHeap->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T)index * deviceState->dsvDescriptorSize;
        deviceState->device->CreateDepthStencilView(textureState->resource.Get(), &dsvDesc, view->dsv);
        view->hasDsv = true;
    }

    FluxionRHITextureViewHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_FinalizeTextureView(u32 index)
{
    s_textureViews[index] = FluxionRHID3D12TextureView{};
    Fluxion_RHID3D12_PoolFinalize(s_textureViewSlots, index);
}

void Fluxion_RHID3D12_DestroyTextureView(FluxionRHITextureViewHandle view)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_textureViewSlots, FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS, view.index, view.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroyTextureView called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHID3D12_PoolRetire(s_textureViewSlots, FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS, view.index, view.generation);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHID3D12_Retire(deviceState, FluxionRHID3D12RetiredEntry::Kind::TextureView, view.index, deviceState->gcCounter);
    else
        Fluxion_RHID3D12_FinalizeTextureView(view.index);
}

FluxionRHID3D12TextureView* Fluxion_RHID3D12_ResolveTextureView(FluxionRHITextureViewHandle view)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_textureViewSlots, FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS, view.index, view.generation)) return nullptr;
    return &s_textureViews[view.index];
}

// --- Samplers ----------------------------------------------------------------
//
// Unlike Vulkan (a real VkSampler object created once and reused), a
// D3D12 sampler is just a small desc struct written directly into a
// heap slot wherever it's bound -- so this pool stores the desc, and
// D3D12Binding.cpp calls CreateSampler into a BindGroup's heap range at
// bind-group-creation time.

static FluxionRHID3D12Slot s_samplerSlots[FLUXION_RHI_D3D12_MAX_SAMPLERS];
static FluxionRHISamplerDesc s_samplers[FLUXION_RHI_D3D12_MAX_SAMPLERS];

FluxionRHISamplerHandle Fluxion_RHID3D12_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc)
{
    FluxionRHISamplerHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHID3D12_ResolveDevice(device) == nullptr || desc == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_samplerSlots, FLUXION_RHI_D3D12_MAX_SAMPLERS, &index, &generation)) return invalid;
    s_samplers[index] = *desc;

    FluxionRHISamplerHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_FinalizeSampler(u32 index)
{
    s_samplers[index] = FluxionRHISamplerDesc{};
    Fluxion_RHID3D12_PoolFinalize(s_samplerSlots, index);
}

void Fluxion_RHID3D12_DestroySampler(FluxionRHISamplerHandle sampler)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_samplerSlots, FLUXION_RHI_D3D12_MAX_SAMPLERS, sampler.index, sampler.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroySampler called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHID3D12_PoolRetire(s_samplerSlots, FLUXION_RHI_D3D12_MAX_SAMPLERS, sampler.index, sampler.generation);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHID3D12_Retire(deviceState, FluxionRHID3D12RetiredEntry::Kind::Sampler, sampler.index, deviceState->gcCounter);
    else
        Fluxion_RHID3D12_FinalizeSampler(sampler.index);
}

const FluxionRHISamplerDesc* Fluxion_RHID3D12_ResolveSamplerDesc(FluxionRHISamplerHandle sampler)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_samplerSlots, FLUXION_RHI_D3D12_MAX_SAMPLERS, sampler.index, sampler.generation)) return nullptr;
    return &s_samplers[sampler.index];
}

// --- Retirement dispatch ------------------------------------------------

extern void Fluxion_RHID3D12_FinalizeShaderSlot(u32 index);
extern void Fluxion_RHID3D12_FinalizePipelineSlot(u32 index);

void Fluxion_RHID3D12_FinalizeRetired(FluxionRHID3D12Device* deviceState, FluxionRHID3D12RetiredEntry::Kind kind, u32 index)
{
    FLUXION_UNUSED(deviceState);
    switch (kind)
    {
        case FluxionRHID3D12RetiredEntry::Kind::Buffer: Fluxion_RHID3D12_FinalizeBuffer(index); break;
        case FluxionRHID3D12RetiredEntry::Kind::Texture: Fluxion_RHID3D12_FinalizeTexture(index); break;
        case FluxionRHID3D12RetiredEntry::Kind::TextureView: Fluxion_RHID3D12_FinalizeTextureView(index); break;
        case FluxionRHID3D12RetiredEntry::Kind::Sampler: Fluxion_RHID3D12_FinalizeSampler(index); break;
        case FluxionRHID3D12RetiredEntry::Kind::Shader: Fluxion_RHID3D12_FinalizeShaderSlot(index); break;
        case FluxionRHID3D12RetiredEntry::Kind::Pipeline: Fluxion_RHID3D12_FinalizePipelineSlot(index); break;
        case FluxionRHID3D12RetiredEntry::Kind::BindGroup: Fluxion_RHID3D12_FinalizeBindGroupSlot(index); break;
    }
}
