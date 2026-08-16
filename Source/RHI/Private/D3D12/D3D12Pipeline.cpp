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

// Shaders / graphics + compute pipelines / pipeline cache. A D3D12 root
// signature is built once per PIPELINE from up to FLUXION_RHI_MAX_BIND_GROUPS
// BindGroupLayoutHandles together (unlike Vulkan's cached-per-layout
// VkDescriptorSetLayout) -- for each active group, up to two root
// parameters: one descriptor table covering that group's CBV/SRV/UAV
// entries (D3D12 allows mixed range types within a single table), one
// covering its Sampler entries (always a separate table -- D3D12
// requires samplers in a separate heap from CBV/SRV/UAV). A group with
// zero entries of a given class contributes no root parameter for it
// (D3D12 disallows a zero-size descriptor range).

#include "D3D12Common.h"

#include "../PipelineCacheFile.h"

#include <cstdio>
#include <string>
#include <vector>

// --- Shaders -----------------------------------------------------------------

static FluxionRHID3D12Slot s_shaderSlots[FLUXION_RHI_D3D12_MAX_SHADERS];
static FluxionRHID3D12Shader s_shaders[FLUXION_RHI_D3D12_MAX_SHADERS];

FluxionRHIShaderHandle Fluxion_RHID3D12_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc)
{
    FluxionRHIShaderHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHID3D12_ResolveDevice(device) == nullptr || desc == nullptr || desc->bytecode == nullptr || desc->bytecodeSize == 0) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_shaderSlots, FLUXION_RHI_D3D12_MAX_SHADERS, &index, &generation)) return invalid;

    // desc->bytecode is already compiled DXIL (e.g. ShaderProgram.cpp's
    // CompileStage routes it through ShaderCompiler::CompileToDxil
    // for this backend) -- copied verbatim, same as the Vulkan backend
    // copies already-compiled SPIR-V into a VkShaderModule.
    FluxionRHID3D12Shader* shader = &s_shaders[index];
    shader->dxil.assign((const u8*)desc->bytecode, (const u8*)desc->bytecode + desc->bytecodeSize);
    shader->stage = desc->stage;

    FluxionRHIShaderHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_FinalizeShaderSlot(u32 index)
{
    s_shaders[index] = FluxionRHID3D12Shader{};
    Fluxion_RHID3D12_PoolFinalize(s_shaderSlots, index);
}

void Fluxion_RHID3D12_DestroyShader(FluxionRHIShaderHandle shader)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_shaderSlots, FLUXION_RHI_D3D12_MAX_SHADERS, shader.index, shader.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroyShader called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHID3D12_PoolRetire(s_shaderSlots, FLUXION_RHI_D3D12_MAX_SHADERS, shader.index, shader.generation);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHID3D12_Retire(deviceState, FluxionRHID3D12RetiredEntry::Kind::Shader, shader.index, deviceState->gcCounter);
    else
        Fluxion_RHID3D12_FinalizeShaderSlot(shader.index);
}

FluxionRHID3D12Shader* Fluxion_RHID3D12_ResolveShader(FluxionRHIShaderHandle shader)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_shaderSlots, FLUXION_RHI_D3D12_MAX_SHADERS, shader.index, shader.generation)) return nullptr;
    return &s_shaders[shader.index];
}

// --- Root signature ------------------------------------------------------

static bool Fluxion_RHID3D12_BuildRootSignature(FluxionRHID3D12Device* deviceState, const FluxionRHIBindGroupLayoutHandle* bindGroupLayouts, u32 layoutCount,
    ComPtr<ID3D12RootSignature>* outRootSignature, FluxionRHID3D12RootParamSlot outGroupSlots[FLUXION_RHI_MAX_BIND_GROUPS])
{
    std::vector<D3D12_ROOT_PARAMETER1> rootParams;
    // Kept alive until the root signature is actually created below --
    // each table's pDescriptorRanges pointer must stay valid that long.
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE1>> allRanges;
    allRanges.reserve(FLUXION_RHI_MAX_BIND_GROUPS * 2);

    u32 count = layoutCount > FLUXION_RHI_MAX_BIND_GROUPS ? FLUXION_RHI_MAX_BIND_GROUPS : layoutCount;
    for (u32 g = 0; g < count; ++g)
    {
        outGroupSlots[g] = FluxionRHID3D12RootParamSlot{};
        if (!FLUXION_HANDLE_IS_VALID(bindGroupLayouts[g])) continue;
        const FluxionRHIBindGroupLayoutDesc* layout = Fluxion_RHID3D12_ResolveBindGroupLayoutDesc(bindGroupLayouts[g]);
        if (layout == nullptr) return false;

        FluxionRHID3D12LayoutCounts counts = Fluxion_RHID3D12_CountLayoutEntries(layout);

        // One descriptor range PER ENTRY, with BaseShaderRegister set to
        // the entry's own `binding`: a coalesced range starting at 0 only
        // matches shaders whose registers happen to start at 0, which the
        // compiler's numbering does not guarantee. Ranges are emitted
        // CBV-, then SRV-, then UAV-block (entries[] order within each),
        // because OFFSET_APPEND must land on the same table offsets
        // D3D12Binding.cpp writes descriptors to.
        //
        // DATA_VOLATILE on every range: a storage buffer's SRV and UAV
        // ranges share one table, and the buffer legitimately moves
        // between UAV and SRV states within a frame. The default
        // (STATIC_WHILE_SET_AT_EXECUTE) validates state at the
        // SetRootDescriptorTable call; VOLATILE defers it to execution,
        // which is when the state is actually right.
        if (counts.cbvCount + counts.srvCount + counts.uavCount > 0)
        {
            allRanges.emplace_back();
            std::vector<D3D12_DESCRIPTOR_RANGE1>& ranges = allRanges.back();
            for (u32 i = 0; i < layout->entryCount; ++i)
                if (layout->entries[i].type == FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER)
                    ranges.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, layout->entries[i].binding, g, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND });
            for (u32 i = 0; i < layout->entryCount; ++i)
                if (layout->entries[i].type == FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE || layout->entries[i].type == FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER)
                    ranges.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, layout->entries[i].binding, g, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND });
            for (u32 i = 0; i < layout->entryCount; ++i)
                if (layout->entries[i].type == FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER)
                    ranges.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, layout->entries[i].binding, g, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND });

            D3D12_ROOT_PARAMETER1 param = {};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // v1 simplification: no per-entry stage restriction
            param.DescriptorTable.NumDescriptorRanges = (UINT)ranges.size();
            param.DescriptorTable.pDescriptorRanges = ranges.data();
            outGroupSlots[g].cbvSrvUavRootParam = (i32)rootParams.size();
            rootParams.push_back(param);
        }

        if (counts.samplerCount > 0)
        {
            allRanges.emplace_back();
            std::vector<D3D12_DESCRIPTOR_RANGE1>& ranges = allRanges.back();
            for (u32 i = 0; i < layout->entryCount; ++i)
                if (layout->entries[i].type == FLUXION_RHI_BINDING_TYPE_SAMPLER)
                    ranges.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, layout->entries[i].binding, g, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND });

            D3D12_ROOT_PARAMETER1 param = {};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            param.DescriptorTable.NumDescriptorRanges = (UINT)ranges.size();
            param.DescriptorTable.pDescriptorRanges = ranges.data();
            outGroupSlots[g].samplerRootParam = (i32)rootParams.size();
            rootParams.push_back(param);
        }
    }

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1.NumParameters = (UINT)rootParams.size();
    versionedDesc.Desc_1_1.pParameters = rootParams.empty() ? nullptr : rootParams.data();
    versionedDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized, error;
    if (FAILED(D3D12SerializeVersionedRootSignature(&versionedDesc, &serialized, &error))) return false;
    // outRootSignature is a ComPtr<T>* (pointer to the caller's ComPtr),
    // not a T** -- IID_PPV_ARGS_Helper has no overload for that, only for
    // T** and ComPtr<T>& (via ComPtr::operator&), so dereference first to
    // get the ComPtr<T> lvalue IID_PPV_ARGS expects.
    return SUCCEEDED(deviceState->device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&*outRootSignature)));
}

// --- Pipeline cache ------------------------------------------------------
//
// One ID3D12PipelineLibrary per device, populated as pipelines are
// created -- the same explicit save/load-to-file shape as the Vulkan
// backend's VkPipelineCache, just backed by a different native D3D12
// object. Named by a pipeline's own debugName (or an auto-generated
// unique name if none was given, in which case it's never actually
// found again across a save/load cycle -- a caller that wants its
// pipelines to benefit from a loaded cache should pass a debugName).

// outSeeded, where the caller passed one, says whether the library
// actually came up on the supplied bytes. Falling back to an empty
// library is the right behaviour -- a rejected blob must not fail
// pipeline creation -- but reporting that fallback as success is what
// made a load of an unusable file indistinguishable from a load of a good
// one. The two answers are now separate: the function still succeeds, and
// the caller still learns it started cold.
static bool Fluxion_RHID3D12_EnsurePipelineLibrary(FluxionRHID3D12Device* deviceState, const void* initialData, usize initialDataSize, bool* outSeeded = nullptr)
{
    if (outSeeded != nullptr) *outSeeded = false;
    if (deviceState->pipelineLibrary != nullptr) return initialData == nullptr; // same "can't reseed an existing library" contract as the Vulkan backend

    if (initialData != nullptr)
    {
        // The runtime does its own driver-version and adapter check on
        // these bytes, on top of the header check the caller already did.
        HRESULT hr = deviceState->device->CreatePipelineLibrary(initialData, initialDataSize, IID_PPV_ARGS(&deviceState->pipelineLibrary));
        if (SUCCEEDED(hr))
        {
            if (outSeeded != nullptr) *outSeeded = true;
            return true;
        }
    }

    return SUCCEEDED(deviceState->device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&deviceState->pipelineLibrary)));
}

static std::wstring Fluxion_RHID3D12_PipelineCacheKey(const char* debugName, u32 poolIndex)
{
    if (debugName == nullptr || debugName[0] == '\0')
        return L"FluxionAnonymousPipeline" + std::to_wstring(poolIndex);
    int length = MultiByteToWideChar(CP_UTF8, 0, debugName, -1, nullptr, 0);
    std::wstring wide((size_t)length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, debugName, -1, wide.data(), length);
    return wide;
}

// D3D12 exposes no driver version through DXGI, so driverVersion stays
// zero here and the runtime's own check inside CreatePipelineLibrary
// covers that case instead -- which is now reported rather than swallowed
// (see EnsurePipelineLibrary above). The adapter ids still have to match
// before the runtime is asked at all.
static FluxionRHIPipelineCacheIdentity Fluxion_RHID3D12_PipelineCacheIdentity(FluxionRHID3D12Device* deviceState)
{
    FluxionRHIPipelineCacheIdentity identity = {};
    identity.backend = FLUXION_RHI_BACKEND_D3D12;

    DXGI_ADAPTER_DESC1 desc = {};
    if (deviceState->adapter != nullptr && SUCCEEDED(deviceState->adapter->GetDesc1(&desc)))
    {
        identity.vendorId = desc.VendorId;
        identity.deviceId = desc.DeviceId;
    }
    return identity;
}

bool Fluxion_RHID3D12_SavePipelineCacheToFile(FluxionRHIDeviceHandle device, const char* path)
{
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr || path == nullptr || deviceState->pipelineLibrary == nullptr) return false;

    SIZE_T size = deviceState->pipelineLibrary->GetSerializedSize();
    if (size == 0) return false;
    std::vector<u8> data(size);
    if (FAILED(deviceState->pipelineLibrary->Serialize(data.data(), size))) return false;

    return Fluxion_RHIPipelineCacheFile_Write(path, Fluxion_RHID3D12_PipelineCacheIdentity(deviceState), data.data(), size);
}

bool Fluxion_RHID3D12_LoadPipelineCacheFromFile(FluxionRHIDeviceHandle device, const char* path)
{
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    if (deviceState == nullptr || path == nullptr) return false;

    std::vector<u8> data;
    if (!Fluxion_RHIPipelineCacheFile_Read(path, Fluxion_RHID3D12_PipelineCacheIdentity(deviceState), &data)) return false;

    // Handed to the device state first, because the library will keep
    // reading out of whatever pointer it is given for as long as it
    // exists -- a local buffer would be gone before the first lookup.
    deviceState->pipelineLibraryBlob = std::move(data);

    bool seeded = false;
    if (!Fluxion_RHID3D12_EnsurePipelineLibrary(deviceState, deviceState->pipelineLibraryBlob.data(), deviceState->pipelineLibraryBlob.size(), &seeded)) return false;
    if (!seeded) deviceState->pipelineLibraryBlob.clear();
    return seeded;
}

// --- Pipelines -----------------------------------------------------------

static FluxionRHID3D12Slot s_pipelineSlots[FLUXION_RHI_D3D12_MAX_PIPELINES];
static FluxionRHID3D12Pipeline s_pipelines[FLUXION_RHI_D3D12_MAX_PIPELINES];

static D3D12_CULL_MODE Fluxion_RHID3D12_MapCullMode(FluxionRHICullMode mode)
{
    switch (mode)
    {
        case FLUXION_RHI_CULL_MODE_FRONT: return D3D12_CULL_MODE_FRONT;
        case FLUXION_RHI_CULL_MODE_BACK: return D3D12_CULL_MODE_BACK;
        default: return D3D12_CULL_MODE_NONE;
    }
}

static D3D12_COMPARISON_FUNC Fluxion_RHID3D12_MapCompareOp(FluxionRHICompareOp op)
{
    switch (op)
    {
        case FLUXION_RHI_COMPARE_OP_LESS: return D3D12_COMPARISON_FUNC_LESS;
        case FLUXION_RHI_COMPARE_OP_EQUAL: return D3D12_COMPARISON_FUNC_EQUAL;
        case FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER: return D3D12_COMPARISON_FUNC_GREATER;
        case FLUXION_RHI_COMPARE_OP_NOT_EQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER_OR_EQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case FLUXION_RHI_COMPARE_OP_ALWAYS: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_NEVER;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE Fluxion_RHID3D12_MapTopologyType(FluxionRHIPrimitiveTopology topology)
{
    switch (topology)
    {
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_LINE_LIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_POINT_LIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY Fluxion_RHID3D12_MapTopology(FluxionRHIPrimitiveTopology topology)
{
    switch (topology)
    {
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_LINE_LIST: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_POINT_LIST: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        default: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

FluxionRHIPipelineHandle Fluxion_RHID3D12_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    FluxionRHID3D12Shader* vs = desc != nullptr ? Fluxion_RHID3D12_ResolveShader(desc->vertexShader) : nullptr;
    FluxionRHID3D12Shader* fs = desc != nullptr ? Fluxion_RHID3D12_ResolveShader(desc->fragmentShader) : nullptr;
    if (deviceState == nullptr || desc == nullptr || vs == nullptr || fs == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, &index, &generation)) return invalid;

    FluxionRHID3D12Pipeline* pipeline = &s_pipelines[index];
    *pipeline = FluxionRHID3D12Pipeline{};
    pipeline->isCompute = false;
    pipeline->topology = Fluxion_RHID3D12_MapTopology(desc->topology);
    pipeline->vertexStride = desc->vertexLayout.stride;

    if (!Fluxion_RHID3D12_BuildRootSignature(deviceState, desc->bindGroupLayouts, desc->bindGroupLayoutCount, &pipeline->rootSignature, pipeline->groupSlots))
    {
        Fluxion_RHID3D12_PoolFree(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, index, generation);
        return invalid;
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements(desc->vertexLayout.attributeCount);
    for (u32 i = 0; i < desc->vertexLayout.attributeCount; ++i)
    {
        inputElements[i] = {};
        inputElements[i].SemanticName = "TEXCOORD"; // matches HLSLBackend.cpp's StageInput field semantics
        inputElements[i].SemanticIndex = desc->vertexLayout.attributes[i].location;
        inputElements[i].Format = Fluxion_RHID3D12_MapFormat(desc->vertexLayout.attributes[i].format);
        inputElements[i].AlignedByteOffset = desc->vertexLayout.attributes[i].offset;
        inputElements[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pipeline->rootSignature.Get();
    psoDesc.VS = { vs->dxil.data(), vs->dxil.size() };
    psoDesc.PS = { fs->dxil.data(), fs->dxil.size() };
    psoDesc.InputLayout = { inputElements.empty() ? nullptr : inputElements.data(), (UINT)inputElements.size() };
    psoDesc.PrimitiveTopologyType = Fluxion_RHID3D12_MapTopologyType(desc->topology);

    psoDesc.RasterizerState.FillMode = desc->rasterState.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = Fluxion_RHID3D12_MapCullMode(desc->rasterState.cullMode);
    // Inverted, not a direct passthrough -- the same inversion the OpenGL
    // and Vulkan backends make, for the same reason: what the portable
    // flag calls the front is the opposite of what these rasterizers call
    // it, given how the geometry reaching them is wound. Three backends
    // agreeing is what matters; the flag's own sense is the thing that is
    // off, and correcting it belongs where the flag is set, not here.
    psoDesc.RasterizerState.FrontCounterClockwise = desc->rasterState.frontFaceCounterClockwise ? FALSE : TRUE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable = desc->depthState.testEnable ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = desc->depthState.writeEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = Fluxion_RHID3D12_MapCompareOp(desc->depthState.compareOp);

    // IndependentBlendEnable stays FALSE (default), so RenderTarget[0]'s
    // desc applies uniformly to every active render target -- matches
    // the RHI contract's single FluxionRHIBlendState (no per-target
    // control yet, same v1 scope as the Vulkan/OpenGL backends).
    D3D12_RENDER_TARGET_BLEND_DESC& rt0 = psoDesc.BlendState.RenderTarget[0];
    rt0.BlendEnable = desc->blendState.blendEnable ? TRUE : FALSE;
    rt0.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOp = D3D12_BLEND_OP_ADD;
    rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.NumRenderTargets = desc->colorFormatCount;
    for (u32 i = 0; i < desc->colorFormatCount; ++i) psoDesc.RTVFormats[i] = Fluxion_RHID3D12_MapFormat(desc->colorFormats[i]);
    psoDesc.DSVFormat = desc->depthFormat != FLUXION_RHI_FORMAT_UNKNOWN ? Fluxion_RHID3D12_MapFormat(desc->depthFormat) : DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;

    Fluxion_RHID3D12_EnsurePipelineLibrary(deviceState, nullptr, 0);
    std::wstring key = Fluxion_RHID3D12_PipelineCacheKey(desc->debugName, index);
    HRESULT hr = deviceState->pipelineLibrary->LoadGraphicsPipeline(key.c_str(), &psoDesc, IID_PPV_ARGS(&pipeline->pipelineState));
    if (FAILED(hr))
    {
        hr = deviceState->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pipelineState));
        if (SUCCEEDED(hr)) deviceState->pipelineLibrary->StorePipeline(key.c_str(), pipeline->pipelineState.Get()); // best-effort; a name collision here is harmless, just skips caching
    }
    if (FAILED(hr))
    {
        Fluxion_RHID3D12_PoolFree(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, index, generation);
        return invalid;
    }

    Fluxion_RHID3D12_SetName(pipeline->pipelineState.Get(), desc->debugName);

    FluxionRHIPipelineHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

FluxionRHIPipelineHandle Fluxion_RHID3D12_CreateComputePipeline(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_ResolveDevice(device);
    FluxionRHID3D12Shader* cs = desc != nullptr ? Fluxion_RHID3D12_ResolveShader(desc->computeShader) : nullptr;
    if (deviceState == nullptr || desc == nullptr || cs == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHID3D12_PoolAllocate(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, &index, &generation)) return invalid;

    FluxionRHID3D12Pipeline* pipeline = &s_pipelines[index];
    *pipeline = FluxionRHID3D12Pipeline{};
    pipeline->isCompute = true;

    if (!Fluxion_RHID3D12_BuildRootSignature(deviceState, desc->bindGroupLayouts, desc->bindGroupLayoutCount, &pipeline->rootSignature, pipeline->groupSlots))
    {
        Fluxion_RHID3D12_PoolFree(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, index, generation);
        return invalid;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pipeline->rootSignature.Get();
    psoDesc.CS = { cs->dxil.data(), cs->dxil.size() };

    Fluxion_RHID3D12_EnsurePipelineLibrary(deviceState, nullptr, 0);
    std::wstring key = Fluxion_RHID3D12_PipelineCacheKey(desc->debugName, index);
    HRESULT hr = deviceState->pipelineLibrary->LoadComputePipeline(key.c_str(), &psoDesc, IID_PPV_ARGS(&pipeline->pipelineState));
    if (FAILED(hr))
    {
        hr = deviceState->device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pipelineState));
        if (SUCCEEDED(hr)) deviceState->pipelineLibrary->StorePipeline(key.c_str(), pipeline->pipelineState.Get());
    }
    if (FAILED(hr))
    {
        Fluxion_RHID3D12_PoolFree(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, index, generation);
        return invalid;
    }

    Fluxion_RHID3D12_SetName(pipeline->pipelineState.Get(), desc->debugName);

    FluxionRHIPipelineHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHID3D12_FinalizePipelineSlot(u32 index)
{
    s_pipelines[index] = FluxionRHID3D12Pipeline{};
    Fluxion_RHID3D12_PoolFinalize(s_pipelineSlots, index);
}

void Fluxion_RHID3D12_DestroyPipeline(FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, pipeline.index, pipeline.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI D3D12 backend: DestroyPipeline called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHID3D12_PoolRetire(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, pipeline.index, pipeline.generation);
    FluxionRHID3D12Device* deviceState = Fluxion_RHID3D12_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHID3D12_Retire(deviceState, FluxionRHID3D12RetiredEntry::Kind::Pipeline, pipeline.index, deviceState->gcCounter);
    else
        Fluxion_RHID3D12_FinalizePipelineSlot(pipeline.index);
}

FluxionRHID3D12Pipeline* Fluxion_RHID3D12_ResolvePipeline(FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHID3D12_PoolIsValid(s_pipelineSlots, FLUXION_RHI_D3D12_MAX_PIPELINES, pipeline.index, pipeline.generation)) return nullptr;
    return &s_pipelines[pipeline.index];
}
