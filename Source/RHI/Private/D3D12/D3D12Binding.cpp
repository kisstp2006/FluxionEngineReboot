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

// BindGroupLayout/BindGroup. A BindGroupLayout has no persistent native
// object (a D3D12 root signature is built per-PIPELINE from up to 4
// layouts together, in D3D12Pipeline.cpp -- see that file's comment) --
// this file just stores the desc, the same "CPU-side desc only" choice
// the OpenGL backend makes for its own BindGroupLayout. A BindGroup DOES
// own real GPU state: a contiguous range of the device's shared
// CBV_SRV_UAV/Sampler heaps (D3D12Common.h's free-list allocator),
// populated with real CBV/SRV/UAV/sampler descriptors at creation time
// and destroyed through the same retirement queue as every other
// resource here.

#include "D3D12Common.h"

// The same mapping the depth state uses, written out again here rather
// than shared: the pipeline's copy lives in another translation unit,
// and a shared helper would be one more header for eight cases.
static D3D12_COMPARISON_FUNC Fluxion_RHID3D12_MapSamplerCompareOp(FluxionRHICompareOp op)
{
    switch (op)
    {
        case FLUXION_RHI_COMPARE_OP_NEVER: return D3D12_COMPARISON_FUNC_NEVER;
        case FLUXION_RHI_COMPARE_OP_EQUAL: return D3D12_COMPARISON_FUNC_EQUAL;
        case FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER: return D3D12_COMPARISON_FUNC_GREATER;
        case FLUXION_RHI_COMPARE_OP_NOT_EQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER_OR_EQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case FLUXION_RHI_COMPARE_OP_ALWAYS: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_LESS;
    }
}

// --- Layout storage ------------------------------------------------------

static FluxionRHID3D12Slot s_layoutSlots[FLUXION_RHI_D3D12_MAX_BIND_GROUP_LAYOUTS];
static FluxionRHIBindGroupLayoutDesc s_layouts[FLUXION_RHI_D3D12_MAX_BIND_GROUP_LAYOUTS];

FluxionRHIBindGroupLayoutHandle Fluxion_RHID3D12_CreateBindGroupLayout(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc)
{
    FluxionRHIBindGroupLayoutHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHID3D12_ResolveDevice(device) == nullptr || desc == nullptr || desc->entryCount > FLUXION_RHI_MAX_BIND_GROUP_ENTRIES) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_layoutSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUP_LAYOUTS, &index, &generation)) return invalid;
    s_layouts[index] = *desc;

    FluxionRHIBindGroupLayoutHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_DestroyBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_layoutSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUP_LAYOUTS, layout.index, layout.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroyBindGroupLayout called with an invalid or already-destroyed handle");
        return;
    }
    // Not GC-deferred: same reasoning as the OpenGL backend's
    // BindGroupLayout destruction -- there is no real native object here
    // that the GPU could still be reading, only CPU-side desc data used
    // when a PIPELINE (not a layout) is created.
    s_layouts[layout.index] = FluxionRHIBindGroupLayoutDesc{};
    Fluxion_RHID3D12_PoolFree(s_layoutSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUP_LAYOUTS, layout.index, layout.generation);
}

const FluxionRHIBindGroupLayoutDesc* Fluxion_RHID3D12_ResolveBindGroupLayoutDesc(FluxionRHIBindGroupLayoutHandle layout)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_layoutSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUP_LAYOUTS, layout.index, layout.generation)) return nullptr;
    return &s_layouts[layout.index];
}

FluxionRHID3D12LayoutCounts Fluxion_RHID3D12_CountLayoutEntries(const FluxionRHIBindGroupLayoutDesc* layout)
{
    FluxionRHID3D12LayoutCounts counts;
    if (layout == nullptr) return counts;
    for (u32 i = 0; i < layout->entryCount; ++i)
    {
        switch (layout->entries[i].type)
        {
            case FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER: ++counts.cbvCount; break;
            case FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE: ++counts.srvCount; break;
            case FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER: ++counts.srvCount; ++counts.uavCount; break;
            case FLUXION_RHI_BINDING_TYPE_SAMPLER: ++counts.samplerCount; break;
        }
    }
    return counts;
}

// --- BindGroups ------------------------------------------------------------

static FluxionRHID3D12Slot s_bindGroupSlots[FLUXION_RHI_D3D12_MAX_BIND_GROUPS];
static FluxionRHID3D12BindGroup s_bindGroups[FLUXION_RHI_D3D12_MAX_BIND_GROUPS];

FluxionRHIBindGroupHandle Fluxion_RHID3D12_CreateBindGroup(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc)
{
    FluxionRHIBindGroupHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;
    const FluxionRHIBindGroupLayoutDesc* layout = Fluxion_RHID3D12_ResolveBindGroupLayoutDesc(desc->layout);
    if (layout == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_bindGroupSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUPS, &index, &generation)) return invalid;

    // The heap range this BindGroup occupies is always laid out as
    // [CBV block][SRV block][UAV block], sized identically to how
    // D3D12Pipeline.cpp sizes the matching root signature's descriptor
    // table ranges for this same layout -- see
    // Fluxion_RHID3D12_CountLayoutEntries's own comment in D3D12Common.h.
    FluxionRHID3D12LayoutCounts counts = Fluxion_RHID3D12_CountLayoutEntries(layout);
    u32 cbvSrvUavTotal = counts.cbvCount + counts.srvCount + counts.uavCount;

    FluxionRHID3D12BindGroup* bindGroup = &s_bindGroups[index];
    *bindGroup = FluxionRHID3D12BindGroup{};
    bindGroup->layout = desc->layout;

    if (cbvSrvUavTotal > 0 && !Fluxion_RHID3D12_HeapAllocatorAllocate(&deviceState->cbvSrvUavAllocator, cbvSrvUavTotal, &bindGroup->cbvSrvUavRange.offset))
    {
        Fluxion_RHID3D12_PoolFree(s_bindGroupSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUPS, index, generation);
        return invalid;
    }
    bindGroup->cbvSrvUavRange.count = cbvSrvUavTotal;

    if (counts.samplerCount > 0 && !Fluxion_RHID3D12_HeapAllocatorAllocate(&deviceState->samplerAllocator, counts.samplerCount, &bindGroup->samplerRange.offset))
    {
        if (cbvSrvUavTotal > 0) Fluxion_RHID3D12_HeapAllocatorFree(&deviceState->cbvSrvUavAllocator, bindGroup->cbvSrvUavRange.offset, cbvSrvUavTotal);
        Fluxion_RHID3D12_PoolFree(s_bindGroupSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUPS, index, generation);
        return invalid;
    }
    bindGroup->samplerRange.count = counts.samplerCount;

    u32 cbvBase = bindGroup->cbvSrvUavRange.offset;
    u32 srvBase = cbvBase + counts.cbvCount;
    u32 uavBase = srvBase + counts.srvCount;
    u32 cbvCursor = 0, srvCursor = 0, uavCursor = 0, samplerCursor = 0;

    u32 entryCount = desc->entryCount > FLUXION_RHI_MAX_BIND_GROUP_ENTRIES ? FLUXION_RHI_MAX_BIND_GROUP_ENTRIES : desc->entryCount;
    for (u32 i = 0; i < entryCount; ++i)
    {
        const FluxionRHIBindGroupEntry& entry = desc->entries[i];
        if (entry.type == FLUXION_RHI_BINDING_TYPE_SAMPLER)
        {
            const FluxionRHISamplerDesc* samplerDesc = Fluxion_RHID3D12_ResolveSamplerDesc(entry.sampler);
            if (samplerDesc == nullptr) { ++samplerCursor; continue; }
            D3D12_SAMPLER_DESC nativeSampler = {};
            const bool linear = samplerDesc->minFilter == FLUXION_RHI_FILTER_LINEAR && samplerDesc->magFilter == FLUXION_RHI_FILTER_LINEAR;

            // A comparing sampler is a different FILTER here, not a flag
            // beside one: the comparison happens before the filtering,
            // so it is part of what filtering means.
            if (samplerDesc->compareEnable)
            {
                nativeSampler.Filter = linear ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
                nativeSampler.ComparisonFunc = Fluxion_RHID3D12_MapSamplerCompareOp(samplerDesc->compareOp);
            }
            else
            {
                nativeSampler.Filter = linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
            }
            auto mapAddress = [](FluxionRHIAddressMode mode) {
                switch (mode)
                {
                    case FLUXION_RHI_ADDRESS_MODE_MIRRORED_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                    case FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    case FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_BORDER: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                    default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                }
            };
            nativeSampler.AddressU = mapAddress(samplerDesc->addressModeU);
            nativeSampler.AddressV = mapAddress(samplerDesc->addressModeV);
            nativeSampler.AddressW = mapAddress(samplerDesc->addressModeW);
            nativeSampler.MaxAnisotropy = (UINT)(samplerDesc->maxAnisotropy > 1.0f ? samplerDesc->maxAnisotropy : 1.0f);
            nativeSampler.MaxLOD = D3D12_FLOAT32_MAX;
            D3D12_CPU_DESCRIPTOR_HANDLE handle = Fluxion_RHID3D12_HeapCpuHandle(&deviceState->samplerAllocator, bindGroup->samplerRange.offset + samplerCursor);
            deviceState->device->CreateSampler(&nativeSampler, handle);
            ++samplerCursor;
        }
        else if (entry.type == FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER)
        {
            FluxionRHID3D12Buffer* bufferState = Fluxion_RHID3D12_ResolveBuffer(entry.buffer);
            if (bufferState != nullptr)
            {
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
                cbvDesc.BufferLocation = bufferState->resource->GetGPUVirtualAddress() + entry.bufferOffset;
                usize size = entry.bufferSize > 0 ? entry.bufferSize : bufferState->size;
                cbvDesc.SizeInBytes = (UINT)((size + 255) & ~(usize)255); // CBV size must be a 256-byte multiple
                deviceState->device->CreateConstantBufferView(&cbvDesc, Fluxion_RHID3D12_HeapCpuHandle(&deviceState->cbvSrvUavAllocator, cbvBase + cbvCursor));
            }
            ++cbvCursor;
        }
        else if (entry.type == FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE)
        {
            FluxionRHID3D12TextureView* viewState = Fluxion_RHID3D12_ResolveTextureView(entry.textureView);
            FluxionRHID3D12Texture* textureState = viewState != nullptr ? Fluxion_RHID3D12_ResolveTexture(viewState->texture) : nullptr;
            if (textureState != nullptr)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = Fluxion_RHID3D12_MapFormat(viewState->format);
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

                // A cube map has no separate resource shape here -- it is
                // six array slices, and only this view makes it a cube.
                // Which is why the view had to remember being one.
                if (viewState->dimension == FLUXION_RHI_TEXTURE_DIMENSION_CUBE)
                {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    srvDesc.TextureCube.MostDetailedMip = viewState->baseMipLevel;
                    srvDesc.TextureCube.MipLevels = viewState->mipLevelCount;
                    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
                }
                else
                {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MostDetailedMip = viewState->baseMipLevel;
                    srvDesc.Texture2D.MipLevels = viewState->mipLevelCount;
                }
                deviceState->device->CreateShaderResourceView(textureState->resource.Get(), &srvDesc, Fluxion_RHID3D12_HeapCpuHandle(&deviceState->cbvSrvUavAllocator, srvBase + srvCursor));
            }
            ++srvCursor;
        }
        else // FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER -- both an SRV and a UAV are created, see Fluxion_RHID3D12_CountLayoutEntries's comment
        {
            FluxionRHID3D12Buffer* bufferState = Fluxion_RHID3D12_ResolveBuffer(entry.buffer);
            if (bufferState != nullptr)
            {
                usize size = entry.bufferSize > 0 ? entry.bufferSize : bufferState->size;

                // D3D12's structured-buffer views describe a buffer by
                // ELEMENT, not by byte, so they need to know how big one
                // element is. The portable entry now carries that (see
                // FluxionRHIBindGroupEntry.bufferElementStride) -- it
                // used to be assumed to be four, which was true only
                // because a buffer of floats was the only kind anything
                // had ever bound. A buffer of light descriptions is
                // sixty-four bytes an element, and viewed four at a time
                // it reads the right memory in the wrong pieces.
                const UINT elementStride = entry.bufferElementStride != 0 ? (UINT)entry.bufferElementStride : 4u;

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srvDesc.Buffer.FirstElement = entry.bufferOffset / elementStride;
                srvDesc.Buffer.NumElements = (UINT)(size / elementStride);
                srvDesc.Buffer.StructureByteStride = elementStride;
                deviceState->device->CreateShaderResourceView(bufferState->resource.Get(), &srvDesc, Fluxion_RHID3D12_HeapCpuHandle(&deviceState->cbvSrvUavAllocator, srvBase + srvCursor));

                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = DXGI_FORMAT_UNKNOWN;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.FirstElement = entry.bufferOffset / elementStride;
                uavDesc.Buffer.NumElements = (UINT)(size / elementStride);
                uavDesc.Buffer.StructureByteStride = elementStride;
                deviceState->device->CreateUnorderedAccessView(bufferState->resource.Get(), nullptr, &uavDesc, Fluxion_RHID3D12_HeapCpuHandle(&deviceState->cbvSrvUavAllocator, uavBase + uavCursor));
            }
            ++srvCursor;
            ++uavCursor;
        }
    }

    FluxionRHIBindGroupHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_FinalizeBindGroupSlot(u32 index)
{
    FluxionRHID3D12BindGroup* bindGroup = &s_bindGroups[index];
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
    {
        if (bindGroup->cbvSrvUavRange.count > 0) Fluxion_RHID3D12_HeapAllocatorFree(&deviceState->cbvSrvUavAllocator, bindGroup->cbvSrvUavRange.offset, bindGroup->cbvSrvUavRange.count);
        if (bindGroup->samplerRange.count > 0) Fluxion_RHID3D12_HeapAllocatorFree(&deviceState->samplerAllocator, bindGroup->samplerRange.offset, bindGroup->samplerRange.count);
    }
    *bindGroup = FluxionRHID3D12BindGroup{};
    Fluxion_RHID3D12_PoolFinalize(s_bindGroupSlots, index);
}

void Fluxion_RHID3D12_DestroyBindGroup(FluxionRHIBindGroupHandle bindGroup)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_bindGroupSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUPS, bindGroup.index, bindGroup.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroyBindGroup called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHID3D12_PoolRetire(s_bindGroupSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUPS, bindGroup.index, bindGroup.generation);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHID3D12_Retire(deviceState, FluxionRHID3D12RetiredEntry::Kind::BindGroup, bindGroup.index, deviceState->gcCounter);
    else
        Fluxion_RHID3D12_FinalizeBindGroupSlot(bindGroup.index);
}

FluxionRHID3D12BindGroup* Fluxion_RHID3D12_ResolveBindGroup(FluxionRHIBindGroupHandle bindGroup)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_bindGroupSlots, FLUXION_RHI_D3D12_MAX_BIND_GROUPS, bindGroup.index, bindGroup.generation)) return nullptr;
    return &s_bindGroups[bindGroup.index];
}
