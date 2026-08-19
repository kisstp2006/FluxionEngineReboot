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

// Three vertices in clip space, big enough that the triangle covers the
// screen and the corners fall outside it.
static const f32 kPostFullscreenTriangle[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };

// What the resolve is told about the frame -- see PostProcess.jsl for
// what each component means.
typedef struct FluxionPostUniform
{
    FluxionVec4 params;
} FluxionPostUniform;

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

    desc.entryCount = 3;
    desc.debugName = "Fluxion.Renderer.PostProcessLayout";
    return desc;
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

    FluxionRHIBufferDesc vertexDesc;
    memset(&vertexDesc, 0, sizeof(vertexDesc));
    vertexDesc.size = sizeof(kPostFullscreenTriangle);
    vertexDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER;
    vertexDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    vertexDesc.debugName = "Fluxion.Renderer.PostProcessTriangle";
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
    pipelineDesc.blendState.blendEnable = false;
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

// One bind group, kept, remade only when the texture it reads changes --
// which is when the window is resized. A group per frame is what
// exhausted a descriptor heap the last time this engine made one.
static bool FluxionPostProcessPass_EnsureBindGroup(FluxionRenderer* renderer)
{
    if (renderer->postBoundSceneColor.index == renderer->sceneColorSampleView.index &&
        renderer->postBoundSceneColor.generation == renderer->sceneColorSampleView.generation &&
        FLUXION_HANDLE_IS_VALID(renderer->postBindGroup))
    {
        return true;
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->postBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->postBindGroup);
    renderer->postBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBindGroupEntry entries[3];
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

    FluxionRHIBindGroupDesc groupDesc;
    groupDesc.layout = renderer->postLayout;
    groupDesc.entries = entries;
    groupDesc.entryCount = 3;

    renderer->postBindGroup = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->postBindGroup)) return false;

    renderer->postBoundSceneColor = renderer->sceneColorSampleView;
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
    if (!FluxionPostProcessPass_EnsureBindGroup(renderer)) return;

    FluxionRenderTargetHandle renderTarget = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRendererInternal_RenderView_Get(renderer->currentView, &renderTarget, NULL, NULL);

    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount = 0;
    FluxionRHITextureViewHandle depthView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!FluxionRendererInternal_RenderTarget_Get(renderTarget, colorViews, &colorViewCount, &depthView) || colorViewCount == 0) return;

    FluxionViewport viewport = { 0 };
    FluxionRendererInternal_RenderView_GetViewport(renderer->currentView, &viewport);

    // What the frame decided about the camera, handed to the one place
    // that acts on it.
    FluxionPostUniform uniform;
    memset(&uniform, 0, sizeof(uniform));
    FluxionRendererInternal_RenderView_GetToneMapping(renderer->currentView, &uniform.params);

    // Which way this backend's rows run -- see PostProcess.jsl. Asked of
    // the device rather than guessed, and only this pass needs it.
    uniform.params.w = Fluxion_RHI_GetDeviceBackendType(renderer->device) == FLUXION_RHI_BACKEND_OPENGL ? 1.0f : 0.0f;

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
    attachment.view = colorViews[0];
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
}

void FluxionRendererInternal_PostProcess_Shutdown(FluxionRenderer* renderer)
{
    FluxionRendererInternal_PostProcess_ReleaseSceneColor(renderer);

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
