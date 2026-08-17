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

// The registered "ForwardOpaquePass" render graph pass type (see
// Fluxion_Renderer_Create) -- userData is always a FluxionRenderer*.
//
// Resource-name convention: Setup declares a Write on
// "ForwardOpaquePass.ColorN" (N = 0..colorViewCount-1) for every color
// attachment of the active render view's render target, and
// "ForwardOpaquePass.Depth" if it has a depth attachment -- the caller
// must import the matching underlying texture(s) under these exact
// names (Fluxion_RenderGraph_ImportTexture) before calling
// Fluxion_RenderGraph_AddPassFromRegistry(graph, "ForwardOpaquePass",
// Fluxion_Renderer_GetForwardOpaquePassUserData(renderer)). A real
// system would instead have RenderTarget carry its own graph resource
// handles so this could be derived automatically; that's out of scope
// here, so this fixed naming convention stands in for it.

#include "RendererInternal.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>

#include <string.h>

static void Fluxion_ForwardOpaquePassInternal_WriteColorName(char* buffer, usize bufferSize, u32 index)
{
    // snprintf is C99/C11/C23-available and this codebase already
    // assumes a C23 toolchain (see project build notes) -- a tiny
    // hand-rolled formatter would be pure overhead here.
    (void)bufferSize;
    const char* digits = "0123456789";
    buffer[0] = 'F'; buffer[1] = 'o'; buffer[2] = 'r'; buffer[3] = 'w'; buffer[4] = 'a'; buffer[5] = 'r';
    buffer[6] = 'd'; buffer[7] = 'O'; buffer[8] = 'p'; buffer[9] = 'a'; buffer[10] = 'q'; buffer[11] = 'u';
    buffer[12] = 'e'; buffer[13] = 'P'; buffer[14] = 'a'; buffer[15] = 's'; buffer[16] = 's'; buffer[17] = '.';
    buffer[18] = 'C'; buffer[19] = 'o'; buffer[20] = 'l'; buffer[21] = 'o'; buffer[22] = 'r';
    buffer[23] = digits[index % 10];
    buffer[24] = '\0';
}

void FluxionForwardOpaquePass_Setup(FluxionRenderGraphBuilder* builder, void* userData)
{
    const FluxionRenderer* renderer = (const FluxionRenderer*)userData;
    if (renderer == NULL) return;

    FluxionRenderTargetHandle renderTarget = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRendererInternal_RenderView_Get(renderer->currentView, &renderTarget, NULL, NULL);

    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount = 0;
    FluxionRHITextureViewHandle depthView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!FluxionRendererInternal_RenderTarget_Get(renderTarget, colorViews, &colorViewCount, &depthView)) return;

    for (u32 i = 0; i < colorViewCount; ++i)
    {
        char name[32];
        Fluxion_ForwardOpaquePassInternal_WriteColorName(name, sizeof(name), i);
        Fluxion_RenderGraphBuilder_WriteColorTarget(builder, name);
    }
    if (FLUXION_HANDLE_IS_VALID(depthView))
    {
        Fluxion_RenderGraphBuilder_WriteDepthTarget(builder, "ForwardOpaquePass.Depth");
    }

    // Declared whether or not anything casts a shadow this frame, for the
    // same reason the shadow pass declares writing it: what a pass reads
    // is part of what it IS, and a declaration that came and went would
    // reorder the graph from one frame to the next. It is also the only
    // thing that puts the two passes in the right order and hands the
    // atlas over from being drawn into to being read.
    Fluxion_RenderGraphBuilder_ReadTexture(builder, FLUXION_RENDER_VIEW_SHADOW_ATLAS_RESOURCE);
}

void FluxionForwardOpaquePass_Execute(FluxionRHICommandListHandle commandList, void* userData)
{
    FluxionRenderer* renderer = (FluxionRenderer*)userData;

    // No early return on an empty frame any more. A scene with nothing in
    // it still has a sky behind it, and leaving without drawing would
    // also leave the target uncleared -- so an empty scene would show
    // whatever the last frame left.
    if (renderer == NULL) return;

    FluxionRenderTargetHandle renderTarget = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBindGroupHandle frameBindGroup = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRendererInternal_RenderView_Get(renderer->currentView, &renderTarget, NULL, &frameBindGroup);

    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount = 0;
    FluxionRHITextureViewHandle depthView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!FluxionRendererInternal_RenderTarget_Get(renderTarget, colorViews, &colorViewCount, &depthView) || colorViewCount == 0) return;

    FluxionRHIRenderingAttachment colorAttachments[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    for (u32 i = 0; i < colorViewCount; ++i)
    {
        colorAttachments[i].view = colorViews[i];
        colorAttachments[i].clear = (i == 0);
        colorAttachments[i].clearColor[0] = 0.0f;
        colorAttachments[i].clearColor[1] = 0.0f;
        colorAttachments[i].clearColor[2] = 0.0f;
        colorAttachments[i].clearColor[3] = 1.0f;
    }

    FluxionRHIRenderingAttachment depthAttachment;
    depthAttachment.view = depthView;
    depthAttachment.clear = true;
    depthAttachment.clearColor[0] = 1.0f;

    FluxionViewport viewport = { 0 };
    FluxionRendererInternal_RenderView_GetViewport(renderer->currentView, &viewport);

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = colorAttachments;
    renderingDesc.colorAttachmentCount = colorViewCount;
    renderingDesc.depthAttachment = FLUXION_HANDLE_IS_VALID(depthView) ? &depthAttachment : NULL;
    renderingDesc.width = (u32)viewport.width;
    renderingDesc.height = (u32)viewport.height;

    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);

    // ONE DRAW PER BATCH, not one per object.
    //
    // What each batch is, and which run of the object list it covers,
    // was worked out by Fluxion_Renderer_UploadScene before this pass
    // ran; the command that draws it is already sitting in a buffer the
    // GPU reads its own arguments from. What is left here is binding
    // what the batch needs and pointing at its command.
    const u32 batchCount = Fluxion_GPUScene_GetBatchCount(renderer->gpuScene);
    if (batchCount > 0 && !FLUXION_HANDLE_IS_VALID(Fluxion_GPUScene_GetObjectBuffer(renderer->gpuScene)))
    {
        // A batch to draw and nothing to draw it from. Said here rather
        // than left to a backend, which would report it as a descriptor
        // nobody filled in -- true, and no help at all.
        FLUXION_LOG_ERROR("Renderer", "the frame has %u batches and no object buffer behind them; nothing is drawn", batchCount);
        return;
    }

    const FluxionRHIBufferHandle indirectBuffer = Fluxion_GPUScene_GetIndirectBuffer(renderer->gpuScene);


    for (u32 i = 0; i < batchCount; ++i)
    {
        const FluxionGPUSceneBatch* batch = Fluxion_GPUScene_GetBatch(renderer->gpuScene, i);
        if (batch == NULL) continue;

        // A batch whose every object was culled: it still holds its
        // rows, and it draws none of them. Skipped here rather than
        // submitted with zero instances -- a draw call that draws
        // nothing is still a draw call, and the count below is a number
        // somebody reads.
        if (batch->visibleCount == 0) continue;

        FluxionRHIBufferHandle vertexBuffer, indexBuffer;
        u32 vertexCount, indexCount;
        bool use16BitIndices;
        FluxionRHIVertexLayout vertexLayout;
        if (!FluxionRendererInternal_MeshBuffer_Get(batch->mesh, &vertexBuffer, &indexBuffer, &vertexCount, &indexCount, &use16BitIndices, &vertexLayout)) continue;

        FluxionRHIPipelineHandle rhiPipeline = FluxionRendererInternal_RenderPipeline_Resolve(batch->pipeline, renderer->device, &vertexLayout);
        if (!FLUXION_HANDLE_IS_VALID(rhiPipeline)) continue;

        Fluxion_Material_FlushDirty(batch->material);
        FluxionRHIBindGroupHandle materialBindGroup = FluxionRendererInternal_Material_GetBindGroup(batch->material);

        // The batch's own bind group, built with the batch (GPUScene.c)
        // rather than here: a group made and destroyed around each draw
        // hands its descriptors to the next one while this command list
        // is still only recorded.
        const FluxionRHIBindGroupHandle objectBindGroup = Fluxion_GPUScene_GetBatchBindGroup(renderer->gpuScene, i);

        // FRAME must be (re)bound only after SetPipeline, not once before
        // this loop -- the Vulkan backend's SetBindGroup needs a pipeline
        // already bound to know which VkPipelineLayout/bind point to use
        // (vkCmdBindDescriptorSets takes a layout argument the portable
        // SetBindGroup call itself doesn't carry) and silently no-ops
        // otherwise, so binding FRAME before any SetPipeline call would
        // never actually reach the GPU on that backend.
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
            // A mesh with no indices is still ONE call for the whole
            // batch -- just a direct one, because the command the scene
            // wrote describes an indexed draw and there is nothing to
            // index. The instance count is the batch's, and the first
            // instance is zero, which is what keeps the shader's index
            // arithmetic the same either way.
            Fluxion_RHI_CommandList_Draw(commandList, vertexCount, batch->objectCount, 0, 0);
        }
        ++renderer->lastDrawCallCount;

    }

    // Last, not first. The sky sits exactly on the far plane and keeps
    // only what is equal or nearer, so drawing it after everything else
    // fills precisely the pixels nothing covered -- and every pixel that
    // WAS covered is rejected by the depth test before its shader runs.
    // Drawing it first would shade every one of them and then throw the
    // work away.
    //
    // The formats come from what the caller said it draws into. Nothing
    // reachable from here can answer that -- a texture view carries no
    // queryable format any more than it carries an extent -- so these are
    // the two the renderer was told, and they are the frame's attachment
    // formats rather than anything specific to the debug lines that first
    // needed them.
    FluxionRendererInternal_Skybox_Draw(renderer, commandList, frameBindGroup,
        renderer->attachmentColorFormat, renderer->attachmentDepthFormat, FLUXION_HANDLE_IS_VALID(depthView));

    Fluxion_RHI_CommandList_EndRendering(commandList);
}
