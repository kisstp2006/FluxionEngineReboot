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
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#include <string.h>

#define FLUXION_FXAA_LOG_CATEGORY "Renderer"

static const char* const kFXAAVertexSource = "#include \"Fluxion/Pass/FullscreenVertex.jsl\"\n";
static const char* const kFXAAFragmentSource = "#include \"Fluxion/Pass/FXAA.jsl\"\n";

// What the pass is told -- see FXAA.jsl.
typedef struct FluxionFXAAUniform
{
    FluxionVec4 step;
} FluxionFXAAUniform;

static FluxionRHIBindGroupLayoutDesc FluxionFXAA_MakeLayoutDesc(void)
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
    desc.debugName = "Fluxion.Renderer.FXAALayout";
    return desc;
}

static void FluxionFXAA_ReleaseTexture(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaTargetView)) Fluxion_RHI_DestroyTextureView(renderer->fxaaTargetView);
    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaSampleView)) Fluxion_RHI_DestroyTextureView(renderer->fxaaSampleView);
    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaTexture)) Fluxion_RHI_DestroyTexture(renderer->fxaaTexture);

    renderer->fxaaTargetView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->fxaaSampleView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->fxaaTexture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->fxaaWidth = 0;
    renderer->fxaaHeight = 0;
    renderer->fxaaNeedsFirstTransition = true;

    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->fxaaBindGroup);
    renderer->fxaaBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->fxaaBoundSource = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

void FluxionRendererInternal_FXAA_Release(FluxionRenderer* renderer)
{
    FluxionFXAA_ReleaseTexture(renderer);

    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaPipeline)) Fluxion_RHI_DestroyPipeline(renderer->fxaaPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaProgram)) Fluxion_ShaderProgram_Destroy(renderer->fxaaProgram);
    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->fxaaLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->fxaaUniformBuffer);

    renderer->fxaaPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->fxaaProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->fxaaLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->fxaaUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->fxaaBuiltForFormat = FLUXION_RHI_FORMAT_UNKNOWN;
}

// The picture this pass reads, kept between frames and remade when the
// window changes size -- or when the format the renderer writes changes,
// which is a thing a caller may say at any point before the first frame.
static bool FluxionFXAA_EnsureTexture(FluxionRenderer* renderer, u32 width, u32 height)
{
    if (width == 0 || height == 0) return false;

    if (renderer->fxaaWidth == width && renderer->fxaaHeight == height &&
        FLUXION_HANDLE_IS_VALID(renderer->fxaaTexture))
    {
        return true;
    }

    FluxionFXAA_ReleaseTexture(renderer);

    FluxionRHITextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

    // THE SAME FORMAT THE SCREEN IS, and that is what makes this pass
    // something a person can switch on and off while watching: the
    // resolve's pipeline was built to write this format, and it goes on
    // writing this format whether its destination is this texture or the
    // window.
    desc.format = renderer->postOutputFormat;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "Fluxion.Renderer.FXAA";

    renderer->fxaaTexture = Fluxion_RHI_CreateTexture(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->fxaaTexture))
    {
        FLUXION_LOG_ERROR(FLUXION_FXAA_LOG_CATEGORY, "the picture has nowhere to be smoothed at %ux%u", width, height);
        return false;
    }

    FluxionRHITextureViewDesc viewDesc;
    memset(&viewDesc, 0, sizeof(viewDesc));
    viewDesc.texture = renderer->fxaaTexture;
    viewDesc.format = desc.format;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

    renderer->fxaaTargetView = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
    renderer->fxaaSampleView = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->fxaaTargetView) || !FLUXION_HANDLE_IS_VALID(renderer->fxaaSampleView))
    {
        FLUXION_LOG_ERROR(FLUXION_FXAA_LOG_CATEGORY, "the smoothed picture could not be looked at");
        return false;
    }

    renderer->fxaaWidth = width;
    renderer->fxaaHeight = height;
    renderer->fxaaNeedsFirstTransition = true;
    return true;
}

static bool FluxionFXAA_EnsurePipeline(FluxionRenderer* renderer)
{
    // REBUILT WHEN THE FORMAT CHANGES, not merely built once. A caller
    // may say what the renderer writes into at any point before the first
    // frame, and a pipeline built for the format that was current when
    // this pass was first switched on would then be writing the wrong
    // one -- which no backend reports as a mistake.
    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaPipeline) && renderer->fxaaBuiltForFormat == renderer->postOutputFormat)
    {
        return true;
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaPipeline)) Fluxion_RHI_DestroyPipeline(renderer->fxaaPipeline);
    renderer->fxaaPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (!FLUXION_HANDLE_IS_VALID(renderer->fxaaProgram))
    {
        FluxionShaderProgramDesc programDesc;
        memset(&programDesc, 0, sizeof(programDesc));
        programDesc.debugName = "Fluxion.Renderer.FXAA";
        programDesc.vertexSource = kFXAAVertexSource;
        programDesc.fragmentSource = kFXAAFragmentSource;
        renderer->fxaaProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->fxaaProgram))
        {
            FLUXION_LOG_ERROR(FLUXION_FXAA_LOG_CATEGORY, "the smoothing shader could not be built; edges will stay as they are");
            return false;
        }
    }

    if (!FLUXION_HANDLE_IS_VALID(renderer->fxaaLayout))
    {
        const FluxionRHIBindGroupLayoutDesc layoutDesc = FluxionFXAA_MakeLayoutDesc();
        renderer->fxaaLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &layoutDesc);
    }

    if (!FLUXION_HANDLE_IS_VALID(renderer->fxaaUniformBuffer))
    {
        FluxionRHIBufferDesc uniformDesc;
        memset(&uniformDesc, 0, sizeof(uniformDesc));
        uniformDesc.size = FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
        uniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
        uniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
        uniformDesc.debugName = "Fluxion.Renderer.FXAAParams";
        renderer->fxaaUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &uniformDesc);
    }

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
    pipelineDesc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->fxaaProgram);
    pipelineDesc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->fxaaProgram);
    pipelineDesc.vertexLayout = vertexLayout;
    pipelineDesc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    pipelineDesc.depthState.testEnable = false;
    pipelineDesc.depthState.writeEnable = false;
    pipelineDesc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_ALWAYS;
    pipelineDesc.blendState.mode = FLUXION_RHI_BLEND_MODE_NONE;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.colorFormats[0] = renderer->postOutputFormat;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = FLUXION_RHI_FORMAT_UNKNOWN;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = renderer->fxaaLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = noLayout;
    pipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_GLOBAL + 1;
    pipelineDesc.debugName = "Fluxion.Renderer.FXAAPipeline";
    renderer->fxaaPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &pipelineDesc);

    if (!FLUXION_HANDLE_IS_VALID(renderer->fxaaPipeline) || !FLUXION_HANDLE_IS_VALID(renderer->fxaaUniformBuffer) ||
        !FLUXION_HANDLE_IS_VALID(renderer->fxaaLayout))
    {
        FLUXION_LOG_ERROR(FLUXION_FXAA_LOG_CATEGORY, "the smoothing could not be set up; edges will stay as they are");
        return false;
    }

    renderer->fxaaBuiltForFormat = renderer->postOutputFormat;
    return true;
}

static bool FluxionFXAA_EnsureBindGroup(FluxionRenderer* renderer)
{
    if (renderer->fxaaBoundSource.index == renderer->fxaaSampleView.index &&
        renderer->fxaaBoundSource.generation == renderer->fxaaSampleView.generation &&
        FLUXION_HANDLE_IS_VALID(renderer->fxaaBindGroup))
    {
        return true;
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->fxaaBindGroup)) Fluxion_RHI_DestroyBindGroup(renderer->fxaaBindGroup);
    renderer->fxaaBindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBindGroupEntry entries[3];
    memset(entries, 0, sizeof(entries));

    entries[0].binding = 0;
    entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    entries[0].buffer = renderer->fxaaUniformBuffer;
    entries[0].bufferSize = sizeof(FluxionFXAAUniform);

    entries[1].binding = 1;
    entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[1].textureView = renderer->fxaaSampleView;

    entries[2].binding = 2;
    entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[2].sampler = renderer->postSampler;

    FluxionRHIBindGroupDesc groupDesc;
    memset(&groupDesc, 0, sizeof(groupDesc));
    groupDesc.layout = renderer->fxaaLayout;
    groupDesc.entries = entries;
    groupDesc.entryCount = 3;

    renderer->fxaaBindGroup = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->fxaaBindGroup)) return false;

    renderer->fxaaBoundSource = renderer->fxaaSampleView;
    return true;
}

bool FluxionRendererInternal_FXAA_Begin(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList, u32 width, u32 height)
{
    if (!renderer->fxaaEnabled) return false;
    if (renderer->fxaaFailed) return false;

    if (!FluxionFXAA_EnsureTexture(renderer, width, height) || !FluxionFXAA_EnsurePipeline(renderer) ||
        !FluxionFXAA_EnsureBindGroup(renderer))
    {
        // Said once. Everything above logs its own reason; what this adds
        // is that the frame will keep drawing without it rather than
        // reporting the same failure sixty times a second.
        FLUXION_LOG_WARN(FLUXION_FXAA_LOG_CATEGORY, "edges will stay as they are; the resolve writes the screen directly");
        renderer->fxaaFailed = true;
        return false;
    }

    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // A texture nobody has drawn into is in no state at all; after that
    // it is whatever the pass below left it in.
    const FluxionRHIResourceState was = renderer->fxaaNeedsFirstTransition ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED
                                                                          : FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    renderer->fxaaNeedsFirstTransition = false;

    FluxionRHIBarrier toTarget = { renderer->fxaaTexture, noBuffer, was, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &toTarget, 1);
    return true;
}

FluxionRHITextureViewHandle FluxionRendererInternal_FXAA_GetTargetView(const FluxionRenderer* renderer)
{
    return renderer->fxaaTargetView;
}

void FluxionRendererInternal_FXAA_Resolve(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                          FluxionRHITextureViewHandle outputView, u32 width, u32 height)
{
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBarrier toRead = { renderer->fxaaTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                 FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);

    FluxionFXAAUniform uniform;
    memset(&uniform, 0, sizeof(uniform));
    uniform.step.x = width > 0 ? 1.0f / (f32)width : 0.0f;
    uniform.step.y = height > 0 ? 1.0f / (f32)height : 0.0f;

    // WHICH WAY THIS BACKEND'S ROWS RUN. EVERY FULLSCREEN PASS NEEDS
    // THIS, not only the one that writes the screen -- the same rule the
    // glow's chain follows, and for the same reason. On that backend each
    // of these passes turns its own output over as it writes it, so a
    // pass that did not read the other way round would leave the picture
    // upside down for whatever came next.
    //
    // Measured, with this pass alone left out of the rule: the triangle
    // sat on row 36.8 with the smoothing on and row 26.2 with it off, on
    // a frame sixty-four rows tall.
    uniform.step.z = Fluxion_RHI_GetDeviceBackendType(renderer->device) == FLUXION_RHI_BACKEND_OPENGL ? 1.0f : 0.0f;

    void* mapped = Fluxion_RHI_MapBuffer(renderer->fxaaUniformBuffer);
    if (mapped != NULL)
    {
        memcpy(mapped, &uniform, sizeof(uniform));
        Fluxion_RHI_UnmapBuffer(renderer->fxaaUniformBuffer);
    }

    FluxionRHIRenderingAttachment attachment;
    attachment.view = outputView;
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
    Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->fxaaPipeline);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, renderer->fxaaBindGroup);
    Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->postVertexBuffer, 0);
    Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
    Fluxion_RHI_CommandList_EndRendering(commandList);
}
