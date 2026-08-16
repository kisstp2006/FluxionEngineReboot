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

// The environment blurred per roughness, and the split-sum table.
//
// Both run on the device but write buffers, because a compute pass here
// has no other way to write; recorded copies turn the buffers into the
// textures the surfaces sample. The chain runs when the environment
// changes; the table runs once per view, ever.

#include "RendererInternal.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#include <cstring>

namespace
{

const char* const kPrefilterSource = "#include \"Fluxion/Pass/SpecularPrefilter.jsl\"\n";
const char* const kDfgSource = "#include \"Fluxion/Pass/DfgIntegrate.jsl\"\n";

// How wide the compute groups the shader compiler emits are; a dispatch
// is the work rounded up to whole groups.
constexpr u32 kComputeGroupWidth = 64;

// One parameter slot per prefiltered mip, then one for the table pass.
// Slots are as far apart as the strictest backend's uniform-offset rule
// demands, which costs a few hundred spare bytes once.
constexpr usize kParamsSlotBytes = 256;
constexpr u32 kParamsSlotCount = FLUXION_RENDERER_PREFILTERED_MIPS + 1;
constexpr u32 kDfgParamsSlot = FLUXION_RENDERER_PREFILTERED_MIPS;

u32 MipWidth(u32 mip)
{
    return FLUXION_RENDERER_PREFILTERED_SIZE >> mip;
}

// The buffer layout the copies read: rows spaced to the copy row
// alignment, faces spaced to the copy placement alignment. The shader
// writes with the same strides, handed over in its parameters.
usize RowStrideBytes(u32 width)
{
    const usize tight = (usize)width * sizeof(FluxionVec4);
    return tight > FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT ? tight : FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
}

usize FaceStrideBytes(u32 width)
{
    const usize tight = (usize)width * RowStrideBytes(width);
    return tight > FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT ? tight : FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
}

// A texture-and-sampler-and-storage layout for the chain, a
// uniform-and-storage one for the table -- numbered the way the shader
// compiler numbers them: the uniform buffer first, each texture as a
// pair, storage buffers last.
FluxionRHIBindGroupLayoutDesc MakePrefilterLayoutDesc()
{
    FluxionRHIBindGroupLayoutDesc desc = { };

    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entries[1].binding = 1;
    desc.entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entries[2].binding = 2;
    desc.entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[2].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entries[3].binding = 3;
    desc.entries[3].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[3].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entryCount = 4;
    desc.debugName = "Fluxion.Renderer.PrefilterBindGroupLayout";
    return desc;
}

FluxionRHIBindGroupLayoutDesc MakeDfgLayoutDesc()
{
    FluxionRHIBindGroupLayoutDesc desc = { };

    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entries[1].binding = 1;
    desc.entries[1].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entryCount = 2;
    desc.debugName = "Fluxion.Renderer.DfgBindGroupLayout";
    return desc;
}

bool BuildComputePipeline(FluxionRHIDeviceHandle device, const char* source, const char* name,
                          const FluxionRHIBindGroupLayoutDesc* layoutDesc,
                          FluxionShaderProgramHandle* outProgram, FluxionRHIBindGroupLayoutHandle* outLayout,
                          FluxionRHIPipelineHandle* outPipeline)
{
    FluxionShaderProgramDesc programDesc;
    std::memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = name;
    programDesc.computeSource = source;

    *outProgram = Fluxion_ShaderProgram_Create(device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(*outProgram)) return false;

    *outLayout = Fluxion_RHI_CreateBindGroupLayout(device, layoutDesc);
    if (!FLUXION_HANDLE_IS_VALID(*outLayout)) return false;

    FluxionRHIComputePipelineDesc pipelineDesc;
    std::memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.computeShader = FluxionRendererInternal_ShaderProgram_GetComputeShader(*outProgram);
    pipelineDesc.bindGroupLayouts[0] = *outLayout;
    pipelineDesc.bindGroupLayoutCount = 1;
    pipelineDesc.debugName = name;

    *outPipeline = Fluxion_RHI_CreateComputePipeline(device, &pipelineDesc);
    return FLUXION_HANDLE_IS_VALID(*outPipeline);
}

bool EnsureResources(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->dfgPipeline)) return true;
    if (renderer->prefilterFailed) return false;

    const FluxionRHIBindGroupLayoutDesc prefilterLayoutDesc = MakePrefilterLayoutDesc();
    const FluxionRHIBindGroupLayoutDesc dfgLayoutDesc = MakeDfgLayoutDesc();

    if (!BuildComputePipeline(renderer->device, kPrefilterSource, "Fluxion.Renderer.SpecularPrefilter",
                              &prefilterLayoutDesc, &renderer->prefilterProgram, &renderer->prefilterLayout,
                              &renderer->prefilterPipeline) ||
        !BuildComputePipeline(renderer->device, kDfgSource, "Fluxion.Renderer.DfgIntegrate",
                              &dfgLayoutDesc, &renderer->dfgProgram, &renderer->dfgLayout,
                              &renderer->dfgPipeline))
    {
        // Said once. A shader that would not build will not build again,
        // and repeating it buries whatever said it first.
        FLUXION_LOG_ERROR("Renderer", "The environment prefilter shaders could not be built; reflections will stay black.");
        renderer->prefilterFailed = true;
        return false;
    }

    // Every parameter every dispatch will ever need, written once from
    // here: a buffer updated BETWEEN dispatches on one command list would
    // hand every dispatch the final value.
    FluxionRHIBufferDesc paramsDesc;
    paramsDesc.size = kParamsSlotBytes * kParamsSlotCount;
    paramsDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    paramsDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    paramsDesc.debugName = "Fluxion.Renderer.EnvironmentParams";
    renderer->environmentParamsBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &paramsDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->environmentParamsBuffer))
    {
        renderer->prefilterFailed = true;
        return false;
    }

    u8* mapped = (u8*)Fluxion_RHI_MapBuffer(renderer->environmentParamsBuffer);
    if (mapped == nullptr)
    {
        renderer->prefilterFailed = true;
        return false;
    }

    std::memset(mapped, 0, paramsDesc.size);
    for (u32 mip = 0; mip < FLUXION_RENDERER_PREFILTERED_MIPS; ++mip)
    {
        const u32 width = MipWidth(mip);
        f32* slot = (f32*)(mapped + (usize)mip * kParamsSlotBytes);
        slot[0] = (f32)width;
        slot[1] = (f32)mip / (f32)(FLUXION_RENDERER_PREFILTERED_MIPS - 1);
        slot[2] = (f32)(RowStrideBytes(width) / sizeof(FluxionVec4));
        slot[3] = (f32)(FaceStrideBytes(width) / sizeof(FluxionVec4));
    }
    f32* dfgSlot = (f32*)(mapped + (usize)kDfgParamsSlot * kParamsSlotBytes);
    dfgSlot[0] = (f32)FLUXION_RENDERER_DFG_SIZE;
    dfgSlot[1] = (f32)(RowStrideBytes(FLUXION_RENDERER_DFG_SIZE) / sizeof(FluxionVec4));
    Fluxion_RHI_UnmapBuffer(renderer->environmentParamsBuffer);

    // Sized for the largest mip's six faces; every other user fits under
    // that. GPU-only: the compute pass writes it and the copies read it,
    // nothing on the CPU ever looks.
    FluxionRHIBufferDesc scratchDesc;
    scratchDesc.size = 6 * FaceStrideBytes(FLUXION_RENDERER_PREFILTERED_SIZE);
    scratchDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    scratchDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    scratchDesc.debugName = "Fluxion.Renderer.EnvironmentScratch";
    renderer->environmentScratchBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &scratchDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->environmentScratchBuffer))
    {
        renderer->prefilterFailed = true;
        return false;
    }

    return true;
}

// One bind group per mip, all pointing at the same environment; the
// parameter slot is what differs. Rebuilt whenever the environment does.
bool RebuildPrefilterBindGroups(FluxionRenderer* renderer, FluxionRHITextureViewHandle environment,
                                FluxionRHISamplerHandle sampler)
{
    const FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHISamplerHandle noSampler = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBindGroupHandle fresh[FLUXION_RENDERER_PREFILTERED_MIPS];

    for (u32 mip = 0; mip < FLUXION_RENDERER_PREFILTERED_MIPS; ++mip)
    {
        FluxionRHIBindGroupEntry entries[4];
        std::memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->environmentParamsBuffer;
        entries[0].bufferOffset = (usize)mip * kParamsSlotBytes;
        entries[0].bufferSize = sizeof(FluxionVec4);
        entries[0].textureView = noView;
        entries[0].sampler = noSampler;

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[1].buffer = noBuffer;
        entries[1].textureView = environment;
        entries[1].sampler = noSampler;

        entries[2].binding = 2;
        entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[2].buffer = noBuffer;
        entries[2].textureView = noView;
        entries[2].sampler = sampler;

        entries[3].binding = 3;
        entries[3].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
        entries[3].buffer = renderer->environmentScratchBuffer;
        entries[3].bufferSize = 6 * FaceStrideBytes(MipWidth(mip));
        entries[3].textureView = noView;
        entries[3].sampler = noSampler;
        entries[3].bufferElementStride = (u32)sizeof(FluxionVec4);

        FluxionRHIBindGroupDesc desc;
        desc.layout = renderer->prefilterLayout;
        desc.entries = entries;
        desc.entryCount = 4;
        fresh[mip] = Fluxion_RHI_CreateBindGroup(renderer->device, &desc);

        if (!FLUXION_HANDLE_IS_VALID(fresh[mip]))
        {
            for (u32 undo = 0; undo < mip; ++undo) Fluxion_RHI_DestroyBindGroup(fresh[undo]);
            return false;
        }
    }

    for (u32 mip = 0; mip < FLUXION_RENDERER_PREFILTERED_MIPS; ++mip)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->prefilterBindGroups[mip]))
        {
            Fluxion_RHI_DestroyBindGroup(renderer->prefilterBindGroups[mip]);
        }
        renderer->prefilterBindGroups[mip] = fresh[mip];
    }

    return true;
}

} // namespace

extern "C" void FluxionRendererInternal_Prefilter_Project(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                                          FluxionRenderViewHandle view)
{
    if (!EnsureResources(renderer)) return;

    const FluxionRHITextureViewHandle environment = FluxionRendererInternal_RenderView_GetEnvironmentView(view);
    const FluxionRHISamplerHandle sampler = FluxionRendererInternal_RenderView_GetEnvironmentSampler(view);
    const FluxionRHITextureHandle target = FluxionRendererInternal_RenderView_GetPrefilteredTexture(view);
    if (!FLUXION_HANDLE_IS_VALID(environment) || !FLUXION_HANDLE_IS_VALID(target)) return;

    if (!RebuildPrefilterBindGroups(renderer, environment, sampler)) return;

    const FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // The whole chain becomes a copy destination once; the mips are
    // filled one after another through the same scratch buffer, which
    // swings between being written and being read.
    //
    // The previous state is the REAL one: undefined only the first time,
    // and the state every draw since left it in on a refill.
    const bool refilling = FluxionRendererInternal_RenderView_MarkPrefilteredFilled(view);

    FluxionRHIBarrier targetToCopy;
    targetToCopy.texture = target;
    targetToCopy.buffer = noBuffer;
    targetToCopy.before = refilling ? FLUXION_RHI_RESOURCE_STATE_SHADER_READ : FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    targetToCopy.after = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    Fluxion_RHI_CommandList_Barrier(commandList, &targetToCopy, 1);

    for (u32 mip = 0; mip < FLUXION_RENDERER_PREFILTERED_MIPS; ++mip)
    {
        const u32 width = MipWidth(mip);

        FluxionRHIBarrier scratchToWrite;
        scratchToWrite.texture = noTexture;
        scratchToWrite.buffer = renderer->environmentScratchBuffer;
        scratchToWrite.before = mip == 0 ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE;
        scratchToWrite.after = FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE;
        Fluxion_RHI_CommandList_Barrier(commandList, &scratchToWrite, 1);

        Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->prefilterPipeline);
        Fluxion_RHI_CommandList_SetBindGroup(commandList, 0, renderer->prefilterBindGroups[mip]);

        const u32 texelCount = 6 * width * width;
        Fluxion_RHI_CommandList_Dispatch(commandList, (texelCount + kComputeGroupWidth - 1) / kComputeGroupWidth, 1, 1);

        FluxionRHIBarrier scratchToCopy = scratchToWrite;
        scratchToCopy.before = FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE;
        scratchToCopy.after = FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE;
        Fluxion_RHI_CommandList_Barrier(commandList, &scratchToCopy, 1);

        for (u32 face = 0; face < 6; ++face)
        {
            Fluxion_RHI_CommandList_CopyBufferToTexture(commandList, renderer->environmentScratchBuffer,
                                                        (usize)face * FaceStrideBytes(width), target, mip, face);
        }
    }

    FluxionRHIBarrier targetToRead = targetToCopy;
    targetToRead.before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    targetToRead.after = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    Fluxion_RHI_CommandList_Barrier(commandList, &targetToRead, 1);
}

extern "C" void FluxionRendererInternal_Dfg_Compute(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                                    FluxionRenderViewHandle view)
{
    const FluxionRHITextureHandle target = FluxionRendererInternal_RenderView_GetDfgTexture(view);
    if (!FLUXION_HANDLE_IS_VALID(target)) return;

    // Asked before the resources, so a view is only ever marked filled
    // on the way to actually filling it.
    if (!EnsureResources(renderer)) return;
    if (!FluxionRendererInternal_RenderView_TakeDfgWanted(view)) return;

    if (!FLUXION_HANDLE_IS_VALID(renderer->dfgBindGroup))
    {
        const FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        const FluxionRHISamplerHandle noSampler = { FLUXION_HANDLE_INVALID_INDEX, 0 };

        FluxionRHIBindGroupEntry entries[2];
        std::memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->environmentParamsBuffer;
        entries[0].bufferOffset = (usize)kDfgParamsSlot * kParamsSlotBytes;
        entries[0].bufferSize = sizeof(FluxionVec4);
        entries[0].textureView = noView;
        entries[0].sampler = noSampler;

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
        entries[1].buffer = renderer->environmentScratchBuffer;
        entries[1].bufferSize = (usize)FLUXION_RENDERER_DFG_SIZE * RowStrideBytes(FLUXION_RENDERER_DFG_SIZE);
        entries[1].textureView = noView;
        entries[1].sampler = noSampler;
        entries[1].bufferElementStride = (u32)sizeof(FluxionVec4);

        FluxionRHIBindGroupDesc desc;
        desc.layout = renderer->dfgLayout;
        desc.entries = entries;
        desc.entryCount = 2;
        renderer->dfgBindGroup = Fluxion_RHI_CreateBindGroup(renderer->device, &desc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->dfgBindGroup)) return;
    }

    const FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBarrier scratchToWrite;
    scratchToWrite.texture = noTexture;
    scratchToWrite.buffer = renderer->environmentScratchBuffer;
    scratchToWrite.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    scratchToWrite.after = FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE;
    Fluxion_RHI_CommandList_Barrier(commandList, &scratchToWrite, 1);

    Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->dfgPipeline);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, 0, renderer->dfgBindGroup);

    const u32 texelCount = FLUXION_RENDERER_DFG_SIZE * FLUXION_RENDERER_DFG_SIZE;
    Fluxion_RHI_CommandList_Dispatch(commandList, (texelCount + kComputeGroupWidth - 1) / kComputeGroupWidth, 1, 1);

    FluxionRHIBarrier scratchToCopy = scratchToWrite;
    scratchToCopy.before = FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE;
    scratchToCopy.after = FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE;

    FluxionRHIBarrier targetToCopy;
    targetToCopy.texture = target;
    targetToCopy.buffer = noBuffer;
    targetToCopy.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    targetToCopy.after = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;

    const FluxionRHIBarrier toCopy[2] = { scratchToCopy, targetToCopy };
    Fluxion_RHI_CommandList_Barrier(commandList, toCopy, 2);

    Fluxion_RHI_CommandList_CopyBufferToTexture(commandList, renderer->environmentScratchBuffer, 0, target, 0, 0);

    FluxionRHIBarrier targetToRead = targetToCopy;
    targetToRead.before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    targetToRead.after = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    Fluxion_RHI_CommandList_Barrier(commandList, &targetToRead, 1);
}

extern "C" void FluxionRendererInternal_Prefilter_Destroy(FluxionRenderer* renderer)
{
    const FluxionRHIBindGroupHandle noGroup = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    for (u32 mip = 0; mip < FLUXION_RENDERER_PREFILTERED_MIPS; ++mip)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->prefilterBindGroups[mip]))
        {
            Fluxion_RHI_DestroyBindGroup(renderer->prefilterBindGroups[mip]);
        }
        renderer->prefilterBindGroups[mip] = noGroup;
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->dfgBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->dfgBindGroup);
    if (FLUXION_HANDLE_IS_VALID(renderer->environmentScratchBuffer)) Fluxion_RHI_DestroyBuffer(renderer->environmentScratchBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->environmentParamsBuffer)) Fluxion_RHI_DestroyBuffer(renderer->environmentParamsBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->prefilterPipeline)) Fluxion_RHI_DestroyPipeline(renderer->prefilterPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->prefilterLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->prefilterLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->prefilterProgram)) Fluxion_ShaderProgram_Destroy(renderer->prefilterProgram);
    if (FLUXION_HANDLE_IS_VALID(renderer->dfgPipeline)) Fluxion_RHI_DestroyPipeline(renderer->dfgPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->dfgLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->dfgLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->dfgProgram)) Fluxion_ShaderProgram_Destroy(renderer->dfgProgram);

    renderer->dfgBindGroup = noGroup;
    renderer->environmentScratchBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->environmentParamsBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prefilterPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prefilterLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prefilterProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->dfgPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->dfgLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->dfgProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
}
