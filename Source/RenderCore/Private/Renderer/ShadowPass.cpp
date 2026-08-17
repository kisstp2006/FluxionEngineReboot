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

// Drawing the world from a light, into one tile of the shadow atlas.
//
// The same draw list the forward pass uses, through a pipeline that
// writes depth and nothing else, with the light's matrix in place of the
// camera's. What comes out is what a surface reads later to find out
// whether anything stood between it and that light.

#include "RendererInternal.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPass.h>

#include <cstring>

namespace
{

const char* const kShadowVertexSource = "#include \"Fluxion/Pass/ShadowDepth.jsl\"\n";
const char* const kShadowFragmentSource = "#include \"Fluxion/Pass/ShadowDepthWrite.jsl\"\n";

// The light's matrix, alone in the Global group -- see ShadowDepth.jsl
// for why it does not travel with the frame.
FluxionRHIBindGroupLayoutDesc MakeShadowGlobalLayoutDesc()
{
    FluxionRHIBindGroupLayoutDesc desc = { };
    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX;
    desc.entryCount = 1;
    desc.debugName = "Fluxion.Renderer.ShadowGlobalLayout";
    return desc;
}

bool SameVertexLayout(const FluxionRHIVertexLayout& a, const FluxionRHIVertexLayout& b)
{
    if (a.attributeCount != b.attributeCount || a.stride != b.stride) return false;
    for (u32 i = 0; i < a.attributeCount; ++i)
    {
        if (a.attributes[i].location != b.attributes[i].location) return false;
        if (a.attributes[i].format != b.attributes[i].format) return false;
        if (a.attributes[i].offset != b.attributes[i].offset) return false;
    }
    return true;
}

bool EnsureProgram(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->shadowProgram)) return true;
    if (renderer->shadowFailed) return false;

    FluxionShaderProgramDesc programDesc;
    std::memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "Fluxion.Renderer.ShadowDepth";
    programDesc.vertexSource = kShadowVertexSource;
    programDesc.fragmentSource = kShadowFragmentSource;

    renderer->shadowProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->shadowProgram))
    {
        // Said once. A shader that would not build will not build again.
        FLUXION_LOG_ERROR("Renderer", "The shadow depth shader could not be built; nothing will cast a shadow.");
        renderer->shadowFailed = true;
        return false;
    }

    const FluxionRHIBindGroupLayoutDesc layoutDesc = MakeShadowGlobalLayoutDesc();
    renderer->shadowGlobalLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &layoutDesc);

    // One slice per shadow, each starting where a constant buffer view is
    // allowed to start -- the same stride the per-draw object buffer uses
    // and for the same reason. A shadow's matrix cannot be rewritten
    // between draws on one command list: every draw would read whatever
    // the last one put there.
    FluxionRHIBufferDesc matrixDesc;
    matrixDesc.size = (usize)FLUXION_RENDER_VIEW_MAX_SHADOWS * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    matrixDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    matrixDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    matrixDesc.debugName = "Fluxion.Renderer.ShadowLightMatrix";
    renderer->shadowMatrixBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &matrixDesc);

    if (!FLUXION_HANDLE_IS_VALID(renderer->shadowGlobalLayout) || !FLUXION_HANDLE_IS_VALID(renderer->shadowMatrixBuffer))
    {
        renderer->shadowFailed = true;
        return false;
    }

    const FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHISamplerHandle noSampler = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // One group per slice, made once. Which slice a draw reads is settled
    // by which group is bound, so nothing has to be rewritten mid-list.
    for (u32 i = 0; i < FLUXION_RENDER_VIEW_MAX_SHADOWS; ++i)
    {
        FluxionRHIBindGroupEntry entry;
        std::memset(&entry, 0, sizeof(entry));
        entry.binding = 0;
        entry.type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entry.buffer = renderer->shadowMatrixBuffer;
        entry.bufferOffset = (usize)i * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
        entry.bufferSize = sizeof(FluxionMat4);
        entry.textureView = noView;
        entry.sampler = noSampler;

        FluxionRHIBindGroupDesc groupDesc;
        groupDesc.layout = renderer->shadowGlobalLayout;
        groupDesc.entries = &entry;
        groupDesc.entryCount = 1;
        renderer->shadowGlobalBindGroups[i] = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);

        if (!FLUXION_HANDLE_IS_VALID(renderer->shadowGlobalBindGroups[i]))
        {
            renderer->shadowFailed = true;
            return false;
        }
    }

    return true;
}

// A pipeline is built against one vertex layout, so a mesh shaped
// differently needs another. One is kept, and a second shape replaces it
// -- a scene that alternated between two would rebuild every frame, and
// a real cache keyed by layout is what RenderPipeline already does when
// that becomes the problem.
bool EnsurePipeline(FluxionRenderer* renderer, const FluxionRHIVertexLayout* vertexLayout)
{
    if (renderer->shadowPipelineBuilt && SameVertexLayout(renderer->shadowVertexLayout, *vertexLayout)) return true;
    if (renderer->shadowFailed) return false;
    if (!EnsureProgram(renderer)) return false;

    if (FLUXION_HANDLE_IS_VALID(renderer->shadowPipeline)) Fluxion_RHI_DestroyPipeline(renderer->shadowPipeline);

    FluxionRHIGraphicsPipelineDesc desc{};
    desc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->shadowProgram);
    desc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->shadowProgram);
    desc.vertexLayout = *vertexLayout;

    // Front faces culled, not back. A shadow map holds the far side of
    // each caster, which pushes the surface being tested away from the
    // depth it is compared against -- the cheapest half of keeping a lit
    // surface from shadowing itself.
    desc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_FRONT;
    desc.rasterState.frontFaceCounterClockwise = false;
    desc.rasterState.wireframe = false;

    desc.depthState.testEnable = true;
    desc.depthState.writeEnable = true;
    desc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;
    desc.blendState.blendEnable = false;
    desc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // No colour at all: this pass produces depth, and the fragment
    // stage's return lands nowhere.
    desc.colorFormatCount = 0;
    desc.depthFormat = FLUXION_RHI_FORMAT_D32_FLOAT;

    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = renderer->shadowGlobalLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = renderer->objectBindGroupLayout;
    desc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_OBJECT + 1;
    desc.debugName = "Fluxion.Renderer.ShadowPipeline";

    renderer->shadowPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->shadowPipeline))
    {
        FLUXION_LOG_ERROR("Renderer", "The shadow pipeline could not be built; nothing will cast a shadow.");
        renderer->shadowFailed = true;
        return false;
    }

    renderer->shadowVertexLayout = *vertexLayout;
    renderer->shadowPipelineBuilt = true;
    return true;
}

} // namespace

extern "C" void FluxionShadowPass_Setup(FluxionRenderGraphBuilder* builder, void* userData)
{
    const FluxionRenderer* renderer = (const FluxionRenderer*)userData;
    if (renderer == nullptr) return;

    // Declared whether or not there is a shadow this frame. What a pass
    // writes is part of what it IS, and a declaration that came and went
    // would reorder the graph from one frame to the next.
    Fluxion_RenderGraphBuilder_WriteDepthTarget(builder, "ShadowPass.Atlas");
}

extern "C" void FluxionShadowPass_Execute(FluxionRHICommandListHandle commandList, void* userData)
{
    FluxionRenderer* renderer = (FluxionRenderer*)userData;
    if (renderer == nullptr) return;

    const u32 shadowCount = FluxionRendererInternal_RenderView_GetShadowCount(renderer->currentView);
    if (shadowCount == 0) return;

    const FluxionRHITextureViewHandle atlasView = FluxionRendererInternal_RenderView_GetShadowAtlasView(renderer->currentView);
    if (!FLUXION_HANDLE_IS_VALID(atlasView)) return;
    if (!EnsureProgram(renderer)) return;

    // Transposed on the way out, at the one boundary where that happens:
    // the shading languages read a matrix the other way round, and every
    // other upload in this engine turns it here rather than at its
    // caller.
    if (u8* base = (u8*)Fluxion_RHI_MapBuffer(renderer->shadowMatrixBuffer))
    {
        for (u32 i = 0; i < shadowCount; ++i)
        {
            FluxionMat4 lightViewProjection;
            if (!FluxionRendererInternal_RenderView_GetShadow(renderer->currentView, i, &lightViewProjection, nullptr)) continue;

            FluxionMat4* slice = (FluxionMat4*)(base + (usize)i * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE);
            *slice = Fluxion_Mat4_Transposed(lightViewProjection);
        }
        Fluxion_RHI_UnmapBuffer(renderer->shadowMatrixBuffer);
    }

    FluxionRHIRenderingAttachment depthAttachment;
    depthAttachment.view = atlasView;
    depthAttachment.clear = true;
    depthAttachment.clearColor[0] = 1.0f; // nothing in front of anything, until something is drawn

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = nullptr;
    renderingDesc.colorAttachmentCount = 0;
    renderingDesc.depthAttachment = &depthAttachment;
    renderingDesc.width = FLUXION_RENDERER_SHADOW_ATLAS_SIZE;
    renderingDesc.height = FLUXION_RENDERER_SHADOW_ATLAS_SIZE;

    // The WHOLE atlas is cleared once, and each light then draws into
    // its own part of it. Beginning a render per tile would clear the
    // others every time.
    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);

    const f32 tileSize = (f32)FLUXION_RENDERER_SHADOW_TILE_SIZE;

    // CASTERS OUTSIDE, SHADOWS INSIDE, which is the opposite of the way
    // this reads. A shadow is a viewport and a bind group -- both cheap,
    // both already made. A caster is a bind group BUILT HERE, and built
    // once per shadow it appeared in when the loops were the other way
    // round: eleven shadows in the sample, so eleven of the same object
    // every frame. This way each caster costs one.
    FluxionRHIPipelineHandle boundPipeline = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // Worked out once each, before anything is drawn: what a shadow can
    // see does not change between casters.
    FluxionFrustumPlanes shadowFrustum[FLUXION_RENDER_VIEW_MAX_SHADOWS];
    for (u32 shadowIndex = 0; shadowIndex < shadowCount; ++shadowIndex)
    {
        FluxionMat4 lightViewProjection;
        if (!FluxionRendererInternal_RenderView_GetShadow(renderer->currentView, shadowIndex, &lightViewProjection, nullptr)) continue;
        shadowFrustum[shadowIndex] = Fluxion_Mat4_FrustumPlanes(lightViewProjection);
    }

    for (u32 i = 0; i < renderer->packetCount; ++i)
    {
        const FluxionDrawPacket* packet = &renderer->packets[i];

        FluxionRHIBufferHandle vertexBuffer, indexBuffer;
        u32 vertexCount, indexCount;
        bool use16BitIndices;
        FluxionRHIVertexLayout vertexLayout;
        if (!FluxionRendererInternal_MeshBuffer_Get(packet->mesh, &vertexBuffer, &indexBuffer, &vertexCount, &indexCount, &use16BitIndices, &vertexLayout)) continue;

        // Where this caster is and how far it reaches, as a sphere.
        //
        // A SPHERE RATHER THAN THE BOX, because a box has to be turned
        // with the object and a sphere does not -- eight corners
        // transformed per caster per shadow, against one centre and one
        // number. It claims more room than the box does, which for
        // throwing work away is the safe direction: what survives may be
        // outside, what is dropped certainly is.
        const FluxionMat4& world = renderer->packetTransforms[i];
        FluxionAABB bounds;
        bool haveBounds = FluxionRendererInternal_MeshBuffer_GetBounds(packet->mesh, &bounds);

        FluxionVec3 worldCentre = { 0.0f, 0.0f, 0.0f };
        f32 worldRadius = 0.0f;
        if (haveBounds)
        {
            const FluxionVec3 centre = { (bounds.min.x + bounds.max.x) * 0.5f,
                                         (bounds.min.y + bounds.max.y) * 0.5f,
                                         (bounds.min.z + bounds.max.z) * 0.5f };
            const FluxionVec3 extent = { (bounds.max.x - bounds.min.x) * 0.5f,
                                         (bounds.max.y - bounds.min.y) * 0.5f,
                                         (bounds.max.z - bounds.min.z) * 0.5f };

            worldCentre.x = world.m[0][0] * centre.x + world.m[0][1] * centre.y + world.m[0][2] * centre.z + world.m[0][3];
            worldCentre.y = world.m[1][0] * centre.x + world.m[1][1] * centre.y + world.m[1][2] * centre.z + world.m[1][3];
            worldCentre.z = world.m[2][0] * centre.x + world.m[2][1] * centre.y + world.m[2][2] * centre.z + world.m[2][3];

            // The largest the transform stretches anything, taken from
            // its own rows -- a scale of two in one axis grows the
            // sphere by two whichever way the object was turned.
            f32 longestAxis = 0.0f;
            for (u32 row = 0; row < 3; ++row)
            {
                const f32 length = sqrtf(world.m[row][0] * world.m[row][0] +
                                         world.m[row][1] * world.m[row][1] +
                                         world.m[row][2] * world.m[row][2]);
                if (length > longestAxis) longestAxis = length;
            }

            worldRadius = sqrtf(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z) * longestAxis;
        }

        // Nothing to draw anywhere is worth finding out before a
        // pipeline is built for it.
        bool wantedByAny = false;
        for (u32 shadowIndex = 0; shadowIndex < shadowCount && !wantedByAny; ++shadowIndex)
        {
            if (!haveBounds || Fluxion_Frustum_TouchesSphere(&shadowFrustum[shadowIndex], worldCentre, worldRadius)) wantedByAny = true;
        }
        if (!wantedByAny) continue;

        if (!EnsurePipeline(renderer, &vertexLayout)) break;

        FluxionRHIBindGroupEntry objectEntry;
        objectEntry.binding = 0;
        objectEntry.type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        objectEntry.buffer = renderer->objectBuffer;
        objectEntry.bufferOffset = packet->objectDataOffset;
        objectEntry.bufferSize = sizeof(FluxionMat4);
        objectEntry.textureView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        objectEntry.sampler = FluxionRHISamplerHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

        FluxionRHIBindGroupDesc objectBindGroupDesc;
        objectBindGroupDesc.layout = renderer->objectBindGroupLayout;
        objectBindGroupDesc.entries = &objectEntry;
        objectBindGroupDesc.entryCount = 1;
        FluxionRHIBindGroupHandle objectBindGroup = Fluxion_RHI_CreateBindGroup(renderer->device, &objectBindGroupDesc);

        // Rebound only when EnsurePipeline above actually built another
        // one, which happens when a mesh of a different shape turns up.
        if (boundPipeline.index != renderer->shadowPipeline.index ||
            boundPipeline.generation != renderer->shadowPipeline.generation)
        {
            Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->shadowPipeline);
            boundPipeline = renderer->shadowPipeline;
        }

        Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, vertexBuffer, 0);
        if (FLUXION_HANDLE_IS_VALID(indexBuffer))
        {
            Fluxion_RHI_CommandList_SetIndexBuffer(commandList, indexBuffer, 0, use16BitIndices);
        }

        // Bound after the pipeline, always: one backend's SetBindGroup
        // needs a pipeline already bound to know which layout it belongs
        // to, and silently does nothing without one.
        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_OBJECT, objectBindGroup);

        for (u32 shadowIndex = 0; shadowIndex < shadowCount; ++shadowIndex)
        {
            FluxionShadowAtlasTile tile;
            if (!FluxionRendererInternal_RenderView_GetShadow(renderer->currentView, shadowIndex, nullptr, &tile)) continue;

            // The near cascades cover a few metres each. Without this
            // they get the whole world, one draw at a time.
            if (haveBounds && !Fluxion_Frustum_TouchesSphere(&shadowFrustum[shadowIndex], worldCentre, worldRadius)) continue;

            // The viewport is what puts a shadow in its own corner of the
            // atlas, and the scissor is what keeps it there: geometry
            // outside the viewport is still clipped to the render area
            // rather than to the tile, so without this a caster would
            // draw over its neighbour.
            const f32 tileX = (f32)tile.x * tileSize;
            const f32 tileY = (f32)tile.y * tileSize;
            Fluxion_RHI_CommandList_SetViewport(commandList, tileX, tileY, tileSize, tileSize, 0.0f, 1.0f);
            Fluxion_RHI_CommandList_SetScissor(commandList, (i32)tileX, (i32)tileY, FLUXION_RENDERER_SHADOW_TILE_SIZE, FLUXION_RENDERER_SHADOW_TILE_SIZE);

            Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, renderer->shadowGlobalBindGroups[shadowIndex]);

            if (FLUXION_HANDLE_IS_VALID(indexBuffer))
            {
                Fluxion_RHI_CommandList_DrawIndexed(commandList, indexCount, 1, 0, 0, 0);
            }
            else
            {
                Fluxion_RHI_CommandList_Draw(commandList, vertexCount, 1, 0, 0);
            }
        }

        Fluxion_RHI_DestroyBindGroup(objectBindGroup);
    }

    Fluxion_RHI_CommandList_EndRendering(commandList);
}

extern "C" void FluxionRendererInternal_Shadow_Destroy(FluxionRenderer* renderer)
{
    for (u32 i = 0; i < FLUXION_RENDER_VIEW_MAX_SHADOWS; ++i)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->shadowGlobalBindGroups[i])) Fluxion_RHI_DestroyBindGroup(renderer->shadowGlobalBindGroups[i]);
        renderer->shadowGlobalBindGroups[i] = FluxionRHIBindGroupHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    if (FLUXION_HANDLE_IS_VALID(renderer->shadowMatrixBuffer)) Fluxion_RHI_DestroyBuffer(renderer->shadowMatrixBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->shadowPipeline)) Fluxion_RHI_DestroyPipeline(renderer->shadowPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->shadowGlobalLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->shadowGlobalLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->shadowProgram)) Fluxion_ShaderProgram_Destroy(renderer->shadowProgram);

    renderer->shadowMatrixBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->shadowPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->shadowGlobalLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->shadowProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->shadowPipelineBuilt = false;
}
