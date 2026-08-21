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
#include <Fluxion/RenderCore/Renderer/GPUScene.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>

#include <string.h>

#define FLUXION_PREPASS_LOG_CATEGORY "Renderer"

void FluxionRendererInternal_NormalRoughness_Release(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->prepassTargetView)) Fluxion_RHI_DestroyTextureView(renderer->prepassTargetView);
    if (FLUXION_HANDLE_IS_VALID(renderer->prepassSampleView)) Fluxion_RHI_DestroyTextureView(renderer->prepassSampleView);
    if (FLUXION_HANDLE_IS_VALID(renderer->prepassTexture)) Fluxion_RHI_DestroyTexture(renderer->prepassTexture);

    renderer->prepassTargetView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prepassSampleView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prepassTexture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prepassWidth = 0;
    renderer->prepassHeight = 0;
    renderer->prepassNeedsFirstTransition = true;
}

static bool FluxionNormalRoughness_EnsureTexture(FluxionRenderer* renderer, u32 width, u32 height)
{
    if (width == 0 || height == 0) return false;

    if (renderer->prepassWidth == width && renderer->prepassHeight == height &&
        FLUXION_HANDLE_IS_VALID(renderer->prepassTexture))
    {
        return true;
    }

    FluxionRendererInternal_NormalRoughness_Release(renderer);

    FluxionRHITextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

    // EIGHT BITS A CHANNEL, AND THAT IS ENOUGH BECAUSE OF HOW THE NORMAL
    // IS STORED. Two octahedral numbers spread their error evenly over
    // the sphere -- there is no pole where the precision collapses -- so
    // eight bits each is about a quarter of a degree, far below what the
    // occlusion and the reflections that read this can tell apart.
    //
    // Sixteen-bit floats would be four times the memory and the bandwidth
    // of a full-screen texture, every frame, to record a direction more
    // precisely than anything asks for.
    desc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "Fluxion.Renderer.NormalRoughness";

    renderer->prepassTexture = Fluxion_RHI_CreateTexture(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->prepassTexture))
    {
        FLUXION_LOG_ERROR(FLUXION_PREPASS_LOG_CATEGORY, "the frame's surfaces have nowhere to be recorded at %ux%u", width, height);
        return false;
    }

    FluxionRHITextureViewDesc viewDesc;
    memset(&viewDesc, 0, sizeof(viewDesc));
    viewDesc.texture = renderer->prepassTexture;
    viewDesc.format = desc.format;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;

    renderer->prepassTargetView = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
    renderer->prepassSampleView = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->prepassTargetView) || !FLUXION_HANDLE_IS_VALID(renderer->prepassSampleView))
    {
        FLUXION_LOG_ERROR(FLUXION_PREPASS_LOG_CATEGORY, "the recorded surfaces could not be looked at");
        return false;
    }

    renderer->prepassWidth = width;
    renderer->prepassHeight = height;
    renderer->prepassNeedsFirstTransition = true;
    return true;
}

FluxionRHITextureViewHandle FluxionRendererInternal_NormalRoughness_GetView(const FluxionRenderer* renderer)
{
    return renderer->prepassSampleView;
}

FluxionRHITextureHandle FluxionRendererInternal_NormalRoughness_GetTexture(const FluxionRenderer* renderer)
{
    return renderer->prepassTexture;
}

bool FluxionRendererInternal_NormalRoughness_Build(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList)
{
    if (!renderer->prepassEnabled) return false;
    if (renderer->prepassFailed) return false;

    FluxionViewport viewport = { 0 };
    if (!FluxionRendererInternal_RenderView_GetViewport(renderer->currentView, &viewport)) return false;

    const u32 width = (u32)viewport.width;
    const u32 height = (u32)viewport.height;

    if (!FluxionNormalRoughness_EnsureTexture(renderer, width, height))
    {
        FLUXION_LOG_WARN(FLUXION_PREPASS_LOG_CATEGORY,
                         "the frame's surfaces will not be recorded; what reads them finds nothing and leaves the picture alone");
        renderer->prepassFailed = true;
        return false;
    }

    // THE DEPTH THIS PASS WRITES IS THE DEPTH THE FORWARD PASS TESTS
    // AGAINST -- the same attachment, from the same view. That is not a
    // saving, it is the point: the two passes call the same
    // EvaluateSurface and reject the same pixels, so the surface recorded
    // here is the surface that ends up lit, and the forward pass finds
    // its depth already there.
    FluxionRenderTargetHandle renderTarget = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBindGroupHandle frameBindGroup = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRendererInternal_RenderView_Get(renderer->currentView, &renderTarget, NULL, &frameBindGroup);

    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount = 0;
    FluxionRHITextureViewHandle depthView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!FluxionRendererInternal_RenderTarget_Get(renderTarget, colorViews, &colorViewCount, &depthView)) return false;
    if (!FLUXION_HANDLE_IS_VALID(depthView)) return false;

    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIResourceState was = renderer->prepassNeedsFirstTransition ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED
                                                                             : FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    renderer->prepassNeedsFirstTransition = false;

    FluxionRHIBarrier toTarget = { renderer->prepassTexture, noBuffer, was, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &toTarget, 1);

    FluxionRHIRenderingAttachment attachment;
    attachment.view = renderer->prepassTargetView;

    // CLEARED, AND WHAT IT IS CLEARED TO MATTERS. Half in the first two
    // channels is the octahedral encoding of straight up out of the
    // screen; zero roughness is a mirror. Neither is read anywhere, because
    // the alpha says whether anything was recorded at all -- but a texture
    // left holding the last frame's surfaces where nothing drew this one
    // would give the effects that read it a scene that is not there.
    attachment.clear = true;
    attachment.clearColor[0] = 0.5f;
    attachment.clearColor[1] = 0.5f;
    attachment.clearColor[2] = 0.0f;
    attachment.clearColor[3] = 0.0f;

    FluxionRHIRenderingAttachment depthAttachment;
    depthAttachment.view = depthView;
    depthAttachment.clear = true;
    depthAttachment.clearColor[0] = 1.0f;
    depthAttachment.clearColor[1] = 0.0f;
    depthAttachment.clearColor[2] = 0.0f;
    depthAttachment.clearColor[3] = 0.0f;

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = &attachment;
    renderingDesc.colorAttachmentCount = 1;
    renderingDesc.depthAttachment = &depthAttachment;
    renderingDesc.width = width;
    renderingDesc.height = height;

    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
    Fluxion_RHI_CommandList_SetViewport(commandList, 0.0f, 0.0f, viewport.width, viewport.height, 0.0f, 1.0f);
    Fluxion_RHI_CommandList_SetScissor(commandList, 0, 0, width, height);

    const u32 batchCount = Fluxion_GPUScene_GetBatchCount(renderer->gpuScene);
    const FluxionRHIBufferHandle indirectBuffer = Fluxion_GPUScene_GetIndirectBuffer(renderer->gpuScene);
    const bool countsOnDevice = Fluxion_GPUScene_CountsOnDevice(renderer->gpuScene);

    for (u32 i = 0; i < batchCount; ++i)
    {
        const FluxionGPUSceneBatch* batch = Fluxion_GPUScene_GetBatch(renderer->gpuScene, i);
        if (batch == NULL) continue;
        if (batch->visibleCount == 0 && !countsOnDevice) continue;

        FluxionRHIBufferHandle vertexBuffer;
        FluxionRHIBufferHandle indexBuffer;
        u32 vertexCount = 0;
        u32 indexCount = 0;
        bool use16BitIndices = false;
        FluxionRHIVertexLayout vertexLayout;
        if (!FluxionRendererInternal_MeshBuffer_Get(batch->mesh, &vertexBuffer, &indexBuffer, &vertexCount, &indexCount, &use16BitIndices, &vertexLayout)) continue;

        // A BATCH WHOSE MATERIAL WAS NEVER GIVEN A PROGRAM FOR THIS PASS
        // IS SKIPPED, not drawn some other way. What it leaves behind is
        // the cleared value, whose alpha says "nothing recorded here" --
        // and the effects that read this leave those pixels alone. A
        // stand-in surface would be worse: occlusion and reflections
        // would both act on a direction nothing faces.
        FluxionRHIPipelineHandle rhiPipeline = FluxionRendererInternal_RenderPipeline_ResolvePrepass(
            batch->pipeline, renderer->device, &vertexLayout, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM);
        if (!FLUXION_HANDLE_IS_VALID(rhiPipeline)) continue;

        Fluxion_Material_FlushDirty(batch->material);
        FluxionRHIBindGroupHandle materialBindGroup = FluxionRendererInternal_Material_GetBindGroup(batch->material);
        const FluxionRHIBindGroupHandle objectBindGroup = Fluxion_GPUScene_GetBatchBindGroup(renderer->gpuScene, i);
        if (!FLUXION_HANDLE_IS_VALID(objectBindGroup)) continue;

        // The same order the forward pass uses, and for the same reason:
        // one backend needs a pipeline bound before it can be told which
        // groups go with it.
        Fluxion_RHI_CommandList_SetPipeline(commandList, rhiPipeline);
        if (FLUXION_HANDLE_IS_VALID(frameBindGroup)) Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_FRAME, frameBindGroup);
        Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, vertexBuffer, 0);
        if (FLUXION_HANDLE_IS_VALID(materialBindGroup)) Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_MATERIAL, materialBindGroup);
        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_OBJECT, objectBindGroup);

        if (FLUXION_HANDLE_IS_VALID(indexBuffer))
        {
            Fluxion_RHI_CommandList_SetIndexBuffer(commandList, indexBuffer, 0, use16BitIndices);
            Fluxion_RHI_CommandList_DrawIndexedIndirect(commandList, indirectBuffer,
                                                        (usize)i * sizeof(FluxionRHIDrawIndexedIndirectCommand), 1,
                                                        (u32)sizeof(FluxionRHIDrawIndexedIndirectCommand));
        }
        else
        {
            Fluxion_RHI_CommandList_Draw(commandList, vertexCount, batch->objectCount, 0, 0);
        }
    }

    Fluxion_RHI_CommandList_EndRendering(commandList);

    FluxionRHIBarrier toRead = { renderer->prepassTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                 FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);
    return true;
}
