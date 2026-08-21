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

#include "Renderer/RendererInternal.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#include <string.h>

#define FLUXION_OCCLUSION_LOG_CATEGORY "Renderer"

static const char* const kOcclusionVertexSource = "#include \"Fluxion/Pass/FullscreenVertex.jsl\"\n";
static const char* const kViewDepthFragmentSource = "#include \"Fluxion/Pass/ViewDepth.jsl\"\n";
static const char* const kOcclusionFragmentSource = "#include \"Fluxion/Pass/GroundTruthOcclusion.jsl\"\n";
static const char* const kDenoiseFragmentSource = "#include \"Fluxion/Pass/OcclusionDenoise.jsl\"\n";

// What one level of the distance chain is told -- see Pass/ViewDepth.jsl.
typedef struct FluxionViewDepthUniform
{
    FluxionVec4 sizes;
    FluxionVec4 params;
} FluxionViewDepthUniform;

// And the search itself -- see Pass/GroundTruthOcclusion.jsl. The order
// here is the order the shader declares them in, and that IS the layout.
typedef struct FluxionOcclusionUniform
{
    FluxionVec4 params;
    FluxionVec4 sampling;
    FluxionVec4 projection;
    FluxionMat4 view;
} FluxionOcclusionUniform;

typedef struct FluxionDenoiseUniform
{
    FluxionVec4 params;
} FluxionDenoiseUniform;

// A uniform, a texture and a sampler: the shape the chain's steps and the
// denoise both use.
static FluxionRHIBindGroupLayoutDesc FluxionOcclusion_MakeOneTextureLayoutDesc(const char* name)
{
    FluxionRHIBindGroupLayoutDesc desc;
    memset(&desc, 0, sizeof(desc));

    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[1].binding = 1;
    desc.entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[2].binding = 2;
    desc.entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[2].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entryCount = 3;
    desc.debugName = name;
    return desc;
}

// And the shape for a pass that reads two.
static FluxionRHIBindGroupLayoutDesc FluxionOcclusion_MakeTwoTextureLayoutDesc(const char* name)
{
    FluxionRHIBindGroupLayoutDesc desc = FluxionOcclusion_MakeOneTextureLayoutDesc(name);

    desc.entries[3].binding = 3;
    desc.entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[3].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[4].binding = 4;
    desc.entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[4].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entryCount = 5;
    return desc;
}

static u32 FluxionOcclusion_LevelSize(u32 size, u32 level)
{
    u32 result = size >> level;
    return result > 0 ? result : 1u;
}

void FluxionRendererInternal_Occlusion_Release(FluxionRenderer* renderer)
{
    for (u32 level = 0; level < FLUXION_RENDERER_VIEW_DEPTH_LEVELS; ++level)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthTargetViews[level])) Fluxion_RHI_DestroyTextureView(renderer->viewDepthTargetViews[level]);
        if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthLevelViews[level])) Fluxion_RHI_DestroyTextureView(renderer->viewDepthLevelViews[level]);
        if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthBindGroups[level])) Fluxion_RHI_DestroyBindGroup(renderer->viewDepthBindGroups[level]);
        renderer->viewDepthTargetViews[level] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->viewDepthLevelViews[level] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->viewDepthBindGroups[level] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthChainView)) Fluxion_RHI_DestroyTextureView(renderer->viewDepthChainView);
    if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthTexture)) Fluxion_RHI_DestroyTexture(renderer->viewDepthTexture);
    renderer->viewDepthChainView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->viewDepthTexture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    for (u32 slot = 0; slot < FLUXION_RENDERER_OCCLUSION_TEXTURES; ++slot)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->occlusionTargetViews[slot])) Fluxion_RHI_DestroyTextureView(renderer->occlusionTargetViews[slot]);
        if (FLUXION_HANDLE_IS_VALID(renderer->occlusionSampleViews[slot])) Fluxion_RHI_DestroyTextureView(renderer->occlusionSampleViews[slot]);
        if (FLUXION_HANDLE_IS_VALID(renderer->occlusionTextures[slot])) Fluxion_RHI_DestroyTexture(renderer->occlusionTextures[slot]);
        renderer->occlusionTargetViews[slot] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->occlusionSampleViews[slot] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->occlusionTextures[slot] = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->occlusionBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->occlusionBindGroup);
    if (FLUXION_HANDLE_IS_VALID(renderer->denoiseBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->denoiseBindGroup);
    renderer->occlusionBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->denoiseBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    renderer->occlusionWidth = 0;
    renderer->occlusionHeight = 0;
    renderer->viewDepthNeedsFirstTransition = true;
    renderer->occlusionNeedsFirstTransition = true;
    renderer->occlusionBoundDepthView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

void FluxionRendererInternal_Occlusion_Destroy(FluxionRenderer* renderer)
{
    FluxionRendererInternal_Occlusion_Release(renderer);

    if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthPipeline)) Fluxion_RHI_DestroyPipeline(renderer->viewDepthPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->occlusionPipeline)) Fluxion_RHI_DestroyPipeline(renderer->occlusionPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->denoisePipeline)) Fluxion_RHI_DestroyPipeline(renderer->denoisePipeline);
    renderer->viewDepthPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->occlusionPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->denoisePipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthProgram)) Fluxion_ShaderProgram_Destroy(renderer->viewDepthProgram);
    if (FLUXION_HANDLE_IS_VALID(renderer->occlusionProgram)) Fluxion_ShaderProgram_Destroy(renderer->occlusionProgram);
    if (FLUXION_HANDLE_IS_VALID(renderer->denoiseProgram)) Fluxion_ShaderProgram_Destroy(renderer->denoiseProgram);
    renderer->viewDepthProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->occlusionProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->denoiseProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->viewDepthLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->occlusionLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->occlusionLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->denoiseLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->denoiseLayout);
    renderer->viewDepthLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->occlusionLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->denoiseLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->viewDepthUniformBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->occlusionUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->occlusionUniformBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->denoiseUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->denoiseUniformBuffer);
    renderer->viewDepthUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->occlusionUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->denoiseUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->occlusionSampler)) Fluxion_RHI_DestroySampler(renderer->occlusionSampler);
    renderer->occlusionSampler = (FluxionRHISamplerHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

static bool FluxionOcclusion_EnsureTextures(FluxionRenderer* renderer, u32 width, u32 height)
{
    if (width == 0 || height == 0) return false;

    if (renderer->occlusionWidth == width && renderer->occlusionHeight == height &&
        FLUXION_HANDLE_IS_VALID(renderer->viewDepthTexture))
    {
        return true;
    }

    FluxionRendererInternal_Occlusion_Release(renderer);

    // ONE CHANNEL OF FULL-PRECISION FLOAT, because what it holds is a
    // distance in metres and the far end of a scene is a large number
    // next to the near end. The same choice the depth pyramid made, for
    // the same reason.
    FluxionRHITextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = FLUXION_RENDERER_VIEW_DEPTH_LEVELS;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    desc.format = FLUXION_RHI_FORMAT_R32_FLOAT;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "Fluxion.Renderer.ViewDepth";

    renderer->viewDepthTexture = Fluxion_RHI_CreateTexture(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->viewDepthTexture))
    {
        FLUXION_LOG_ERROR(FLUXION_OCCLUSION_LOG_CATEGORY, "the frame's distances have nowhere to be worked out at %ux%u", width, height);
        return false;
    }

    for (u32 level = 0; level < FLUXION_RENDERER_VIEW_DEPTH_LEVELS; ++level)
    {
        FluxionRHITextureViewDesc viewDesc;
        memset(&viewDesc, 0, sizeof(viewDesc));
        viewDesc.texture = renderer->viewDepthTexture;
        viewDesc.format = desc.format;
        viewDesc.baseMipLevel = level;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

        renderer->viewDepthTargetViews[level] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        renderer->viewDepthLevelViews[level] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->viewDepthTargetViews[level]) ||
            !FLUXION_HANDLE_IS_VALID(renderer->viewDepthLevelViews[level]))
        {
            FLUXION_LOG_ERROR(FLUXION_OCCLUSION_LOG_CATEGORY, "the distance chain's levels could not be looked at");
            return false;
        }
    }

    // AND ONE VIEW OVER ALL OF THEM. The search reads a level it chooses
    // per sample, which a view of a single level cannot do -- the level
    // is an argument to the read, and a read can only reach levels the
    // view covers.
    FluxionRHITextureViewDesc chainDesc;
    memset(&chainDesc, 0, sizeof(chainDesc));
    chainDesc.texture = renderer->viewDepthTexture;
    chainDesc.format = desc.format;
    chainDesc.baseMipLevel = 0;
    chainDesc.mipLevelCount = FLUXION_RENDERER_VIEW_DEPTH_LEVELS;
    chainDesc.baseArrayLayer = 0;
    chainDesc.arrayLayerCount = 1;
    chainDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    renderer->viewDepthChainView = Fluxion_RHI_CreateTextureView(renderer->device, &chainDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->viewDepthChainView)) return false;

    // The two the answer lives in: what the search produced, and what the
    // denoise made of it. Eight bits, because occlusion is a fraction
    // between nothing and all of it -- and because the lighting reads it
    // through an ordinary sampler, which every backend filters for this
    // format without being asked.
    FluxionRHITextureDesc occlusionDesc;
    memset(&occlusionDesc, 0, sizeof(occlusionDesc));
    occlusionDesc.width = width;
    occlusionDesc.height = height;
    occlusionDesc.depth = 1;
    occlusionDesc.mipLevels = 1;
    occlusionDesc.arrayLayers = 1;
    occlusionDesc.sampleCount = 1;
    occlusionDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    occlusionDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    occlusionDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED |
                               FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC;
    occlusionDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    occlusionDesc.debugName = "Fluxion.Renderer.Occlusion";

    for (u32 slot = 0; slot < FLUXION_RENDERER_OCCLUSION_TEXTURES; ++slot)
    {
        renderer->occlusionTextures[slot] = Fluxion_RHI_CreateTexture(renderer->device, &occlusionDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->occlusionTextures[slot])) return false;

        FluxionRHITextureViewDesc viewDesc;
        memset(&viewDesc, 0, sizeof(viewDesc));
        viewDesc.texture = renderer->occlusionTextures[slot];
        viewDesc.format = occlusionDesc.format;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;
        viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

        renderer->occlusionTargetViews[slot] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        renderer->occlusionSampleViews[slot] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->occlusionTargetViews[slot]) ||
            !FLUXION_HANDLE_IS_VALID(renderer->occlusionSampleViews[slot]))
        {
            return false;
        }
    }

    renderer->occlusionWidth = width;
    renderer->occlusionHeight = height;
    renderer->viewDepthNeedsFirstTransition = true;
    renderer->occlusionNeedsFirstTransition = true;
    return true;
}

static FluxionRHIPipelineHandle FluxionOcclusion_MakePipeline(FluxionRenderer* renderer, FluxionShaderProgramHandle program,
                                                              FluxionRHIBindGroupLayoutHandle layout, FluxionRHIFormat colorFormat,
                                                              const char* name)
{
    FluxionRHIVertexLayout vertexLayout;
    memset(&vertexLayout, 0, sizeof(vertexLayout));
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    vertexLayout.attributes[0].offset = 0;
    vertexLayout.attributeCount = 1;
    vertexLayout.stride = 2 * sizeof(f32);

    const FluxionRHIBindGroupLayoutHandle noLayout = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIGraphicsPipelineDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(program);
    desc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(program);
    desc.vertexLayout = vertexLayout;
    desc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    desc.depthState.testEnable = false;
    desc.depthState.writeEnable = false;
    desc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_ALWAYS;
    desc.blendState.mode = FLUXION_RHI_BLEND_MODE_NONE;
    desc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.colorFormats[0] = colorFormat;
    desc.colorFormatCount = 1;
    desc.depthFormat = FLUXION_RHI_FORMAT_UNKNOWN;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = layout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = noLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = noLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = noLayout;
    desc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_GLOBAL + 1;
    desc.debugName = name;
    return Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &desc);
}

static bool FluxionOcclusion_EnsurePipelines(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->occlusionPipeline)) return true;

    // The triangle every fullscreen pass draws. Asked for here because
    // this pass runs before the one that used to own it.
    FluxionRendererInternal_EnsureFullscreenTriangle(renderer);
    if (!FLUXION_HANDLE_IS_VALID(renderer->postVertexBuffer)) return false;

    FluxionShaderProgramDesc programDesc;
    memset(&programDesc, 0, sizeof(programDesc));
    programDesc.vertexSource = kOcclusionVertexSource;

    programDesc.debugName = "Fluxion.Renderer.ViewDepth";
    programDesc.fragmentSource = kViewDepthFragmentSource;
    renderer->viewDepthProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);

    programDesc.debugName = "Fluxion.Renderer.Occlusion";
    programDesc.fragmentSource = kOcclusionFragmentSource;
    renderer->occlusionProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);

    programDesc.debugName = "Fluxion.Renderer.OcclusionDenoise";
    programDesc.fragmentSource = kDenoiseFragmentSource;
    renderer->denoiseProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);

    if (!FLUXION_HANDLE_IS_VALID(renderer->viewDepthProgram) || !FLUXION_HANDLE_IS_VALID(renderer->occlusionProgram) ||
        !FLUXION_HANDLE_IS_VALID(renderer->denoiseProgram))
    {
        FLUXION_LOG_ERROR(FLUXION_OCCLUSION_LOG_CATEGORY, "the occlusion's shaders could not be built; nothing will be occluded");
        return false;
    }

    const FluxionRHIBindGroupLayoutDesc chainLayoutDesc = FluxionOcclusion_MakeOneTextureLayoutDesc("Fluxion.Renderer.ViewDepthLayout");
    const FluxionRHIBindGroupLayoutDesc searchLayoutDesc = FluxionOcclusion_MakeTwoTextureLayoutDesc("Fluxion.Renderer.OcclusionLayout");
    const FluxionRHIBindGroupLayoutDesc denoiseLayoutDesc = FluxionOcclusion_MakeTwoTextureLayoutDesc("Fluxion.Renderer.DenoiseLayout");
    renderer->viewDepthLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &chainLayoutDesc);
    renderer->occlusionLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &searchLayoutDesc);
    renderer->denoiseLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &denoiseLayoutDesc);

    FluxionRHIBufferDesc uniformDesc;
    memset(&uniformDesc, 0, sizeof(uniformDesc));
    uniformDesc.size = (usize)FLUXION_RENDERER_VIEW_DEPTH_LEVELS * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    uniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    uniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    uniformDesc.debugName = "Fluxion.Renderer.ViewDepthSteps";
    renderer->viewDepthUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &uniformDesc);

    uniformDesc.size = FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    uniformDesc.debugName = "Fluxion.Renderer.OcclusionParams";
    renderer->occlusionUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &uniformDesc);

    uniformDesc.debugName = "Fluxion.Renderer.DenoiseParams";
    renderer->denoiseUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &uniformDesc);

    // Clamped at the edges, and no filtering between levels. Every read
    // here names the level it wants; blending two levels would mix
    // distances measured at two scales, and a distance halfway between
    // two scales belongs to neither.
    FluxionRHISamplerDesc samplerDesc;
    memset(&samplerDesc, 0, sizeof(samplerDesc));
    samplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "Fluxion.Renderer.OcclusionSampler";
    renderer->occlusionSampler = Fluxion_RHI_CreateSampler(renderer->device, &samplerDesc);

    renderer->viewDepthPipeline = FluxionOcclusion_MakePipeline(renderer, renderer->viewDepthProgram, renderer->viewDepthLayout,
                                                                FLUXION_RHI_FORMAT_R32_FLOAT, "Fluxion.Renderer.ViewDepthPipeline");
    renderer->occlusionPipeline = FluxionOcclusion_MakePipeline(renderer, renderer->occlusionProgram, renderer->occlusionLayout,
                                                                FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, "Fluxion.Renderer.OcclusionPipeline");
    renderer->denoisePipeline = FluxionOcclusion_MakePipeline(renderer, renderer->denoiseProgram, renderer->denoiseLayout,
                                                              FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, "Fluxion.Renderer.DenoisePipeline");

    if (!FLUXION_HANDLE_IS_VALID(renderer->viewDepthPipeline) || !FLUXION_HANDLE_IS_VALID(renderer->occlusionPipeline) ||
        !FLUXION_HANDLE_IS_VALID(renderer->denoisePipeline) || !FLUXION_HANDLE_IS_VALID(renderer->occlusionSampler) ||
        !FLUXION_HANDLE_IS_VALID(renderer->viewDepthUniformBuffer) || !FLUXION_HANDLE_IS_VALID(renderer->occlusionUniformBuffer) ||
        !FLUXION_HANDLE_IS_VALID(renderer->denoiseUniformBuffer))
    {
        FLUXION_LOG_ERROR(FLUXION_OCCLUSION_LOG_CATEGORY, "the occlusion could not be set up; nothing will be occluded");
        return false;
    }
    return true;
}

static bool FluxionOcclusion_EnsureBindGroups(FluxionRenderer* renderer, FluxionRHITextureViewHandle depthView)
{
    if (renderer->occlusionBoundDepthView.index == depthView.index &&
        renderer->occlusionBoundDepthView.generation == depthView.generation &&
        FLUXION_HANDLE_IS_VALID(renderer->occlusionBindGroup))
    {
        return true;
    }

    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHISamplerHandle noSampler = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    for (u32 level = 0; level < FLUXION_RENDERER_VIEW_DEPTH_LEVELS; ++level)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->viewDepthBindGroups[level])) Fluxion_RHI_DestroyBindGroup(renderer->viewDepthBindGroups[level]);
        renderer->viewDepthBindGroups[level] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

        FluxionRHIBindGroupEntry entries[3];
        memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->viewDepthUniformBuffer;
        entries[0].bufferOffset = (usize)level * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
        entries[0].bufferSize = sizeof(FluxionViewDepthUniform);

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[1].textureView = level == 0 ? depthView : renderer->viewDepthLevelViews[level - 1u];

        entries[2].binding = 2;
        entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[2].sampler = renderer->occlusionSampler;

        FluxionRHIBindGroupDesc groupDesc;
        memset(&groupDesc, 0, sizeof(groupDesc));
        groupDesc.layout = renderer->viewDepthLayout;
        groupDesc.entries = entries;
        groupDesc.entryCount = 3;
        renderer->viewDepthBindGroups[level] = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->viewDepthBindGroups[level])) return false;
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->occlusionBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->occlusionBindGroup);
    {
        FluxionRHIBindGroupEntry entries[5];
        memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->occlusionUniformBuffer;
        entries[0].bufferSize = sizeof(FluxionOcclusionUniform);

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[1].textureView = renderer->viewDepthChainView;

        entries[2].binding = 2;
        entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[2].sampler = renderer->occlusionSampler;

        entries[3].binding = 3;
        entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[3].textureView = renderer->prepassSampleView;

        entries[4].binding = 4;
        entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[4].sampler = renderer->occlusionSampler;

        FluxionRHIBindGroupDesc groupDesc;
        memset(&groupDesc, 0, sizeof(groupDesc));
        groupDesc.layout = renderer->occlusionLayout;
        groupDesc.entries = entries;
        groupDesc.entryCount = 5;
        renderer->occlusionBindGroup = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->occlusionBindGroup)) return false;
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->denoiseBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->denoiseBindGroup);
    {
        FluxionRHIBindGroupEntry entries[5];
        memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->denoiseUniformBuffer;
        entries[0].bufferSize = sizeof(FluxionDenoiseUniform);

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[1].textureView = renderer->occlusionSampleViews[0];

        entries[2].binding = 2;
        entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[2].sampler = renderer->occlusionSampler;

        entries[3].binding = 3;
        entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[3].textureView = renderer->viewDepthLevelViews[0];

        entries[4].binding = 4;
        entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[4].sampler = renderer->occlusionSampler;

        FluxionRHIBindGroupDesc groupDesc;
        memset(&groupDesc, 0, sizeof(groupDesc));
        groupDesc.layout = renderer->denoiseLayout;
        groupDesc.entries = entries;
        groupDesc.entryCount = 5;
        renderer->denoiseBindGroup = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->denoiseBindGroup)) return false;
    }

    FLUXION_UNUSED(noBuffer);
    FLUXION_UNUSED(noView);
    FLUXION_UNUSED(noSampler);

    renderer->occlusionBoundDepthView = depthView;
    return true;
}

static void FluxionOcclusion_DrawFullscreen(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                            FluxionRHITextureViewHandle targetView, FluxionRHIPipelineHandle pipeline,
                                            FluxionRHIBindGroupHandle bindGroup, u32 width, u32 height)
{
    FluxionRHIRenderingAttachment attachment;
    attachment.view = targetView;
    attachment.clear = false;
    attachment.clearColor[0] = 0.0f;
    attachment.clearColor[1] = 0.0f;
    attachment.clearColor[2] = 0.0f;
    attachment.clearColor[3] = 1.0f;

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = &attachment;
    renderingDesc.colorAttachmentCount = 1;
    renderingDesc.depthAttachment = NULL;
    renderingDesc.width = width;
    renderingDesc.height = height;

    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
    Fluxion_RHI_CommandList_SetViewport(commandList, 0.0f, 0.0f, (f32)width, (f32)height, 0.0f, 1.0f);
    Fluxion_RHI_CommandList_SetScissor(commandList, 0, 0, width, height);
    Fluxion_RHI_CommandList_SetPipeline(commandList, pipeline);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, bindGroup);
    Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->postVertexBuffer, 0);
    Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
    Fluxion_RHI_CommandList_EndRendering(commandList);
}

bool FluxionRendererInternal_Occlusion_Build(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                             FluxionRHITextureViewHandle depthView)
{
    if (!renderer->occlusionEnabled) return false;
    if (renderer->occlusionFailed) return false;

    // NOTHING TO READ. The surfaces are what says which way each pixel
    // faces, and without them there is no hemisphere to measure.
    if (!FLUXION_HANDLE_IS_VALID(renderer->prepassTexture)) return false;
    if (!FLUXION_HANDLE_IS_VALID(depthView)) return false;

    FluxionViewport viewport = { 0 };
    if (!FluxionRendererInternal_RenderView_GetViewport(renderer->currentView, &viewport)) return false;

    const u32 width = (u32)viewport.width;
    const u32 height = (u32)viewport.height;

    if (!FluxionOcclusion_EnsureTextures(renderer, width, height) || !FluxionOcclusion_EnsurePipelines(renderer) ||
        !FluxionOcclusion_EnsureBindGroups(renderer, depthView))
    {
        FLUXION_LOG_WARN(FLUXION_OCCLUSION_LOG_CATEGORY, "nothing will be occluded; the lighting keeps all of the sky");
        renderer->occlusionFailed = true;
        return false;
    }

    FluxionMat4 viewMatrix;
    FluxionMat4 projectionMatrix;
    FluxionRendererInternal_RenderView_GetMatrices(renderer->currentView, &viewMatrix, &projectionMatrix);

    FluxionVec4 settings;
    FluxionRendererInternal_RenderView_GetOcclusion(renderer->currentView, &settings);

    // WHICH WAY THIS BACKEND'S ROWS RUN, for a FULLSCREEN pass -- which is
    // the opposite answer from the one a drawn surface needs, because the
    // fullscreen stage already hands over a coordinate turned the right
    // way up for reading a texture. Both questions are real and both are
    // written down where they are asked.
    const bool rowsFlipped = Fluxion_RHI_GetDeviceBackendType(renderer->device) == FLUXION_RHI_BACKEND_OPENGL;
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // Every level's numbers, written once before any of them run.
    u8* uniforms = (u8*)Fluxion_RHI_MapBuffer(renderer->viewDepthUniformBuffer);
    if (uniforms == NULL) return false;

    for (u32 level = 0; level < FLUXION_RENDERER_VIEW_DEPTH_LEVELS; ++level)
    {
        const u32 sourceWidth = level == 0 ? width : FluxionOcclusion_LevelSize(width, level - 1u);
        const u32 sourceHeight = level == 0 ? height : FluxionOcclusion_LevelSize(height, level - 1u);

        FluxionViewDepthUniform uniform;
        memset(&uniform, 0, sizeof(uniform));
        uniform.sizes.x = (f32)sourceWidth;
        uniform.sizes.y = (f32)sourceHeight;
        uniform.sizes.z = (f32)FluxionOcclusion_LevelSize(width, level);
        uniform.sizes.w = (f32)FluxionOcclusion_LevelSize(height, level);

        // The two numbers that turn a depth into a distance, taken from
        // the projection this frame was drawn with rather than from a
        // near and a far plane nobody handed over.
        uniform.params.x = projectionMatrix.m[2][2];
        uniform.params.y = projectionMatrix.m[2][3];
        uniform.params.z = level == 0 ? 1.0f : 0.0f;
        uniform.params.w = rowsFlipped ? 1.0f : 0.0f;

        memcpy(uniforms + (usize)level * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE, &uniform, sizeof(uniform));
    }
    Fluxion_RHI_UnmapBuffer(renderer->viewDepthUniformBuffer);

    FluxionOcclusionUniform occlusionUniform;
    memset(&occlusionUniform, 0, sizeof(occlusionUniform));
    occlusionUniform.params.x = 1.0f / (f32)width;
    occlusionUniform.params.y = 1.0f / (f32)height;
    occlusionUniform.params.z = settings.x;
    occlusionUniform.params.w = settings.y;
    occlusionUniform.sampling.x = settings.z;
    occlusionUniform.sampling.y = settings.w;

    // WHERE A SAMPLE STOPS COUNTING ENTIRELY, past the radius rather than
    // at it -- see the shader for why a hard edge draws a ring.
    occlusionUniform.sampling.z = settings.x * 2.0f;
    occlusionUniform.sampling.w = rowsFlipped ? 1.0f : 0.0f;

    // One over the projection's two diagonal terms: what turns a place on
    // the screen back into a direction in front of the eye.
    occlusionUniform.projection.x = projectionMatrix.m[0][0] != 0.0f ? 1.0f / projectionMatrix.m[0][0] : 1.0f;
    occlusionUniform.projection.y = projectionMatrix.m[1][1] != 0.0f ? 1.0f / projectionMatrix.m[1][1] : 1.0f;

    // AND THE PLAIN TERM AS WELL, which is not the reciprocal of the one
    // above by accident: one answers "what direction is this pixel" and
    // the other "how big does a metre look from here". See the shader.
    occlusionUniform.projection.z = projectionMatrix.m[0][0];
    occlusionUniform.view = viewMatrix;

    void* mapped = Fluxion_RHI_MapBuffer(renderer->occlusionUniformBuffer);
    if (mapped == NULL) return false;
    memcpy(mapped, &occlusionUniform, sizeof(occlusionUniform));
    Fluxion_RHI_UnmapBuffer(renderer->occlusionUniformBuffer);

    FluxionDenoiseUniform denoiseUniform;
    memset(&denoiseUniform, 0, sizeof(denoiseUniform));
    denoiseUniform.params.x = 1.0f / (f32)width;
    denoiseUniform.params.y = 1.0f / (f32)height;

    // How far apart in metres two neighbours have to be before they stop
    // counting as the same surface. A fraction of the radius, so that a
    // caller who works in centimetres and one who works in metres both
    // get an edge-aware blur rather than a plain one.
    denoiseUniform.params.z = settings.x * 0.1f;
    denoiseUniform.params.w = rowsFlipped ? 1.0f : 0.0f;

    mapped = Fluxion_RHI_MapBuffer(renderer->denoiseUniformBuffer);
    if (mapped == NULL) return false;
    memcpy(mapped, &denoiseUniform, sizeof(denoiseUniform));
    Fluxion_RHI_UnmapBuffer(renderer->denoiseUniformBuffer);

    // THE DEPTH HAS TO BECOME READABLE, and it is not: the pass that
    // recorded the surfaces left it as something being DRAWN INTO, which
    // is a different thing on every backend that has layouts at all. It
    // goes back at the end, because the pass after this one draws into it
    // again.
    const FluxionRHITextureHandle depthTexture = Fluxion_RHI_GetTextureViewTexture(depthView);
    if (!FLUXION_HANDLE_IS_VALID(depthTexture)) return false;

    FluxionRHIBarrier depthToRead = { depthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE,
                                      FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &depthToRead, 1);

    if (renderer->viewDepthNeedsFirstTransition)
    {
        FluxionRHIBarrier firstUse = { renderer->viewDepthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED,
                                       FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(commandList, &firstUse, 1);
        renderer->viewDepthNeedsFirstTransition = false;
    }

    if (renderer->occlusionNeedsFirstTransition)
    {
        for (u32 slot = 0; slot < FLUXION_RENDERER_OCCLUSION_TEXTURES; ++slot)
        {
            FluxionRHIBarrier firstUse = { renderer->occlusionTextures[slot], noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED,
                                           FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
            Fluxion_RHI_CommandList_Barrier(commandList, &firstUse, 1);
        }
        renderer->occlusionNeedsFirstTransition = false;
    }

    // --- the distances, and then the same at half size, four times -------
    for (u32 level = 0; level < FLUXION_RENDERER_VIEW_DEPTH_LEVELS; ++level)
    {
        FluxionRHIBarrier toTarget = { renderer->viewDepthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                       FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, level, 1 };
        Fluxion_RHI_CommandList_Barrier(commandList, &toTarget, 1);

        if (level > 0)
        {
            FluxionRHIBarrier toSource = { renderer->viewDepthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                           FLUXION_RHI_RESOURCE_STATE_SHADER_READ, level - 1u, 1 };
            Fluxion_RHI_CommandList_Barrier(commandList, &toSource, 1);
        }

        FluxionOcclusion_DrawFullscreen(renderer, commandList, renderer->viewDepthTargetViews[level],
                                        renderer->viewDepthPipeline, renderer->viewDepthBindGroups[level],
                                        FluxionOcclusion_LevelSize(width, level), FluxionOcclusion_LevelSize(height, level));
    }

    FluxionRHIBarrier lastToSource = { renderer->viewDepthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                       FLUXION_RHI_RESOURCE_STATE_SHADER_READ, FLUXION_RENDERER_VIEW_DEPTH_LEVELS - 1u, 1 };
    Fluxion_RHI_CommandList_Barrier(commandList, &lastToSource, 1);

    // --- the search ------------------------------------------------------
    FluxionRHIBarrier searchToTarget = { renderer->occlusionTextures[0], noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                         FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &searchToTarget, 1);

    FluxionOcclusion_DrawFullscreen(renderer, commandList, renderer->occlusionTargetViews[0], renderer->occlusionPipeline,
                                    renderer->occlusionBindGroup, width, height);

    FluxionRHIBarrier searchToRead = { renderer->occlusionTextures[0], noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                       FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &searchToRead, 1);

    // --- and the noise taken back out ------------------------------------
    FluxionRHIBarrier denoiseToTarget = { renderer->occlusionTextures[1], noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                          FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &denoiseToTarget, 1);

    FluxionOcclusion_DrawFullscreen(renderer, commandList, renderer->occlusionTargetViews[1], renderer->denoisePipeline,
                                    renderer->denoiseBindGroup, width, height);

    FluxionRHIBarrier denoiseToRead = { renderer->occlusionTextures[1], noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                        FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &denoiseToRead, 1);

    // And the depth back to what the pass after this one expects to find.
    FluxionRHIBarrier depthBack = { depthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                    FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &depthBack, 1);
    return true;
}

FluxionRHITextureViewHandle FluxionRendererInternal_Occlusion_GetView(const FluxionRenderer* renderer)
{
    return renderer->occlusionSampleViews[1];
}

FluxionRHITextureHandle FluxionRendererInternal_Occlusion_GetTexture(const FluxionRenderer* renderer)
{
    return renderer->occlusionTextures[1];
}
