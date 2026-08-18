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

    // The view's own numbers, not the engine's defaults: a view built
    // for a pipeline that asked for a different shadow quality has an
    // atlas of a different size, and drawing into it by the default
    // measurements would put every tile in the wrong place.
    u32 atlasSize = 0;
    u32 atlasTileSize = 0;
    Fluxion_RenderView_GetShadowAtlasSize(renderer->currentView, &atlasSize, &atlasTileSize);
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
    renderingDesc.width = atlasSize;
    renderingDesc.height = atlasSize;

    // The WHOLE atlas is cleared once, and each light then draws into
    // its own part of it. Beginning a render per tile would clear the
    // others every time.
    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);

    const f32 tileSize = (f32)atlasTileSize;

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

    const u32 batchCount = Fluxion_GPUScene_GetBatchCount(renderer->gpuScene);

    for (u32 i = 0; i < batchCount; ++i)
    {
        const FluxionGPUSceneBatch* batch = Fluxion_GPUScene_GetBatch(renderer->gpuScene, i);
        if (batch == nullptr) continue;

        // WHAT THE CAMERA CAN SEE IS NOT ASKED HERE, and that is the
        // whole reason this pass draws a batch's rows rather than its
        // visible list: an object behind the eye still throws a shadow
        // into the picture, and a shadow map filled from the camera's
        // answer would lose exactly those.

        FluxionRHIBufferHandle vertexBuffer, indexBuffer;
        u32 vertexCount, indexCount;
        bool use16BitIndices;
        FluxionRHIVertexLayout vertexLayout;
        if (!FluxionRendererInternal_MeshBuffer_Get(batch->mesh, &vertexBuffer, &indexBuffer, &vertexCount, &indexCount, &use16BitIndices, &vertexLayout)) continue;

        // WHAT IS CULLED IS A BATCH, not a caster.
        //
        // It used to be a caster: each object was tested against each
        // shadow's frustum, and one that no shadow could see cost
        // nothing. A batch is a run of objects drawn by one command
        // now, so the granularity the test can work at is the run --
        // the sphere that holds all of it, worked out where the objects
        // were gathered (GPUScene.c).
        //
        // Coarser, and said rather than quietly lost: a batch spread
        // across the world is one sphere spanning the world, and no
        // shadow will reject it. What brings the finer answer back is
        // the culling moving to the GPU, where it can be per instance
        // rather than per draw.
        bool wantedByAny = false;
        for (u32 shadowIndex = 0; shadowIndex < shadowCount && !wantedByAny; ++shadowIndex)
        {
            if (batch->boundsRadius <= 0.0f ||
                Fluxion_Frustum_TouchesSphere(&shadowFrustum[shadowIndex], batch->boundsCentre, batch->boundsRadius))
            {
                wantedByAny = true;
            }
        }
        if (!wantedByAny) continue;

        if (!EnsurePipeline(renderer, &vertexLayout)) break;

        // Zeroed whole, not filled field by field: an entry has a field
        // this one does not use (the element stride, below, on the other
        // one) and leaving it as whatever was on the stack is how a
        // storage-buffer view ends up describing 0xCCCCCCCC-byte
        // elements -- which is not a wrong picture but a removed device.
        // The batch's own bind group, in the form that reads every row
        // it holds -- built once with the batch, beside the one the
        // forward pass binds. See GPUScene.h.
        const FluxionRHIBindGroupHandle objectBindGroup = Fluxion_GPUScene_GetBatchAllRowsBindGroup(renderer->gpuScene, i);

        // Rebound only when EnsurePipeline above actually built another
        // one, which happens when a mesh of a different shape turns up.
        if (boundPipeline.index != renderer->shadowPipeline.index ||
            boundPipeline.generation != renderer->shadowPipeline.generation)
        {
            Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->shadowPipeline);
            boundPipeline = renderer->shadowPipeline;
        }

        Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, vertexBuffer, 0);
        if (FLUXION_HANDLE_IS_VALID(indexBuffer)) Fluxion_RHI_CommandList_SetIndexBuffer(commandList, indexBuffer, 0, use16BitIndices);
        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_OBJECT, objectBindGroup);

        // SHADOWS OUTSIDE, BATCHES INSIDE was the other way round when a
        // caster was a draw: a shadow is a viewport and a bind group,
        // both cheap, and a caster's bind group was not. Now the batch's
        // bind group is built once here and the shadows loop inside it,
        // which is the same trade in the same direction.
        for (u32 shadowIndex = 0; shadowIndex < shadowCount; ++shadowIndex)
        {
            if (batch->boundsRadius > 0.0f &&
                !Fluxion_Frustum_TouchesSphere(&shadowFrustum[shadowIndex], batch->boundsCentre, batch->boundsRadius))
            {
                continue;
            }

            FluxionShadowAtlasTile tile;
            if (!FluxionRendererInternal_RenderView_GetShadow(renderer->currentView, shadowIndex, nullptr, &tile)) continue;

            // The viewport is what puts a shadow in its own corner of the
            // atlas, and the scissor is what keeps it there: geometry
            // outside the viewport is still clipped to the render area
            // rather than to the tile, so without this a caster would
            // draw over its neighbour.
            const f32 tileX = (f32)tile.x * tileSize;
            const f32 tileY = (f32)tile.y * tileSize;
            Fluxion_RHI_CommandList_SetViewport(commandList, tileX, tileY, tileSize, tileSize, 0.0f, 1.0f);
            Fluxion_RHI_CommandList_SetScissor(commandList, (i32)tileX, (i32)tileY, atlasTileSize, atlasTileSize);

            Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, renderer->shadowGlobalBindGroups[shadowIndex]);

            // DIRECT RATHER THAN INDIRECT, unlike the forward pass, and
            // for a plain reason: the count this draw needs is the
            // batch's object count, which this side knows whoever did
            // the culling. The commands in the indirect buffer hold the
            // camera's answer, which is not this pass's question.
            if (FLUXION_HANDLE_IS_VALID(indexBuffer))
            {
                // The batch's own level of detail, because a batch IS a
                // level: a caster drawn at a different range than the
                // camera draws it would cast the shadow of a shape that
                // is not on the screen.
                FluxionMeshLevel level;
                if (!Fluxion_MeshBuffer_GetLevel(batch->mesh, batch->lodIndex, &level))
                {
                    level.firstIndex = 0;
                    level.indexCount = indexCount;
                }

                Fluxion_RHI_CommandList_DrawIndexed(commandList, level.indexCount, batch->objectCount, level.firstIndex, 0, 0);
            }
            else
            {
                Fluxion_RHI_CommandList_Draw(commandList, vertexCount, batch->objectCount, 0, 0);
            }
        }

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
