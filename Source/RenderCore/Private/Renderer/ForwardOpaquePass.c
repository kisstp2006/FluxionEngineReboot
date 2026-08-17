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

// Packets are sorted (index-sort, not moved in place) by
// (pipeline.index, material.index) so consecutive draws minimize
// pipeline/material bind group rebinding -- a real system would also
// weigh depth/state changes and translucency ordering; this is the
// "one afternoon" version.
static void Fluxion_ForwardOpaquePassInternal_SortPacketIndices(const FluxionRenderer* renderer, u32* order)
{
    for (u32 i = 0; i < renderer->packetCount; ++i) order[i] = i;

    for (u32 i = 1; i < renderer->packetCount; ++i)
    {
        u32 key = order[i];
        const FluxionDrawPacket* keyPacket = &renderer->packets[key];
        i32 j = (i32)i - 1;
        while (j >= 0)
        {
            const FluxionDrawPacket* candidate = &renderer->packets[order[j]];
            bool candidateFirst = (candidate->pipeline.index < keyPacket->pipeline.index) ||
                (candidate->pipeline.index == keyPacket->pipeline.index && candidate->material.index <= keyPacket->material.index);
            if (candidateFirst) break;
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
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

    u32 order[FLUXION_RENDERER_MAX_DRAW_PACKETS_PER_FRAME];
    Fluxion_ForwardOpaquePassInternal_SortPacketIndices(renderer, order);

    for (u32 i = 0; i < renderer->packetCount; ++i)
    {
        const FluxionDrawPacket* packet = &renderer->packets[order[i]];

        FluxionRHIBufferHandle vertexBuffer, indexBuffer;
        u32 vertexCount, indexCount;
        bool use16BitIndices;
        FluxionRHIVertexLayout vertexLayout;
        if (!FluxionRendererInternal_MeshBuffer_Get(packet->mesh, &vertexBuffer, &indexBuffer, &vertexCount, &indexCount, &use16BitIndices, &vertexLayout)) continue;

        FluxionRHIPipelineHandle rhiPipeline = FluxionRendererInternal_RenderPipeline_Resolve(packet->pipeline, renderer->device, &vertexLayout);
        if (!FLUXION_HANDLE_IS_VALID(rhiPipeline)) continue;

        Fluxion_Material_FlushDirty(packet->material);
        FluxionRHIBindGroupHandle materialBindGroup = FluxionRendererInternal_Material_GetBindGroup(packet->material);

        // The RHI has no per-draw dynamic bind-group offset (SetBindGroup
        // takes no offset argument) -- so a fresh, tiny OBJECT bind group
        // pointing at this one draw's slice of the shared object buffer
        // is built and torn down per draw instead. Wasteful at real
        // draw-call counts, acceptable for this pass's current scope.
        FluxionRHIBindGroupEntry objectEntry;
        objectEntry.binding = 0;
        objectEntry.type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        objectEntry.buffer = renderer->objectBuffer;
        objectEntry.bufferOffset = packet->objectDataOffset;
        objectEntry.bufferSize = sizeof(FluxionMat4);
        objectEntry.textureView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        objectEntry.sampler = (FluxionRHISamplerHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

        FluxionRHIBindGroupDesc objectBindGroupDesc;
        objectBindGroupDesc.layout = renderer->objectBindGroupLayout;
        objectBindGroupDesc.entries = &objectEntry;
        objectBindGroupDesc.entryCount = 1;
        FluxionRHIBindGroupHandle objectBindGroup = Fluxion_RHI_CreateBindGroup(renderer->device, &objectBindGroupDesc);

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
            Fluxion_RHI_CommandList_DrawIndexed(commandList, indexCount, 1, 0, 0, 0);
        }
        else
        {
            Fluxion_RHI_CommandList_Draw(commandList, vertexCount, 1, 0, 0);
        }
        ++renderer->lastDrawCallCount;

        Fluxion_RHI_DestroyBindGroup(objectBindGroup);
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
