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

#include <math.h>
#include <string.h>

#define FLUXION_EXPOSURE_LOG_CATEGORY "Renderer"

static const char* const kExposureVertexSource = "#include \"Fluxion/Pass/FullscreenVertex.jsl\"\n";
static const char* const kLuminanceFragmentSource = "#include \"Fluxion/Pass/LuminanceDown.jsl\"\n";
static const char* const kExposureAdaptFragmentSource = "#include \"Fluxion/Pass/ExposureAdapt.jsl\"\n";

// What one step of the chain is told -- see LuminanceDown.jsl.
typedef struct FluxionLuminanceUniform
{
    FluxionVec4 step;
} FluxionLuminanceUniform;

// And what the one texel at the end is told -- see ExposureAdapt.jsl.
typedef struct FluxionExposureAdaptUniform
{
    FluxionVec4 params;
} FluxionExposureAdaptUniform;

// The chain's steps read one texture and write another: a uniform, a
// texture and a sampler, which is the same shape the glow's steps use.
static FluxionRHIBindGroupLayoutDesc FluxionAutoExposure_MakeChainLayoutDesc(void)
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
    desc.debugName = "Fluxion.Renderer.LuminanceLayout";
    return desc;
}

// The last step reads two: what was measured, and what the camera was set
// to last frame.
static FluxionRHIBindGroupLayoutDesc FluxionAutoExposure_MakeAdaptLayoutDesc(void)
{
    FluxionRHIBindGroupLayoutDesc desc = FluxionAutoExposure_MakeChainLayoutDesc();

    desc.entries[3].binding = 3;
    desc.entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[3].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[4].binding = 4;
    desc.entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[4].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entryCount = 5;
    desc.debugName = "Fluxion.Renderer.ExposureAdaptLayout";
    return desc;
}

static u32 FluxionAutoExposure_LevelSize(u32 level)
{
    // Exact at every level, because the chain starts at a power of two --
    // which is most of the reason it starts at a fixed square rather than
    // at a fraction of a window that could be any size at all.
    u32 size = FLUXION_RENDERER_LUMINANCE_SIZE >> level;
    return size > 0 ? size : 1u;
}

void FluxionRendererInternal_AutoExposure_Release(FluxionRenderer* renderer)
{
    for (u32 level = 0; level < FLUXION_RENDERER_LUMINANCE_LEVELS; ++level)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->luminanceTargetViews[level])) Fluxion_RHI_DestroyTextureView(renderer->luminanceTargetViews[level]);
        if (FLUXION_HANDLE_IS_VALID(renderer->luminanceSampleViews[level])) Fluxion_RHI_DestroyTextureView(renderer->luminanceSampleViews[level]);
        if (FLUXION_HANDLE_IS_VALID(renderer->luminanceBindGroups[level])) Fluxion_RHI_DestroyBindGroup(renderer->luminanceBindGroups[level]);
        renderer->luminanceTargetViews[level] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->luminanceSampleViews[level] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->luminanceBindGroups[level] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    for (u32 slot = 0; slot < FLUXION_RENDERER_EXPOSURE_HISTORY; ++slot)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->exposureTargetViews[slot])) Fluxion_RHI_DestroyTextureView(renderer->exposureTargetViews[slot]);
        if (FLUXION_HANDLE_IS_VALID(renderer->exposureSampleViews[slot])) Fluxion_RHI_DestroyTextureView(renderer->exposureSampleViews[slot]);
        if (FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptBindGroups[slot])) Fluxion_RHI_DestroyBindGroup(renderer->exposureAdaptBindGroups[slot]);
        if (FLUXION_HANDLE_IS_VALID(renderer->exposureTextures[slot])) Fluxion_RHI_DestroyTexture(renderer->exposureTextures[slot]);
        renderer->exposureTargetViews[slot] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->exposureSampleViews[slot] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->exposureAdaptBindGroups[slot] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->exposureTextures[slot] = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->luminanceTexture)) Fluxion_RHI_DestroyTexture(renderer->luminanceTexture);
    renderer->luminanceTexture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->luminancePipeline)) Fluxion_RHI_DestroyPipeline(renderer->luminancePipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptPipeline)) Fluxion_RHI_DestroyPipeline(renderer->exposureAdaptPipeline);
    renderer->luminancePipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->exposureAdaptPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->luminanceProgram)) Fluxion_ShaderProgram_Destroy(renderer->luminanceProgram);
    if (FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptProgram)) Fluxion_ShaderProgram_Destroy(renderer->exposureAdaptProgram);
    renderer->luminanceProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->exposureAdaptProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->luminanceLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->luminanceLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->exposureAdaptLayout);
    renderer->luminanceLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->exposureAdaptLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->luminanceUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->luminanceUniformBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->exposureAdaptUniformBuffer);
    renderer->luminanceUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->exposureAdaptUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    renderer->luminanceBoundSceneColor = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->luminanceNeedsFirstTransition = true;
    renderer->exposureNeedsFirstTransition = true;
    renderer->exposureHasHistory = false;
    renderer->exposureCurrent = 0;
}

static bool FluxionAutoExposure_EnsureTextures(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->luminanceTexture)) return true;

    // THE SAME FORMAT THE SCENE IS IN, and a single channel would do.
    // Four are used because every target in this renderer is this format
    // and a backend is only asked for one combination -- at a quarter of
    // a megabyte, the saving would buy nothing and the new format would
    // be one more thing that can differ between three backends.
    FluxionRHITextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = FLUXION_RENDERER_LUMINANCE_SIZE;
    desc.height = FLUXION_RENDERER_LUMINANCE_SIZE;
    desc.depth = 1;
    desc.mipLevels = FLUXION_RENDERER_LUMINANCE_LEVELS;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    desc.format = FLUXION_RENDERER_SCENE_COLOR_FORMAT;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "Fluxion.Renderer.Luminance";

    renderer->luminanceTexture = Fluxion_RHI_CreateTexture(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->luminanceTexture))
    {
        FLUXION_LOG_ERROR(FLUXION_EXPOSURE_LOG_CATEGORY, "the frame's brightness has nowhere to be measured");
        return false;
    }

    for (u32 level = 0; level < FLUXION_RENDERER_LUMINANCE_LEVELS; ++level)
    {
        FluxionRHITextureViewDesc viewDesc;
        memset(&viewDesc, 0, sizeof(viewDesc));
        viewDesc.texture = renderer->luminanceTexture;
        viewDesc.format = FLUXION_RENDERER_SCENE_COLOR_FORMAT;
        viewDesc.baseMipLevel = level;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

        renderer->luminanceTargetViews[level] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        renderer->luminanceSampleViews[level] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->luminanceTargetViews[level]) ||
            !FLUXION_HANDLE_IS_VALID(renderer->luminanceSampleViews[level]))
        {
            FLUXION_LOG_ERROR(FLUXION_EXPOSURE_LOG_CATEGORY, "the brightness chain's levels could not be looked at");
            return false;
        }
    }

    // One texel each, and they take it in turns.
    FluxionRHITextureDesc exposureDesc = desc;
    exposureDesc.width = 1;
    exposureDesc.height = 1;
    exposureDesc.mipLevels = 1;
    exposureDesc.debugName = "Fluxion.Renderer.Exposure";

    for (u32 slot = 0; slot < FLUXION_RENDERER_EXPOSURE_HISTORY; ++slot)
    {
        renderer->exposureTextures[slot] = Fluxion_RHI_CreateTexture(renderer->device, &exposureDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->exposureTextures[slot]))
        {
            FLUXION_LOG_ERROR(FLUXION_EXPOSURE_LOG_CATEGORY, "the camera has nowhere to remember its setting");
            return false;
        }

        FluxionRHITextureViewDesc viewDesc;
        memset(&viewDesc, 0, sizeof(viewDesc));
        viewDesc.texture = renderer->exposureTextures[slot];
        viewDesc.format = FLUXION_RENDERER_SCENE_COLOR_FORMAT;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;
        viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

        renderer->exposureTargetViews[slot] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        renderer->exposureSampleViews[slot] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->exposureTargetViews[slot]) ||
            !FLUXION_HANDLE_IS_VALID(renderer->exposureSampleViews[slot]))
        {
            FLUXION_LOG_ERROR(FLUXION_EXPOSURE_LOG_CATEGORY, "the camera's setting could not be looked at");
            return false;
        }
    }

    renderer->luminanceNeedsFirstTransition = true;
    renderer->exposureNeedsFirstTransition = true;
    renderer->exposureHasHistory = false;
    return true;
}

static bool FluxionAutoExposure_EnsurePipelines(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->luminancePipeline) && FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptPipeline))
    {
        return true;
    }

    FluxionShaderProgramDesc chainDesc;
    memset(&chainDesc, 0, sizeof(chainDesc));
    chainDesc.debugName = "Fluxion.Renderer.LuminanceDown";
    chainDesc.vertexSource = kExposureVertexSource;
    chainDesc.fragmentSource = kLuminanceFragmentSource;
    renderer->luminanceProgram = Fluxion_ShaderProgram_Create(renderer->device, &chainDesc);

    FluxionShaderProgramDesc adaptDesc;
    memset(&adaptDesc, 0, sizeof(adaptDesc));
    adaptDesc.debugName = "Fluxion.Renderer.ExposureAdapt";
    adaptDesc.vertexSource = kExposureVertexSource;
    adaptDesc.fragmentSource = kExposureAdaptFragmentSource;
    renderer->exposureAdaptProgram = Fluxion_ShaderProgram_Create(renderer->device, &adaptDesc);

    if (!FLUXION_HANDLE_IS_VALID(renderer->luminanceProgram) || !FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptProgram))
    {
        FLUXION_LOG_ERROR(FLUXION_EXPOSURE_LOG_CATEGORY, "the exposure's shaders could not be built; the camera will not adapt");
        return false;
    }

    const FluxionRHIBindGroupLayoutDesc chainLayoutDesc = FluxionAutoExposure_MakeChainLayoutDesc();
    const FluxionRHIBindGroupLayoutDesc adaptLayoutDesc = FluxionAutoExposure_MakeAdaptLayoutDesc();
    renderer->luminanceLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &chainLayoutDesc);
    renderer->exposureAdaptLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &adaptLayoutDesc);

    FluxionRHIBufferDesc chainUniformDesc;
    memset(&chainUniformDesc, 0, sizeof(chainUniformDesc));
    chainUniformDesc.size = (usize)FLUXION_RENDERER_LUMINANCE_LEVELS * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    chainUniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    chainUniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    chainUniformDesc.debugName = "Fluxion.Renderer.LuminanceSteps";
    renderer->luminanceUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &chainUniformDesc);

    FluxionRHIBufferDesc adaptUniformDesc = chainUniformDesc;
    adaptUniformDesc.size = FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    adaptUniformDesc.debugName = "Fluxion.Renderer.ExposureAdaptParams";
    renderer->exposureAdaptUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &adaptUniformDesc);

    FluxionRHIVertexLayout vertexLayout;
    memset(&vertexLayout, 0, sizeof(vertexLayout));
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    vertexLayout.attributes[0].offset = 0;
    vertexLayout.attributeCount = 1;
    vertexLayout.stride = 2 * sizeof(f32);

    const FluxionRHIBindGroupLayoutHandle noLayout = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIGraphicsPipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->luminanceProgram);
    pipelineDesc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->luminanceProgram);
    pipelineDesc.vertexLayout = vertexLayout;
    pipelineDesc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    pipelineDesc.depthState.testEnable = false;
    pipelineDesc.depthState.writeEnable = false;
    pipelineDesc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_ALWAYS;
    pipelineDesc.blendState.mode = FLUXION_RHI_BLEND_MODE_NONE;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.colorFormats[0] = FLUXION_RENDERER_SCENE_COLOR_FORMAT;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = FLUXION_RHI_FORMAT_UNKNOWN;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = renderer->luminanceLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = noLayout;
    pipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_GLOBAL + 1;
    pipelineDesc.debugName = "Fluxion.Renderer.LuminancePipeline";
    renderer->luminancePipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &pipelineDesc);

    pipelineDesc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->exposureAdaptProgram);
    pipelineDesc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->exposureAdaptProgram);
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = renderer->exposureAdaptLayout;
    pipelineDesc.debugName = "Fluxion.Renderer.ExposureAdaptPipeline";
    renderer->exposureAdaptPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &pipelineDesc);

    if (!FLUXION_HANDLE_IS_VALID(renderer->luminancePipeline) || !FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptPipeline) ||
        !FLUXION_HANDLE_IS_VALID(renderer->luminanceUniformBuffer) || !FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptUniformBuffer))
    {
        FLUXION_LOG_ERROR(FLUXION_EXPOSURE_LOG_CATEGORY, "the exposure could not be set up; the camera will not adapt");
        return false;
    }
    return true;
}

// One group per level, and one per history slot. Remade only when what
// they read changes -- which is when the window is resized and the
// scene's texture is a different one.
static bool FluxionAutoExposure_EnsureBindGroups(FluxionRenderer* renderer)
{
    if (renderer->luminanceBoundSceneColor.index == renderer->sceneColorSampleView.index &&
        renderer->luminanceBoundSceneColor.generation == renderer->sceneColorSampleView.generation &&
        FLUXION_HANDLE_IS_VALID(renderer->luminanceBindGroups[0]))
    {
        return true;
    }

    for (u32 level = 0; level < FLUXION_RENDERER_LUMINANCE_LEVELS; ++level)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->luminanceBindGroups[level])) Fluxion_RHI_DestroyBindGroup(renderer->luminanceBindGroups[level]);
        renderer->luminanceBindGroups[level] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

        FluxionRHIBindGroupEntry entries[3];
        memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->luminanceUniformBuffer;
        entries[0].bufferOffset = (usize)level * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
        entries[0].bufferSize = sizeof(FluxionLuminanceUniform);

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;

        // THE FIRST LEVEL READS THE SCENE; every one after it reads the
        // level above. That single difference is what makes this a chain
        // rather than nine copies of the same draw.
        entries[1].textureView = level == 0 ? renderer->sceneColorSampleView : renderer->luminanceSampleViews[level - 1u];

        entries[2].binding = 2;
        entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[2].sampler = renderer->postSampler;

        FluxionRHIBindGroupDesc groupDesc;
        memset(&groupDesc, 0, sizeof(groupDesc));
        groupDesc.layout = renderer->luminanceLayout;
        groupDesc.entries = entries;
        groupDesc.entryCount = 3;

        renderer->luminanceBindGroups[level] = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->luminanceBindGroups[level])) return false;
    }

    for (u32 slot = 0; slot < FLUXION_RENDERER_EXPOSURE_HISTORY; ++slot)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptBindGroups[slot])) Fluxion_RHI_DestroyBindGroup(renderer->exposureAdaptBindGroups[slot]);
        renderer->exposureAdaptBindGroups[slot] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

        FluxionRHIBindGroupEntry entries[5];
        memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->exposureAdaptUniformBuffer;
        entries[0].bufferSize = sizeof(FluxionExposureAdaptUniform);

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[1].textureView = renderer->luminanceSampleViews[FLUXION_RENDERER_LUMINANCE_LEVELS - 1u];

        entries[2].binding = 2;
        entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[2].sampler = renderer->postSampler;

        // THE GROUP IS NAMED BY WHICH SLOT IT WRITES: it reads the OTHER
        // one. A pass cannot read the texture it is drawing into, and
        // what the camera was set to last frame is what this frame moves
        // away from.
        entries[3].binding = 3;
        entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[3].textureView = renderer->exposureSampleViews[(slot + 1u) % FLUXION_RENDERER_EXPOSURE_HISTORY];

        entries[4].binding = 4;
        entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[4].sampler = renderer->postSampler;

        FluxionRHIBindGroupDesc groupDesc;
        memset(&groupDesc, 0, sizeof(groupDesc));
        groupDesc.layout = renderer->exposureAdaptLayout;
        groupDesc.entries = entries;
        groupDesc.entryCount = 5;

        renderer->exposureAdaptBindGroups[slot] = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->exposureAdaptBindGroups[slot])) return false;
    }

    renderer->luminanceBoundSceneColor = renderer->sceneColorSampleView;
    return true;
}

// HOW MUCH OF THE REMAINING DISTANCE THIS FRAME COVERS.
//
// The speed is per second and frames are not, so the two have to be put
// together -- and NOT by multiplying them, which is the version that goes
// wrong. A frame lasting longer than one over the speed would then move
// more than the whole distance and overshoot, and one lasting twice that
// would come out on the far side further away than it started, which
// oscillates rather than settles.
//
// What is left after a second at this speed is one minus the speed; what
// is left after a frame is that raised to the frame's share of a second.
// A frame of any length whatever therefore lands somewhere between where
// it was and where it is going, and never past it.
static f32 FluxionAutoExposure_BlendForFrame(f32 speed, f32 deltaSeconds)
{
    if (deltaSeconds <= 0.0f) return 1.0f;
    if (speed <= 0.0f) return 0.0f;
    if (speed >= 1.0f) return 1.0f;

    const f32 remainingPerSecond = 1.0f - speed;
    const f32 remaining = powf(remainingPerSecond, deltaSeconds);
    return 1.0f - remaining;
}

bool FluxionRendererInternal_AutoExposure_Build(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList)
{
    if (!renderer->autoExposureEnabled) return false;
    if (renderer->autoExposureFailed) return false;
    if (!FLUXION_HANDLE_IS_VALID(renderer->sceneColorTexture)) return false;

    if (!FluxionAutoExposure_EnsureTextures(renderer) || !FluxionAutoExposure_EnsurePipelines(renderer) ||
        !FluxionAutoExposure_EnsureBindGroups(renderer))
    {
        // SAID ONCE. Everything above logs its own reason; what this adds
        // is that the frame will keep drawing without it rather than
        // reporting the same failure sixty times a second.
        FLUXION_LOG_WARN(FLUXION_EXPOSURE_LOG_CATEGORY, "the camera will not adapt; the exposure the view asked for is the whole answer");
        renderer->autoExposureFailed = true;
        return false;
    }

    FluxionVec4 settings;
    f32 deltaSeconds = 0.0f;
    if (!FluxionRendererInternal_RenderView_GetAutoExposure(renderer->currentView, &settings, &deltaSeconds)) return false;

    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // Every step's numbers, written once before any of them run.
    u8* uniforms = (u8*)Fluxion_RHI_MapBuffer(renderer->luminanceUniformBuffer);
    if (uniforms == NULL) return false;

    for (u32 level = 0; level < FLUXION_RENDERER_LUMINANCE_LEVELS; ++level)
    {
        // The size of what is being READ, so the four taps land in the
        // middle of the four texels being merged.
        const u32 sourceWidth = level == 0 ? renderer->sceneColorWidth : FluxionAutoExposure_LevelSize(level - 1u);
        const u32 sourceHeight = level == 0 ? renderer->sceneColorHeight : FluxionAutoExposure_LevelSize(level - 1u);

        FluxionLuminanceUniform uniform;
        memset(&uniform, 0, sizeof(uniform));
        uniform.step.x = sourceWidth > 0 ? 1.0f / (f32)sourceWidth : 0.0f;
        uniform.step.y = sourceHeight > 0 ? 1.0f / (f32)sourceHeight : 0.0f;
        uniform.step.z = level == 0 ? 1.0f : 0.0f;

        memcpy(uniforms + (usize)level * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE, &uniform, sizeof(uniform));
    }
    Fluxion_RHI_UnmapBuffer(renderer->luminanceUniformBuffer);

    FluxionExposureAdaptUniform adaptUniform;
    memset(&adaptUniform, 0, sizeof(adaptUniform));
    adaptUniform.params.x = settings.x;

    // ALL OF IT ON THE FIRST FRAME, whatever the speed says. There is no
    // previous setting to move away from -- what is in the texture is
    // whatever the allocator handed over -- so the measured answer is
    // taken whole rather than eased towards from a number that means
    // nothing.
    adaptUniform.params.y = renderer->exposureHasHistory ? FluxionAutoExposure_BlendForFrame(settings.y, deltaSeconds) : 1.0f;
    adaptUniform.params.z = settings.z;
    adaptUniform.params.w = settings.w;

    void* adaptMapped = Fluxion_RHI_MapBuffer(renderer->exposureAdaptUniformBuffer);
    if (adaptMapped == NULL) return false;
    memcpy(adaptMapped, &adaptUniform, sizeof(adaptUniform));
    Fluxion_RHI_UnmapBuffer(renderer->exposureAdaptUniformBuffer);

    // A texture nobody has drawn into is in no state at all. Said once
    // for every level at once, so that the steps below can each assume
    // the one before left theirs readable.
    if (renderer->luminanceNeedsFirstTransition)
    {
        FluxionRHIBarrier firstUse = { renderer->luminanceTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED,
                                       FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(commandList, &firstUse, 1);
        renderer->luminanceNeedsFirstTransition = false;
    }

    if (renderer->exposureNeedsFirstTransition)
    {
        for (u32 slot = 0; slot < FLUXION_RENDERER_EXPOSURE_HISTORY; ++slot)
        {
            FluxionRHIBarrier firstUse = { renderer->exposureTextures[slot], noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED,
                                           FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
            Fluxion_RHI_CommandList_Barrier(commandList, &firstUse, 1);
        }
        renderer->exposureNeedsFirstTransition = false;
    }

    for (u32 level = 0; level < FLUXION_RENDERER_LUMINANCE_LEVELS; ++level)
    {
        FluxionRHIBarrier toTarget = { renderer->luminanceTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                       FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, level, 1 };
        Fluxion_RHI_CommandList_Barrier(commandList, &toTarget, 1);

        if (level > 0)
        {
            FluxionRHIBarrier toSource = { renderer->luminanceTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                           FLUXION_RHI_RESOURCE_STATE_SHADER_READ, level - 1u, 1 };
            Fluxion_RHI_CommandList_Barrier(commandList, &toSource, 1);
        }

        const u32 size = FluxionAutoExposure_LevelSize(level);

        FluxionRHIRenderingAttachment attachment;
        attachment.view = renderer->luminanceTargetViews[level];
        attachment.clear = false;
        attachment.clearColor[0] = 0.0f;
        attachment.clearColor[1] = 0.0f;
        attachment.clearColor[2] = 0.0f;
        attachment.clearColor[3] = 1.0f;

        FluxionRHIRenderingDesc renderingDesc;
        renderingDesc.colorAttachments = &attachment;
        renderingDesc.colorAttachmentCount = 1;
        renderingDesc.depthAttachment = NULL;
        renderingDesc.width = size;
        renderingDesc.height = size;

        Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
        Fluxion_RHI_CommandList_SetViewport(commandList, 0.0f, 0.0f, (f32)size, (f32)size, 0.0f, 1.0f);
        Fluxion_RHI_CommandList_SetScissor(commandList, 0, 0, size, size);
        Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->luminancePipeline);
        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, renderer->luminanceBindGroups[level]);
        Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->postVertexBuffer, 0);
        Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
        Fluxion_RHI_CommandList_EndRendering(commandList);
    }

    // The last level is written and nothing has read it yet.
    FluxionRHIBarrier lastToSource = { renderer->luminanceTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                       FLUXION_RHI_RESOURCE_STATE_SHADER_READ, FLUXION_RENDERER_LUMINANCE_LEVELS - 1u, 1 };
    Fluxion_RHI_CommandList_Barrier(commandList, &lastToSource, 1);

    // The slot that is not holding the current answer is the one this
    // frame writes -- and the group for it reads the one that is.
    const u32 target = (renderer->exposureCurrent + 1u) % FLUXION_RENDERER_EXPOSURE_HISTORY;

    FluxionRHIBarrier exposureToTarget = { renderer->exposureTextures[target], noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                           FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &exposureToTarget, 1);

    FluxionRHIRenderingAttachment attachment;
    attachment.view = renderer->exposureTargetViews[target];
    attachment.clear = false;
    attachment.clearColor[0] = 0.0f;
    attachment.clearColor[1] = 0.0f;
    attachment.clearColor[2] = 0.0f;
    attachment.clearColor[3] = 1.0f;

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = &attachment;
    renderingDesc.colorAttachmentCount = 1;
    renderingDesc.depthAttachment = NULL;
    renderingDesc.width = 1;
    renderingDesc.height = 1;

    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
    Fluxion_RHI_CommandList_SetViewport(commandList, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    Fluxion_RHI_CommandList_SetScissor(commandList, 0, 0, 1, 1);
    Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->exposureAdaptPipeline);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, renderer->exposureAdaptBindGroups[target]);
    Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->postVertexBuffer, 0);
    Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
    Fluxion_RHI_CommandList_EndRendering(commandList);

    FluxionRHIBarrier exposureToRead = { renderer->exposureTextures[target], noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                         FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &exposureToRead, 1);

    renderer->exposureCurrent = target;
    renderer->exposureHasHistory = true;
    return true;
}

FluxionRHITextureViewHandle FluxionRendererInternal_AutoExposure_GetView(const FluxionRenderer* renderer)
{
    return renderer->exposureSampleViews[renderer->exposureCurrent];
}
