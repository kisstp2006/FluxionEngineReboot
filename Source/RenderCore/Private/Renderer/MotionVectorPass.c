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

// WHERE EVERY PIXEL WAS A FRAME AGO, drawn.
//
// The same batches the forward pass draws, through a pipeline that writes
// two numbers per pixel and nothing else. It runs AFTER the forward pass
// and tests against the depth that pass left behind, so what ends up in
// the target is the motion of the surface that was actually seen -- not
// of whatever happened to be drawn last.
//
// A PASS OF ITS OWN rather than a second attachment on the forward pass,
// and the reason is worth stating: an extra attachment is part of a
// pipeline's identity, so every material's pipeline would have to be built
// for two colour formats and every material shader would have to write the
// second one. This way no material is touched at all, and the cost is the
// geometry going through once more -- the same three or four indirect
// draws the frame already issues.

#include "RendererInternal.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPass.h>

#include <string.h>

#define FLUXION_MOTION_PASS_LOG_CATEGORY "Renderer"

static const char* const kMotionVertexSource = "#include \"Fluxion/Pass/MotionVector.jsl\"\n";
static const char* const kMotionFragmentSource = "#include \"Fluxion/Pass/MotionVectorWrite.jsl\"\n";

static bool FluxionMotionVectorPass_SameVertexLayout(const FluxionRHIVertexLayout* a, const FluxionRHIVertexLayout* b)
{
    if (a->attributeCount != b->attributeCount || a->stride != b->stride) return false;

    for (u32 i = 0; i < a->attributeCount; ++i)
    {
        if (a->attributes[i].location != b->attributes[i].location) return false;
        if (a->attributes[i].format != b->attributes[i].format) return false;
        if (a->attributes[i].offset != b->attributes[i].offset) return false;
    }
    return true;
}

static bool FluxionMotionVectorPass_EnsureProgram(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->motionProgram)) return true;
    if (renderer->motionFailed) return false;

    FluxionShaderProgramDesc programDesc;
    memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "Fluxion.Renderer.MotionVector";
    programDesc.vertexSource = kMotionVertexSource;
    programDesc.fragmentSource = kMotionFragmentSource;

    renderer->motionProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->motionProgram))
    {
        // Said once. A shader that would not build will not build again.
        FLUXION_LOG_ERROR(FLUXION_MOTION_PASS_LOG_CATEGORY,
                          "The motion vector shader could not be built; nothing temporal will work this run.");
        renderer->motionFailed = true;
        return false;
    }
    return true;
}

static bool FluxionMotionVectorPass_EnsurePipeline(FluxionRenderer* renderer, const FluxionRHIVertexLayout* vertexLayout)
{
    if (renderer->motionPipelineBuilt && FluxionMotionVectorPass_SameVertexLayout(&renderer->motionVertexLayout, vertexLayout)) return true;
    if (renderer->motionFailed) return false;
    if (!FluxionMotionVectorPass_EnsureProgram(renderer)) return false;

    if (FLUXION_HANDLE_IS_VALID(renderer->motionPipeline)) Fluxion_RHI_DestroyPipeline(renderer->motionPipeline);

    FluxionRHIGraphicsPipelineDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->motionProgram);
    desc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->motionProgram);
    desc.vertexLayout = *vertexLayout;

    desc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_BACK;
    desc.rasterState.frontFaceCounterClockwise = false;
    desc.rasterState.wireframe = false;

    // TESTED AGAINST THE FRAME'S DEPTH, WRITING NONE OF IT. The forward
    // pass has already decided which surface is in front at every pixel,
    // and this pass wants that surface to be the one that answers here --
    // so anything BEHIND what is already there is rejected, and the
    // surface at exactly that depth (the one that was drawn) passes.
    //
    // Less-or-equal rather than equal: equal is the tighter statement, and
    // it also means a pass drawn into a depth buffer nobody has written
    // yet produces nothing at all -- which is a frame with no motion in it
    // wherever the order changes, and a test that cannot tell that from a
    // motion of zero.
    desc.depthState.testEnable = true;
    desc.depthState.writeEnable = false;
    desc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;
    desc.blendState.mode = FLUXION_RHI_BLEND_MODE_NONE;
    desc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    desc.colorFormats[0] = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    desc.colorFormatCount = 1;
    desc.depthFormat = FLUXION_RHI_FORMAT_D32_FLOAT;

    // The frame's layout, described rather than borrowed: identical
    // descriptions come back as the same layout object, so this is the
    // one every other pipeline in the frame was built against.
    if (!FLUXION_HANDLE_IS_VALID(renderer->motionFrameLayout))
    {
        const FluxionRHIBindGroupLayoutDesc frameLayoutDesc = FluxionRendererInternal_MakeFrameLayoutDesc();
        renderer->motionFrameLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &frameLayoutDesc);
    }
    const FluxionRHIBindGroupLayoutHandle frameLayout = renderer->motionFrameLayout;

    const FluxionRHIBindGroupLayoutHandle noLayout = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = noLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = frameLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = noLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = renderer->objectBindGroupLayout;
    desc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_OBJECT + 1;
    desc.debugName = "Fluxion.Renderer.MotionVectorPipeline";

    renderer->motionPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->motionPipeline))
    {
        FLUXION_LOG_ERROR(FLUXION_MOTION_PASS_LOG_CATEGORY,
                          "The motion vector pipeline could not be built; nothing temporal will work this run.");
        renderer->motionFailed = true;
        return false;
    }

    renderer->motionVertexLayout = *vertexLayout;
    renderer->motionPipelineBuilt = true;
    return true;
}

void FluxionMotionVectorPass_Setup(FluxionRenderGraphBuilder* builder, void* userData)
{
    FLUXION_UNUSED(userData);

    // What it writes and what it reads, declared whether or not this frame
    // has anything to draw -- what a pass writes is part of what it IS, and
    // a declaration that came and went would reorder the graph from one
    // frame to the next.
    //
    // The depth is declared as a DEPTH TARGET even though this pass only
    // tests against it and writes none of it. Two reasons, and the second
    // is the one that matters: it is what puts this pass after whoever
    // filled the depth buffer, and it is what the depth is bound AS -- an
    // attachment. Declaring it as something read by a shader would ask
    // for a layout a depth texture need not even support, and the sample's
    // does not (measured: a barrier the driver refuses, naming a usage
    // flag nobody asked for).
    Fluxion_RenderGraphBuilder_WriteColorTarget(builder, "MotionVectorPass.Motion");
    Fluxion_RenderGraphBuilder_WriteDepthTarget(builder, "ForwardOpaquePass.Depth");
}

void FluxionMotionVectorPass_Execute(FluxionRHICommandListHandle commandList, void* userData)
{
    FluxionRenderer* renderer = (FluxionRenderer*)userData;
    if (renderer == NULL) return;

    const FluxionRenderHistory* history = &renderer->history;
    if (!FLUXION_HANDLE_IS_VALID(history->motionView)) return;

    // The depth this frame drew, borrowed from whoever owns it: the motion
    // of a surface nobody can see is not motion the frame has.
    FluxionRenderTargetHandle renderTarget = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBindGroupHandle frameBindGroup = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRendererInternal_RenderView_Get(renderer->currentView, &renderTarget, NULL, &frameBindGroup);

    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount = 0;
    FluxionRHITextureViewHandle depthView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!FluxionRendererInternal_RenderTarget_Get(renderTarget, colorViews, &colorViewCount, &depthView)) return;
    if (!FLUXION_HANDLE_IS_VALID(depthView)) return;

    FluxionRHIRenderingAttachment motionAttachment;
    motionAttachment.view = history->motionView;

    // CLEARED EVERY FRAME, AND TO ZERO: a pixel nothing was drawn into has
    // no motion, and a vector left over from a frame ago would send
    // whoever reads it somewhere that has nothing to do with this frame.
    motionAttachment.clear = true;
    motionAttachment.clearColor[0] = 0.0f;
    motionAttachment.clearColor[1] = 0.0f;
    motionAttachment.clearColor[2] = 0.0f;
    motionAttachment.clearColor[3] = 0.0f;

    FluxionRHIRenderingAttachment depthAttachment;
    depthAttachment.view = depthView;

    // NOT cleared: this pass reads the depth the frame already has, and
    // clearing it would throw away the answer it came for.
    depthAttachment.clear = false;
    depthAttachment.clearColor[0] = 1.0f;

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = &motionAttachment;
    renderingDesc.colorAttachmentCount = 1;
    renderingDesc.depthAttachment = &depthAttachment;
    renderingDesc.width = history->width;
    renderingDesc.height = history->height;

    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);

    FluxionViewport viewport;
    memset(&viewport, 0, sizeof(viewport));
    FluxionRendererInternal_RenderView_GetViewport(renderer->currentView, &viewport);
    Fluxion_RHI_CommandList_SetViewport(commandList, viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth,
                                        viewport.maxDepth);
    Fluxion_RHI_CommandList_SetScissor(commandList, 0, 0, history->width, history->height);

    const u32 batchCount = Fluxion_GPUScene_GetBatchCount(renderer->gpuScene);
    const FluxionRHIBufferHandle indirectBuffer = Fluxion_GPUScene_GetIndirectBuffer(renderer->gpuScene);
    const bool countsOnDevice = Fluxion_GPUScene_CountsOnDevice(renderer->gpuScene);

    FluxionRHIPipelineHandle boundPipeline = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    for (u32 i = 0; i < batchCount; ++i)
    {
        const FluxionGPUSceneBatch* batch = Fluxion_GPUScene_GetBatch(renderer->gpuScene, i);
        if (batch == NULL) continue;

        // The same rule as the forward pass: on the device's path this
        // side's count is not the answer.
        if (batch->visibleCount == 0 && !countsOnDevice) continue;

        FluxionRHIBufferHandle vertexBuffer, indexBuffer;
        u32 vertexCount, indexCount;
        bool use16BitIndices;
        FluxionRHIVertexLayout vertexLayout;
        if (!FluxionRendererInternal_MeshBuffer_Get(batch->mesh, &vertexBuffer, &indexBuffer, &vertexCount, &indexCount, &use16BitIndices,
                                                    &vertexLayout))
        {
            continue;
        }

        if (!FluxionMotionVectorPass_EnsurePipeline(renderer, &vertexLayout)) break;

        const FluxionRHIBindGroupHandle objectBindGroup = Fluxion_GPUScene_GetBatchBindGroup(renderer->gpuScene, i);
        if (!FLUXION_HANDLE_IS_VALID(objectBindGroup)) continue;

        if (boundPipeline.index != renderer->motionPipeline.index || boundPipeline.generation != renderer->motionPipeline.generation)
        {
            Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->motionPipeline);
            boundPipeline = renderer->motionPipeline;
        }

        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_FRAME, frameBindGroup);
        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_OBJECT, objectBindGroup);
        Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, vertexBuffer, 0);

        if (FLUXION_HANDLE_IS_VALID(indexBuffer))
        {
            Fluxion_RHI_CommandList_SetIndexBuffer(commandList, indexBuffer, 0, use16BitIndices);
            Fluxion_RHI_CommandList_DrawIndexedIndirect(commandList, indirectBuffer,
                                                        (usize)i * sizeof(FluxionRHIDrawIndexedIndirectCommand), 1,
                                                        (u32)sizeof(FluxionRHIDrawIndexedIndirectCommand));
        }
        else
        {
            Fluxion_RHI_CommandList_Draw(commandList, vertexCount, batch->visibleCount, 0, 0);
        }
    }

    Fluxion_RHI_CommandList_EndRendering(commandList);
}

void FluxionRendererInternal_MotionVector_Destroy(FluxionRenderer* renderer)
{
    if (renderer == NULL) return;

    if (FLUXION_HANDLE_IS_VALID(renderer->motionPipeline)) Fluxion_RHI_DestroyPipeline(renderer->motionPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->motionFrameLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->motionFrameLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->motionProgram)) Fluxion_ShaderProgram_Destroy(renderer->motionProgram);

    renderer->motionPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->motionFrameLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->motionProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->motionPipelineBuilt = false;
}
