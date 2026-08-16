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

// Turning an environment into the nine numbers a diffuse surface reads.
//
// Runs when the environment changes and not otherwise. What it produces
// depends on nothing but the sky, so a frame that draws the same sky as
// the last one has nothing to work out.

#include "RendererInternal.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#include <cstring>

namespace
{

// The whole shader, which is one line: everything it does is in the
// library, where it can be read as a shader rather than as a string.
const char* const kIrradianceSource = "#include \"Fluxion/Pass/IrradianceProject.jsl\"\n";

// The bindings the pass declares, in the order the shader compiler hands
// them out: a texture takes a pair -- the texture and its sampler -- and
// storage buffers come after. There is no uniform buffer in this group,
// so the numbering starts at the texture rather than after one.
FluxionRHIBindGroupLayoutDesc MakeIrradianceLayoutDesc()
{
    FluxionRHIBindGroupLayoutDesc desc = { };

    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entries[1].binding = 1;
    desc.entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entries[2].binding = 2;
    desc.entries[2].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    desc.entries[2].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

    desc.entryCount = 3;
    desc.debugName = "Fluxion.Renderer.IrradianceBindGroupLayout";
    return desc;
}

} // namespace

extern "C" bool FluxionRendererInternal_Irradiance_EnsureResources(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->irradiancePipeline)) return true;
    if (renderer->irradianceFailed) return false;

    FluxionShaderProgramDesc programDesc;
    std::memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "Fluxion.Renderer.Irradiance";
    programDesc.computeSource = kIrradianceSource;

    renderer->irradianceProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->irradianceProgram))
    {
        // Said once, not once a frame. A shader that would not build will
        // not build again, and repeating it buries whatever said it first.
        FLUXION_LOG_ERROR("Renderer", "The irradiance shader could not be built; the sky will light nothing.");
        renderer->irradianceFailed = true;
        return false;
    }

    FluxionRHIBindGroupLayoutDesc layoutDesc = MakeIrradianceLayoutDesc();
    renderer->irradianceLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &layoutDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->irradianceLayout))
    {
        renderer->irradianceFailed = true;
        return false;
    }

    FluxionRHIComputePipelineDesc pipelineDesc;
    std::memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.computeShader = FluxionRendererInternal_ShaderProgram_GetComputeShader(renderer->irradianceProgram);
    pipelineDesc.bindGroupLayouts[0] = renderer->irradianceLayout;
    pipelineDesc.bindGroupLayoutCount = 1;
    pipelineDesc.debugName = "Fluxion.Renderer.IrradiancePipeline";

    renderer->irradiancePipeline = Fluxion_RHI_CreateComputePipeline(renderer->device, &pipelineDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->irradiancePipeline))
    {
        renderer->irradianceFailed = true;
        return false;
    }

    return true;
}

extern "C" void FluxionRendererInternal_Irradiance_Project(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                                           FluxionRenderViewHandle view)
{
    // Asked first, and asking clears it. Everything below is skipped for
    // a sky that has not changed, which is every frame but the first
    // after one is set.
    if (!FluxionRendererInternal_RenderView_TakeEnvironmentDirty(view)) return;
    if (!FluxionRendererInternal_Irradiance_EnsureResources(renderer)) return;

    const FluxionRHIBufferHandle target = FluxionRendererInternal_RenderView_GetIrradianceBuffer(view);
    const FluxionRHITextureViewHandle environment = FluxionRendererInternal_RenderView_GetEnvironmentView(view);
    const FluxionRHISamplerHandle sampler = FluxionRendererInternal_RenderView_GetEnvironmentSampler(view);

    if (!FLUXION_HANDLE_IS_VALID(target) || !FLUXION_HANDLE_IS_VALID(environment)) return;

    const FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHISamplerHandle noSampler = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBindGroupEntry entries[3];
    std::memset(entries, 0, sizeof(entries));

    entries[0].binding = 0;
    entries[0].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[0].buffer = noBuffer;
    entries[0].textureView = environment;
    entries[0].sampler = noSampler;

    entries[1].binding = 1;
    entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[1].buffer = noBuffer;
    entries[1].textureView = noView;
    entries[1].sampler = sampler;

    entries[2].binding = 2;
    entries[2].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    entries[2].buffer = target;
    entries[2].bufferSize = 9 * sizeof(FluxionVec4);
    entries[2].textureView = noView;
    entries[2].sampler = noSampler;
    entries[2].bufferElementStride = (u32)sizeof(FluxionVec4);

    FluxionRHIBindGroupDesc groupDesc;
    groupDesc.layout = renderer->irradianceLayout;
    groupDesc.entries = entries;
    groupDesc.entryCount = 3;

    FluxionRHIBindGroupHandle group = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
    if (!FLUXION_HANDLE_IS_VALID(group)) return;

    // The previous one goes only once its replacement exists, so a failure
    // above leaves the renderer holding something usable rather than
    // nothing.
    if (FLUXION_HANDLE_IS_VALID(renderer->irradianceBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->irradianceBindGroup);
    renderer->irradianceBindGroup = group;

    // Into a state a compute shader may write. What it was before is
    // whatever last read it -- on the first pass, nothing at all.
    FluxionRHIBarrier toWrite;
    toWrite.texture = FluxionRHITextureHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    toWrite.buffer = target;
    toWrite.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    toWrite.after = FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE;
    Fluxion_RHI_CommandList_Barrier(commandList, &toWrite, 1);

    Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->irradiancePipeline);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, 0, group);

    // One group, and one is enough: there are nine coefficients and a
    // group is wider than nine. Every thread walks the whole sky for its
    // own coefficient, so there is nothing to divide between groups.
    Fluxion_RHI_CommandList_Dispatch(commandList, 1, 1, 1);

    FluxionRHIBarrier toRead = toWrite;
    toRead.before = FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE;
    toRead.after = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);
}

extern "C" void FluxionRendererInternal_Irradiance_Destroy(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->irradianceBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->irradianceBindGroup);
    if (FLUXION_HANDLE_IS_VALID(renderer->irradiancePipeline)) Fluxion_RHI_DestroyPipeline(renderer->irradiancePipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->irradianceLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->irradianceLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->irradianceProgram)) Fluxion_ShaderProgram_Destroy(renderer->irradianceProgram);

    renderer->irradianceBindGroup = FluxionRHIBindGroupHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->irradiancePipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->irradianceLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->irradianceProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
}
