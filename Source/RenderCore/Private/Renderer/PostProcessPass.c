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

// WHERE THE PICTURE STOPS BEING AN AMOUNT OF LIGHT.
//
// The scene is drawn into a target that holds light: sixteen bits a
// channel, values far above one, no camera setting applied and no curve.
// This pass reads that and writes what a screen should show.
//
// The reason for the extra target is not the curve -- a shader could
// apply that where it stands, and this engine's did. It is everything
// that wants to READ the picture as light: what glows and by how much,
// how bright the frame is on average, what a reflection carries. All of
// that has nothing to read once the values have been squashed into the
// range a monitor can show, and squashing them is exactly what the
// material shaders used to do.

#include "RendererInternal.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPass.h>

#include <string.h>

#define FLUXION_POST_LOG_CATEGORY "Renderer"

static const char* const kPostVertexSource = "#include \"Fluxion/Pass/FullscreenVertex.jsl\"\n";
static const char* const kPostFragmentSource = "#include \"Fluxion/Pass/PostProcess.jsl\"\n";
static const char* const kBloomDownFragmentSource = "#include \"Fluxion/Pass/BloomDown.jsl\"\n";
static const char* const kBloomUpFragmentSource = "#include \"Fluxion/Pass/BloomUp.jsl\"\n";

// Three vertices in clip space, big enough that the triangle covers the
// screen and the corners fall outside it.
static const f32 kPostFullscreenTriangle[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };

// What the resolve is told about the frame -- see PostProcess.jsl for
// what each component means.
typedef struct FluxionPostUniform
{
    FluxionVec4 params;

    // x: how much of the glow is added back. Zero is the whole effect
    //    switched off, and the resolve then adds nothing whatever is
    //    bound beside it.
    FluxionVec4 bloom;

    // The grading, in the order PostProcess.jsl declares it. THE ORDER IS
    // THE CONTRACT: a member added here without a matching uniform there
    // moves everything after it, and nothing says so out loud -- the
    // picture merely comes out wrong.
    FluxionVec4 balance;
    FluxionVec4 lift;
    FluxionVec4 gamma;
    FluxionVec4 gain;
} FluxionPostUniform;

// What one step of the bloom chain is told -- see BloomDown.jsl.
typedef struct FluxionBloomUniform
{
    FluxionVec4 step;
    FluxionVec4 curve;
} FluxionBloomUniform;

// THE THREE VERTICES EVERY FULLSCREEN PASS DRAWS, and there is one of
// them for the whole renderer.
//
// It lives here because this is where the triangle is written down, and
// it is a function rather than a line inside the resolve's setup because
// the passes that run BEFORE the resolve need it too -- the occlusion is
// worked out before anything is lit, and on the first frame the resolve
// has not run even once by then. Measured, when it was the resolve's
// alone: the occlusion drew from a buffer that did not exist yet, which
// one backend answered by walking into a null pointer.
void FluxionRendererInternal_EnsureFullscreenTriangle(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->postVertexBuffer)) return;

    FluxionRHIBufferDesc vertexDesc;
    memset(&vertexDesc, 0, sizeof(vertexDesc));
    vertexDesc.size = sizeof(kPostFullscreenTriangle);
    vertexDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER;
    vertexDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    vertexDesc.debugName = "Fluxion.Renderer.FullscreenTriangle";
    renderer->postVertexBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &vertexDesc);

    if (FLUXION_HANDLE_IS_VALID(renderer->postVertexBuffer))
    {
        void* mapped = Fluxion_RHI_MapBuffer(renderer->postVertexBuffer);
        if (mapped != NULL)
        {
            memcpy(mapped, kPostFullscreenTriangle, sizeof(kPostFullscreenTriangle));
            Fluxion_RHI_UnmapBuffer(renderer->postVertexBuffer);
        }
    }
}

static FluxionRHIBindGroupLayoutDesc FluxionPostProcessPass_MakeLayoutDesc(void)
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

    desc.entries[3].binding = 3;
    desc.entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[3].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[4].binding = 4;
    desc.entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[4].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[5].binding = 5;
    desc.entries[5].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[5].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[6].binding = 6;
    desc.entries[6].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[6].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entryCount = 7;
    desc.debugName = "Fluxion.Renderer.PostProcessLayout";
    return desc;
}

// The chain's own steps read one texture and write another, which is the
// three the resolve's first three entries already describe -- so they
// share the shape and each keeps its own groups.
static FluxionRHIBindGroupLayoutDesc FluxionBloom_MakeLayoutDesc(void)
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
    desc.debugName = "Fluxion.Renderer.BloomLayout";
    return desc;
}

static u32 FluxionBloom_LevelSize(u32 size, u32 level)
{
    const u32 shifted = size >> level;
    return shifted > 0 ? shifted : 1;
}

// ---------------------------------------------------------------------
// The target the scene is drawn into.
// ---------------------------------------------------------------------

void FluxionRendererInternal_PostProcess_ReleaseSceneColor(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->sceneColorView)) Fluxion_RHI_DestroyTextureView(renderer->sceneColorView);
    if (FLUXION_HANDLE_IS_VALID(renderer->sceneColorSampleView)) Fluxion_RHI_DestroyTextureView(renderer->sceneColorSampleView);
    if (FLUXION_HANDLE_IS_VALID(renderer->sceneColorTexture)) Fluxion_RHI_DestroyTexture(renderer->sceneColorTexture);

    renderer->sceneColorView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->sceneColorSampleView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->sceneColorTexture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->sceneColorWidth = 0;
    renderer->sceneColorHeight = 0;
    renderer->sceneColorIsUndefined = true;

    if (FLUXION_HANDLE_IS_VALID(renderer->postBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->postBindGroup);
    renderer->postBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

bool FluxionRendererInternal_PostProcess_EnsureSceneColor(FluxionRenderer* renderer, u32 width, u32 height)
{
    if (width == 0 || height == 0) return false;
    if (renderer->sceneColorWidth == width && renderer->sceneColorHeight == height &&
        FLUXION_HANDLE_IS_VALID(renderer->sceneColorTexture))
    {
        return true;
    }

    FluxionRendererInternal_PostProcess_ReleaseSceneColor(renderer);

    FluxionRHITextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    desc.format = FLUXION_RENDERER_SCENE_COLOR_FORMAT;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "Fluxion.Renderer.SceneColor";

    renderer->sceneColorTexture = Fluxion_RHI_CreateTexture(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->sceneColorTexture))
    {
        FLUXION_LOG_ERROR(FLUXION_POST_LOG_CATEGORY, "the scene could not be given a target to be drawn into at %ux%u", width, height);
        return false;
    }

    // Two views of one texture, for the same reason the depth pyramid has
    // two per level: what is attached and what is sampled are the same
    // pixels at different moments, and one view cannot be both at once.
    FluxionRHITextureViewDesc viewDesc;
    memset(&viewDesc, 0, sizeof(viewDesc));
    viewDesc.texture = renderer->sceneColorTexture;
    viewDesc.format = FLUXION_RENDERER_SCENE_COLOR_FORMAT;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

    renderer->sceneColorView = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
    renderer->sceneColorSampleView = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);

    if (!FLUXION_HANDLE_IS_VALID(renderer->sceneColorView) || !FLUXION_HANDLE_IS_VALID(renderer->sceneColorSampleView))
    {
        FLUXION_LOG_ERROR(FLUXION_POST_LOG_CATEGORY, "the scene's target could not be looked at");
        FluxionRendererInternal_PostProcess_ReleaseSceneColor(renderer);
        return false;
    }

    renderer->sceneColorWidth = width;
    renderer->sceneColorHeight = height;

    // A texture nobody has drawn into is in no state at all, and the
    // first barrier that moves it must say so rather than claim it was a
    // render target already.
    renderer->sceneColorIsUndefined = true;
    return true;
}

// ---------------------------------------------------------------------
// The chain of ever smaller pictures.
// ---------------------------------------------------------------------

static void FluxionBloom_Release(FluxionRenderer* renderer)
{
    for (u32 level = 0; level < FLUXION_RENDERER_MAX_BLOOM_LEVELS; ++level)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->bloomTargetViews[level])) Fluxion_RHI_DestroyTextureView(renderer->bloomTargetViews[level]);
        if (FLUXION_HANDLE_IS_VALID(renderer->bloomSampleViews[level])) Fluxion_RHI_DestroyTextureView(renderer->bloomSampleViews[level]);
        renderer->bloomTargetViews[level] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->bloomSampleViews[level] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    for (u32 step = 0; step < FLUXION_RENDERER_MAX_BLOOM_STEPS; ++step)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->bloomBindGroups[step])) Fluxion_RHI_DestroyBindGroup(renderer->bloomBindGroups[step]);
        renderer->bloomBindGroups[step] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->bloomTexture)) Fluxion_RHI_DestroyTexture(renderer->bloomTexture);
    renderer->bloomTexture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    renderer->bloomLevels = 0;
    renderer->bloomWidth = 0;
    renderer->bloomHeight = 0;
    renderer->bloomBoundSceneColor = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

// HALF THE FRAME, and levels down from there.
//
// Half rather than full: a glow is the opposite of detail, and the half
// that is thrown away here is detail. It also makes every step after it
// a quarter of the work.
static bool FluxionBloom_EnsureChain(FluxionRenderer* renderer, u32 sceneWidth, u32 sceneHeight)
{
    const u32 width = sceneWidth > 1 ? sceneWidth / 2 : 1;
    const u32 height = sceneHeight > 1 ? sceneHeight / 2 : 1;

    if (renderer->bloomWidth == width && renderer->bloomHeight == height && FLUXION_HANDLE_IS_VALID(renderer->bloomTexture))
    {
        return true;
    }

    FluxionBloom_Release(renderer);

    // As many halvings as there are, up to the limit -- and at least two,
    // because a chain of one has nothing to add on the way back up.
    u32 levels = 1;
    while (levels < FLUXION_RENDERER_MAX_BLOOM_LEVELS &&
           FluxionBloom_LevelSize(width, levels) > 1 && FluxionBloom_LevelSize(height, levels) > 1)
    {
        ++levels;
    }
    if (levels < 2) return false;

    FluxionRHITextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = levels;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    desc.format = FLUXION_RENDERER_SCENE_COLOR_FORMAT;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "Fluxion.Renderer.Bloom";

    renderer->bloomTexture = Fluxion_RHI_CreateTexture(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->bloomTexture))
    {
        FLUXION_LOG_ERROR(FLUXION_POST_LOG_CATEGORY, "the glow has nowhere to be built at %ux%u", width, height);
        return false;
    }

    for (u32 level = 0; level < levels; ++level)
    {
        FluxionRHITextureViewDesc viewDesc;
        memset(&viewDesc, 0, sizeof(viewDesc));
        viewDesc.texture = renderer->bloomTexture;
        viewDesc.format = FLUXION_RENDERER_SCENE_COLOR_FORMAT;
        viewDesc.baseMipLevel = level;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

        renderer->bloomTargetViews[level] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        renderer->bloomSampleViews[level] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);

        if (!FLUXION_HANDLE_IS_VALID(renderer->bloomTargetViews[level]) || !FLUXION_HANDLE_IS_VALID(renderer->bloomSampleViews[level]))
        {
            FLUXION_LOG_ERROR(FLUXION_POST_LOG_CATEGORY, "the glow's levels could not be looked at");
            FluxionBloom_Release(renderer);
            return false;
        }
    }

    renderer->bloomLevels = levels;
    renderer->bloomWidth = width;
    renderer->bloomHeight = height;

    // Nothing has been drawn into it, so it is in no state at all until
    // the first frame says otherwise.
    renderer->bloomNeedsFirstTransition = true;
    return true;
}

FluxionRHITextureViewHandle FluxionRendererInternal_PostProcess_GetSceneColorView(const FluxionRenderer* renderer)
{
    return renderer->sceneColorView;
}

// ---------------------------------------------------------------------
// The pass.
// ---------------------------------------------------------------------

static bool FluxionPostProcessPass_EnsureResources(FluxionRenderer* renderer)
{
    if (renderer->postFailed) return false;
    if (FLUXION_HANDLE_IS_VALID(renderer->postPipeline)) return true;

    FluxionShaderProgramDesc programDesc;
    memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "Fluxion.Renderer.PostProcess";
    programDesc.vertexSource = kPostVertexSource;
    programDesc.fragmentSource = kPostFragmentSource;

    renderer->postProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->postProgram))
    {
        FLUXION_LOG_ERROR(FLUXION_POST_LOG_CATEGORY, "the resolve shader could not be built; nothing will reach the screen");
        renderer->postFailed = true;
        return false;
    }

    const FluxionRHIBindGroupLayoutDesc layoutDesc = FluxionPostProcessPass_MakeLayoutDesc();
    renderer->postLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &layoutDesc);

    FluxionRHISamplerDesc samplerDesc;
    memset(&samplerDesc, 0, sizeof(samplerDesc));
    samplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "Fluxion.Renderer.PostProcessSampler";
    renderer->postSampler = Fluxion_RHI_CreateSampler(renderer->device, &samplerDesc);

    FluxionRendererInternal_EnsureFullscreenTriangle(renderer);

    FluxionRHIBufferDesc uniformDesc;
    memset(&uniformDesc, 0, sizeof(uniformDesc));
    uniformDesc.size = FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    uniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    uniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    uniformDesc.debugName = "Fluxion.Renderer.PostProcessParams";
    renderer->postUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &uniformDesc);

    FluxionRHIVertexLayout vertexLayout;
    memset(&vertexLayout, 0, sizeof(vertexLayout));
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    vertexLayout.attributes[0].offset = 0;
    vertexLayout.attributeCount = 1;
    vertexLayout.stride = 2 * sizeof(f32);

    FluxionRHIGraphicsPipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->postProgram);
    pipelineDesc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->postProgram);
    pipelineDesc.vertexLayout = vertexLayout;
    pipelineDesc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    pipelineDesc.rasterState.frontFaceCounterClockwise = false;
    pipelineDesc.rasterState.wireframe = false;
    pipelineDesc.depthState.testEnable = false;
    pipelineDesc.depthState.writeEnable = false;
    pipelineDesc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_ALWAYS;
    pipelineDesc.blendState.mode = FLUXION_RHI_BLEND_MODE_NONE;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.colorFormats[0] = renderer->postOutputFormat;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = FLUXION_RHI_FORMAT_UNKNOWN;

    const FluxionRHIBindGroupLayoutHandle noLayout = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = renderer->postLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = noLayout;
    pipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_GLOBAL + 1;
    pipelineDesc.debugName = "Fluxion.Renderer.PostProcessPipeline";

    renderer->postPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &pipelineDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->postPipeline) || !FLUXION_HANDLE_IS_VALID(renderer->postVertexBuffer) ||
        !FLUXION_HANDLE_IS_VALID(renderer->postUniformBuffer) || !FLUXION_HANDLE_IS_VALID(renderer->postSampler))
    {
        FLUXION_LOG_ERROR(FLUXION_POST_LOG_CATEGORY, "the resolve pass could not be set up; nothing will reach the screen");
        renderer->postFailed = true;
        return false;
    }
    return true;
}

// The two draws the chain is made of. Same shape, same layout, one
// difference: the way up ADDS to what is already there.
static bool FluxionBloom_EnsurePipelines(FluxionRenderer* renderer)
{
    if (renderer->bloomFailed) return false;
    if (FLUXION_HANDLE_IS_VALID(renderer->bloomDownPipeline) && FLUXION_HANDLE_IS_VALID(renderer->bloomUpPipeline)) return true;

    FluxionShaderProgramDesc downDesc;
    memset(&downDesc, 0, sizeof(downDesc));
    downDesc.debugName = "Fluxion.Renderer.BloomDown";
    downDesc.vertexSource = kPostVertexSource;
    downDesc.fragmentSource = kBloomDownFragmentSource;
    renderer->bloomDownProgram = Fluxion_ShaderProgram_Create(renderer->device, &downDesc);

    FluxionShaderProgramDesc upDesc;
    memset(&upDesc, 0, sizeof(upDesc));
    upDesc.debugName = "Fluxion.Renderer.BloomUp";
    upDesc.vertexSource = kPostVertexSource;
    upDesc.fragmentSource = kBloomUpFragmentSource;
    renderer->bloomUpProgram = Fluxion_ShaderProgram_Create(renderer->device, &upDesc);

    if (!FLUXION_HANDLE_IS_VALID(renderer->bloomDownProgram) || !FLUXION_HANDLE_IS_VALID(renderer->bloomUpProgram))
    {
        FLUXION_LOG_ERROR(FLUXION_POST_LOG_CATEGORY, "the glow's shaders could not be built; nothing will glow");
        renderer->bloomFailed = true;
        return false;
    }

    const FluxionRHIBindGroupLayoutDesc layoutDesc = FluxionBloom_MakeLayoutDesc();
    renderer->bloomLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &layoutDesc);

    FluxionRHIBufferDesc uniformDesc;
    memset(&uniformDesc, 0, sizeof(uniformDesc));
    uniformDesc.size = (usize)FLUXION_RENDERER_MAX_BLOOM_STEPS * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    uniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    uniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    uniformDesc.debugName = "Fluxion.Renderer.BloomSteps";
    renderer->bloomUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &uniformDesc);

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
    pipelineDesc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->bloomDownProgram);
    pipelineDesc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->bloomDownProgram);
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
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = renderer->bloomLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = noLayout;
    pipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_GLOBAL + 1;
    pipelineDesc.debugName = "Fluxion.Renderer.BloomDownPipeline";
    renderer->bloomDownPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &pipelineDesc);

    // The one difference.
    pipelineDesc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->bloomUpProgram);
    pipelineDesc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->bloomUpProgram);
    pipelineDesc.blendState.mode = FLUXION_RHI_BLEND_MODE_ADD;
    pipelineDesc.debugName = "Fluxion.Renderer.BloomUpPipeline";
    renderer->bloomUpPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &pipelineDesc);

    if (!FLUXION_HANDLE_IS_VALID(renderer->bloomDownPipeline) || !FLUXION_HANDLE_IS_VALID(renderer->bloomUpPipeline) ||
        !FLUXION_HANDLE_IS_VALID(renderer->bloomUniformBuffer))
    {
        FLUXION_LOG_ERROR(FLUXION_POST_LOG_CATEGORY, "the glow could not be set up; nothing will glow");
        renderer->bloomFailed = true;
        return false;
    }
    return true;
}

// One group per step, remade when what they read changes -- which is when
// the frame changes size, and never per frame.
static bool FluxionBloom_EnsureBindGroups(FluxionRenderer* renderer)
{
    if (renderer->bloomBoundSceneColor.index == renderer->sceneColorSampleView.index &&
        renderer->bloomBoundSceneColor.generation == renderer->sceneColorSampleView.generation &&
        FLUXION_HANDLE_IS_VALID(renderer->bloomBindGroups[0]))
    {
        return true;
    }

    for (u32 step = 0; step < FLUXION_RENDERER_MAX_BLOOM_STEPS; ++step)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->bloomBindGroups[step])) Fluxion_RHI_DestroyBindGroup(renderer->bloomBindGroups[step]);
        renderer->bloomBindGroups[step] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    const u32 levels = renderer->bloomLevels;
    const u32 stepCount = levels * 2u - 1u;

    for (u32 step = 0; step < stepCount; ++step)
    {
        // The way down reads the frame first and then each level it has
        // just written; the way up reads the level below the one it is
        // adding to.
        FluxionRHITextureViewHandle source;
        if (step == 0) source = renderer->sceneColorSampleView;
        else if (step < levels) source = renderer->bloomSampleViews[step - 1];
        else source = renderer->bloomSampleViews[levels - 1 - (step - levels)];

        FluxionRHIBindGroupEntry entries[3];
        memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->bloomUniformBuffer;
        entries[0].bufferOffset = (usize)step * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
        entries[0].bufferSize = sizeof(FluxionBloomUniform);

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[1].textureView = source;

        entries[2].binding = 2;
        entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[2].sampler = renderer->postSampler;

        FluxionRHIBindGroupDesc groupDesc;
        groupDesc.layout = renderer->bloomLayout;
        groupDesc.entries = entries;
        groupDesc.entryCount = 3;

        renderer->bloomBindGroups[step] = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->bloomBindGroups[step])) return false;
    }

    renderer->bloomBoundSceneColor = renderer->sceneColorSampleView;
    return true;
}

// One bind group, kept, remade only when the texture it reads changes --
// which is when the window is resized. A group per frame is what
// exhausted a descriptor heap the last time this engine made one.
static bool FluxionPostProcessPass_EnsureBindGroup(FluxionRenderer* renderer, FluxionRHITextureViewHandle glow,
                                                  FluxionRHITextureViewHandle exposure)
{
    if (renderer->postBoundSceneColor.index == renderer->sceneColorSampleView.index &&
        renderer->postBoundSceneColor.generation == renderer->sceneColorSampleView.generation &&
        renderer->postBoundGlow.index == glow.index && renderer->postBoundGlow.generation == glow.generation &&
        renderer->postBoundExposure.index == exposure.index && renderer->postBoundExposure.generation == exposure.generation &&
        FLUXION_HANDLE_IS_VALID(renderer->postBindGroup))
    {
        return true;
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->postBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->postBindGroup);
    renderer->postBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBindGroupEntry entries[7];
    memset(entries, 0, sizeof(entries));

    entries[0].binding = 0;
    entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    entries[0].buffer = renderer->postUniformBuffer;
    entries[0].bufferOffset = 0;
    entries[0].bufferSize = sizeof(FluxionPostUniform);

    entries[1].binding = 1;
    entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[1].textureView = renderer->sceneColorSampleView;

    entries[2].binding = 2;
    entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[2].sampler = renderer->postSampler;

    // THE GLOW, OR THE SCENE ITSELF WHEN THERE IS NONE.
    //
    // A binding must name a texture whether or not the shader reads it,
    // and the frame already has one of the right kind to hand. Binding
    // the scene a second time costs nothing and saves a one-pixel
    // stand-in texture that exists only to be multiplied by zero -- which
    // is what the resolve does with it when nothing glows.
    entries[3].binding = 3;
    entries[3].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[3].textureView = glow;

    entries[4].binding = 4;
    entries[4].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[4].sampler = renderer->postSampler;

    // AND THE CAMERA'S OWN SETTING, on the same terms as the glow above:
    // the scene stands in when nothing measured it, and the resolve
    // mixes it out rather than branching around it.
    entries[5].binding = 5;
    entries[5].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[5].textureView = exposure;

    entries[6].binding = 6;
    entries[6].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[6].sampler = renderer->postSampler;

    FluxionRHIBindGroupDesc groupDesc;
    groupDesc.layout = renderer->postLayout;
    groupDesc.entries = entries;
    groupDesc.entryCount = 7;

    renderer->postBindGroup = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->postBindGroup)) return false;

    renderer->postBoundSceneColor = renderer->sceneColorSampleView;
    renderer->postBoundGlow = glow;
    renderer->postBoundExposure = exposure;
    return true;
}

// DOWN, THEN BACK UP, IN ONE GO.
//
// The way down halves the picture level by level, the first step also
// deciding what is bright enough to glow at all. The way up spreads each
// small level over the larger one it came from and ADDS -- so the widest
// blur and the narrowest end up in the same picture, which is what makes
// the falloff smooth.
//
// Returns whether level zero ended up holding a glow the resolve can add.
static bool FluxionBloom_Build(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList)
{
    if (!renderer->bloomEnabled) return false;
    if (!FLUXION_HANDLE_IS_VALID(renderer->sceneColorTexture)) return false;
    if (!FluxionBloom_EnsurePipelines(renderer)) return false;
    if (!FluxionBloom_EnsureChain(renderer, renderer->sceneColorWidth, renderer->sceneColorHeight)) return false;
    if (!FluxionBloom_EnsureBindGroups(renderer)) return false;

    const u32 levels = renderer->bloomLevels;
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionVec4 curve;
    FluxionRendererInternal_RenderView_GetBloom(renderer->currentView, &curve);

    // Every step's numbers, written once before any of them run: the
    // buffer is one allocation with a slice per step, and a map per step
    // would be a map per draw.
    u8* uniforms = (u8*)Fluxion_RHI_MapBuffer(renderer->bloomUniformBuffer);
    if (uniforms == NULL) return false;

    for (u32 step = 0; step < levels * 2u - 1u; ++step)
    {
        const bool goingDown = step < levels;

        // Which level is being READ, and therefore whose texel size the
        // shader steps by.
        u32 sourceWidth;
        u32 sourceHeight;
        if (step == 0)
        {
            sourceWidth = renderer->sceneColorWidth;
            sourceHeight = renderer->sceneColorHeight;
        }
        else
        {
            const u32 sourceLevel = goingDown ? (step - 1u) : (levels - 1u - (step - levels));
            sourceWidth = FluxionBloom_LevelSize(renderer->bloomWidth, sourceLevel);
            sourceHeight = FluxionBloom_LevelSize(renderer->bloomHeight, sourceLevel);
        }

        FluxionBloomUniform uniform;
        memset(&uniform, 0, sizeof(uniform));
        uniform.step.x = 1.0f / (f32)sourceWidth;
        uniform.step.y = 1.0f / (f32)sourceHeight;
        uniform.step.z = (step == 0) ? 1.0f : 0.0f;

        // Which way this backend's rows run -- the same answer the
        // resolve is given, for the same reason, and every step of the
        // chain needs it rather than just the first.
        uniform.step.w = Fluxion_RHI_GetDeviceBackendType(renderer->device) == FLUXION_RHI_BACKEND_OPENGL ? 1.0f : 0.0f;

        uniform.curve = curve;

        memcpy(uniforms + (usize)step * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE, &uniform, sizeof(uniform));
    }
    Fluxion_RHI_UnmapBuffer(renderer->bloomUniformBuffer);

    // A texture nobody has drawn into is in no state at all. Said once,
    // for every level at once, so that the steps below can each assume
    // the one before left theirs readable.
    if (renderer->bloomNeedsFirstTransition)
    {
        FluxionRHIBarrier firstUse = { renderer->bloomTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED,
                                       FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(commandList, &firstUse, 1);
        renderer->bloomNeedsFirstTransition = false;
    }

    for (u32 step = 0; step < levels * 2u - 1u; ++step)
    {
        const bool goingDown = step < levels;
        const u32 targetLevel = goingDown ? step : (levels - 2u - (step - levels));
        const u32 sourceLevel = goingDown ? (step == 0 ? 0u : step - 1u) : (targetLevel + 1u);

        // The level about to be written becomes writable, and the level
        // about to be read becomes readable. The scene's own target is
        // not touched here: the graph already handed it over.
        FluxionRHIBarrier toTarget = { renderer->bloomTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                       FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, targetLevel, 1 };
        Fluxion_RHI_CommandList_Barrier(commandList, &toTarget, 1);

        if (step > 0)
        {
            FluxionRHIBarrier toSource = { renderer->bloomTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                           FLUXION_RHI_RESOURCE_STATE_SHADER_READ, sourceLevel, 1 };
            Fluxion_RHI_CommandList_Barrier(commandList, &toSource, 1);
        }

        const u32 targetWidth = FluxionBloom_LevelSize(renderer->bloomWidth, targetLevel);
        const u32 targetHeight = FluxionBloom_LevelSize(renderer->bloomHeight, targetLevel);

        FluxionRHIRenderingAttachment attachment;
        attachment.view = renderer->bloomTargetViews[targetLevel];

        // CLEARED ON THE WAY DOWN, KEPT ON THE WAY UP: going down each
        // level is written from nothing, and going up each level is added
        // to what the way down left in it.
        attachment.clear = goingDown;
        attachment.clearColor[0] = 0.0f;
        attachment.clearColor[1] = 0.0f;
        attachment.clearColor[2] = 0.0f;
        attachment.clearColor[3] = 1.0f;

        FluxionRHIRenderingDesc renderingDesc;
        renderingDesc.colorAttachments = &attachment;
        renderingDesc.colorAttachmentCount = 1;
        renderingDesc.depthAttachment = NULL;
        renderingDesc.width = targetWidth;
        renderingDesc.height = targetHeight;

        Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
        Fluxion_RHI_CommandList_SetViewport(commandList, 0.0f, 0.0f, (f32)targetWidth, (f32)targetHeight, 0.0f, 1.0f);
        Fluxion_RHI_CommandList_SetScissor(commandList, 0, 0, targetWidth, targetHeight);
        Fluxion_RHI_CommandList_SetPipeline(commandList, goingDown ? renderer->bloomDownPipeline : renderer->bloomUpPipeline);
        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, renderer->bloomBindGroups[step]);
        Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->postVertexBuffer, 0);
        Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
        Fluxion_RHI_CommandList_EndRendering(commandList);
    }

    // And the top of the chain, which the resolve is about to read.
    FluxionRHIBarrier toRead = { renderer->bloomTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                 FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 1 };
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);
    return true;
}

void FluxionPostProcessPass_Setup(FluxionRenderGraphBuilder* builder, void* userData)
{
    const FluxionRenderer* renderer = (const FluxionRenderer*)userData;
    if (renderer == NULL) return;

    // What the scene was drawn into, and where it goes. The first name is
    // the one the forward pass writes -- that is what puts this pass
    // after it, and what hands the target over from being drawn into to
    // being read.
    Fluxion_RenderGraphBuilder_ReadTexture(builder, "ForwardOpaquePass.Color0");
    Fluxion_RenderGraphBuilder_WriteColorTarget(builder, "PostProcessPass.Color");
}

void FluxionPostProcessPass_Execute(FluxionRHICommandListHandle commandList, void* userData)
{
    FluxionRenderer* renderer = (FluxionRenderer*)userData;
    if (renderer == NULL) return;
    if (!FLUXION_HANDLE_IS_VALID(renderer->sceneColorTexture)) return;
    if (!FluxionPostProcessPass_EnsureResources(renderer)) return;

    // The glow is built first, because the resolve adds it.
    const bool glowing = FluxionBloom_Build(renderer, commandList);
    const FluxionRHITextureViewHandle glow = glowing ? renderer->bloomSampleViews[0] : renderer->sceneColorSampleView;

    // And the camera is set from the same scene, before the scene stops
    // being an amount of light -- which is the only point at which the
    // question "how bright was this frame" has an answer.
    const bool measured = FluxionRendererInternal_AutoExposure_Build(renderer, commandList);
    const FluxionRHITextureViewHandle exposure =
        measured ? FluxionRendererInternal_AutoExposure_GetView(renderer) : renderer->sceneColorSampleView;

    if (!FluxionPostProcessPass_EnsureBindGroup(renderer, glow, exposure)) return;

    FluxionRenderTargetHandle renderTarget = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRendererInternal_RenderView_Get(renderer->currentView, &renderTarget, NULL, NULL);

    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount = 0;
    FluxionRHITextureViewHandle depthView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!FluxionRendererInternal_RenderTarget_Get(renderTarget, colorViews, &colorViewCount, &depthView) || colorViewCount == 0) return;

    FluxionViewport viewport = { 0 };
    FluxionRendererInternal_RenderView_GetViewport(renderer->currentView, &viewport);

    // WHERE THIS PASS WRITES: the screen, or a texture one more pass
    // reads. Asked before the uniform below is filled in, because the
    // answer changes what goes in it.
    const bool smoothing =
        FluxionRendererInternal_FXAA_Begin(renderer, commandList, (u32)viewport.width, (u32)viewport.height);
    const FluxionRHITextureViewHandle resolveTarget =
        smoothing ? FluxionRendererInternal_FXAA_GetTargetView(renderer) : colorViews[0];

    // What the frame decided about the camera, handed to the one place
    // that acts on it.
    FluxionPostUniform uniform;
    memset(&uniform, 0, sizeof(uniform));
    FluxionRendererInternal_RenderView_GetToneMapping(renderer->currentView, &uniform.params);

    // Which way this backend's rows run -- see PostProcess.jsl. Asked of
    // the device rather than guessed, and asked WHATEVER THIS PASS IS
    // WRITING INTO: the rule is one per fullscreen pass, not one per
    // frame. Making this one conditional on being the last pass was tried
    // and put the picture upside down the moment another pass followed
    // it.
    uniform.params.w = Fluxion_RHI_GetDeviceBackendType(renderer->device) == FLUXION_RHI_BACKEND_OPENGL ? 1.0f : 0.0f;

    // How much of it is added back -- and zero when there is none, which
    // is what makes binding the scene in its place harmless.
    FluxionVec4 bloomCurve;
    FluxionRendererInternal_RenderView_GetBloom(renderer->currentView, &bloomCurve);
    uniform.bloom.x = glowing ? bloomCurve.z : 0.0f;

    // Whether what is bound beside the glow is a camera setting or the
    // scene standing in for one. See the resolve: it mixes rather than
    // branches, so this is the whole of the difference.
    uniform.bloom.y = measured ? 1.0f : 0.0f;

    // AND THE GRADING, WHICH IS NEUTRAL RATHER THAN ABSENT when the view
    // has nothing to say: the shader runs the same arithmetic either way,
    // so there is no second path through it to keep in step with this
    // one. Neutral is a one in four places and a zero in the rest, which
    // is why it cannot be left to the memset above.
    if (!FluxionRendererInternal_RenderView_GetGrading(renderer->currentView, &uniform.balance, &uniform.lift,
                                                      &uniform.gamma, &uniform.gain))
    {
        uniform.balance.z = 1.0f;
        uniform.balance.w = 1.0f;
        uniform.gamma.x = 1.0f;
        uniform.gamma.y = 1.0f;
        uniform.gamma.z = 1.0f;
        uniform.gain.x = 1.0f;
        uniform.gain.y = 1.0f;
        uniform.gain.z = 1.0f;
    }

    void* mapped = Fluxion_RHI_MapBuffer(renderer->postUniformBuffer);
    if (mapped != NULL)
    {
        memcpy(mapped, &uniform, sizeof(uniform));
        Fluxion_RHI_UnmapBuffer(renderer->postUniformBuffer);
    }

    // NO BARRIER HERE. The scene's target is a resource the graph knows
    // about: this pass declares reading it and the forward pass declares
    // writing it, so the move from one to the other is the graph's to
    // make. A second one here would claim a state the graph had already
    // changed, which is what the contract checks for and says out loud.
    FluxionRHIRenderingAttachment attachment;
    attachment.view = resolveTarget;
    attachment.clear = false;
    attachment.clearColor[0] = 0.0f;
    attachment.clearColor[1] = 0.0f;
    attachment.clearColor[2] = 0.0f;
    attachment.clearColor[3] = 1.0f;

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = &attachment;
    renderingDesc.colorAttachmentCount = 1;
    renderingDesc.depthAttachment = NULL;
    renderingDesc.width = (u32)viewport.width;
    renderingDesc.height = (u32)viewport.height;

    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
    Fluxion_RHI_CommandList_SetViewport(commandList, 0.0f, 0.0f, viewport.width, viewport.height, 0.0f, 1.0f);
    Fluxion_RHI_CommandList_SetScissor(commandList, 0, 0, (u32)viewport.width, (u32)viewport.height);
    Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->postPipeline);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, renderer->postBindGroup);
    Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->postVertexBuffer, 0);
    Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
    Fluxion_RHI_CommandList_EndRendering(commandList);

    // And the one more pass, when there is one.
    if (smoothing)
    {
        FluxionRendererInternal_FXAA_Resolve(renderer, commandList, colorViews[0], (u32)viewport.width, (u32)viewport.height);
    }
}

void FluxionRendererInternal_PostProcess_Shutdown(FluxionRenderer* renderer)
{
    FluxionRendererInternal_PostProcess_ReleaseSceneColor(renderer);
    FluxionBloom_Release(renderer);

    if (FLUXION_HANDLE_IS_VALID(renderer->bloomDownPipeline)) Fluxion_RHI_DestroyPipeline(renderer->bloomDownPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->bloomUpPipeline)) Fluxion_RHI_DestroyPipeline(renderer->bloomUpPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->bloomDownProgram)) Fluxion_ShaderProgram_Destroy(renderer->bloomDownProgram);
    if (FLUXION_HANDLE_IS_VALID(renderer->bloomUpProgram)) Fluxion_ShaderProgram_Destroy(renderer->bloomUpProgram);
    if (FLUXION_HANDLE_IS_VALID(renderer->bloomLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->bloomLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->bloomUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->bloomUniformBuffer);

    renderer->bloomDownPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->bloomUpPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->bloomDownProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->bloomUpProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->bloomLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->bloomUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (FLUXION_HANDLE_IS_VALID(renderer->postPipeline)) Fluxion_RHI_DestroyPipeline(renderer->postPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->postProgram)) Fluxion_ShaderProgram_Destroy(renderer->postProgram);
    if (FLUXION_HANDLE_IS_VALID(renderer->postLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->postLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->postSampler)) Fluxion_RHI_DestroySampler(renderer->postSampler);
    if (FLUXION_HANDLE_IS_VALID(renderer->postVertexBuffer)) Fluxion_RHI_DestroyBuffer(renderer->postVertexBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->postUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->postUniformBuffer);

    renderer->postPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postSampler = (FluxionRHISamplerHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postVertexBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postBoundSceneColor = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}
