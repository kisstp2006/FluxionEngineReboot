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

#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>

#include "RendererInternal.h"

#include <Fluxion/Core/Diagnostics/ProfileScope.hpp>

#include <Fluxion/Foundation/Assert.h>

#include <cstdio>
#include <cstring>

namespace
{

struct FluxionPipelineVariant
{
    bool used = false;
    u64 layoutHash = 0;
    FluxionRHIVertexLayout layout{};
    FluxionRHIPipelineHandle pipeline{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    // Owned by this variant (Fluxion_RHI_CreateBindGroupLayout is free to
    // dedupe/refcount identical-shape descs across callers -- see RHI.h --
    // but each Create still needs a matching Destroy for its own
    // reference, or the underlying VkDescriptorSetLayout/equivalent is
    // never actually released and vkDestroyDevice reports it as leaked).
    FluxionRHIBindGroupLayoutHandle frameLayout{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBindGroupLayoutHandle objectLayout{ FLUXION_HANDLE_INVALID_INDEX, 0 };
};

struct FluxionRenderPipelineRecord
{
    bool alive = false;
    u32 generation = 0;

    FluxionShaderProgramHandle program{};
    FluxionRenderPipelineCategory category = FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE;
    FluxionRHIFormat colorFormat = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    FluxionRHIFormat depthFormat = FLUXION_RHI_FORMAT_D32_FLOAT;

    FluxionPipelineVariant variants[FLUXION_RENDERER_MAX_PIPELINE_VARIANTS];
    u32 variantCount = 0;

    // --- THE SAME MATERIAL, DRAWN BEFORE ANYTHING IS LIT -----------------
    //
    // A second program built from the SAME material source, for the pass
    // that records which way each pixel faces and how rough it is. It is
    // a second program rather than another variant of the one above
    // because it has a different fragment entry point -- and a second
    // cache beside it because a variant is keyed on the vertex layout,
    // and both programs get asked for the same layouts.
    //
    // Optional. A pipeline nobody gave one to does not appear in that
    // pass at all, and what reads it finds nothing recorded there.
    FluxionShaderProgramHandle prepassProgram{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIFormat prepassColorFormat = FLUXION_RHI_FORMAT_UNKNOWN;
    FluxionPipelineVariant prepassVariants[FLUXION_RENDERER_MAX_PIPELINE_VARIANTS];
    u32 prepassVariantCount = 0;
};

FluxionRenderPipelineRecord s_pipelines[FLUXION_RENDERER_MAX_RENDER_PIPELINES];

FluxionRenderPipelineRecord* Resolve(FluxionRenderPipelineHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_RENDER_PIPELINES) return nullptr;
    FluxionRenderPipelineRecord* record = &s_pipelines[handle.index];
    if (!record->alive || record->generation != handle.generation) return nullptr;
    return record;
}

// A plain FNV-1a hash over the layout's own bytes -- only used to speed
// up the linear scan below, never trusted alone: a hash match is always
// confirmed with a full memcmp before treating two layouts as the same
// variant.
u64 HashVertexLayout(const FluxionRHIVertexLayout& layout)
{
    const u8* bytes = reinterpret_cast<const u8*>(&layout);
    u64 hash = 1469598103934665603ull;
    for (usize i = 0; i < sizeof(layout); ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

extern "C" FluxionRenderPipelineHandle Fluxion_RenderPipeline_Create(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program, FluxionRenderPipelineCategory category, FluxionRHIFormat colorFormat, FluxionRHIFormat depthFormat)
{
    FLUXION_UNUSED(device); // nothing RHI-side is created eagerly -- see FluxionRendererInternal_RenderPipeline_Resolve

    FluxionRenderPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    u32 index = FLUXION_RENDERER_MAX_RENDER_PIPELINES;
    for (u32 i = 0; i < FLUXION_RENDERER_MAX_RENDER_PIPELINES; ++i)
    {
        if (!s_pipelines[i].alive) { index = i; break; }
    }
    if (index == FLUXION_RENDERER_MAX_RENDER_PIPELINES) return invalid;

    FluxionRenderPipelineRecord record;
    record.program = program;
    record.category = category;
    record.colorFormat = colorFormat;
    record.depthFormat = depthFormat;
    record.alive = true;
    record.generation = s_pipelines[index].generation;
    s_pipelines[index] = record;

    FluxionRenderPipelineHandle handle = { index, s_pipelines[index].generation };
    return handle;
}

extern "C" void Fluxion_RenderPipeline_Destroy(FluxionRenderPipelineHandle pipeline)
{
    FluxionRenderPipelineRecord* record = Resolve(pipeline);
    if (record == nullptr)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion_RenderPipeline_Destroy called with an invalid or already-destroyed handle");
        return;
    }

    for (u32 i = 0; i < record->variantCount; ++i)
    {
        if (!record->variants[i].used) continue;
        if (FLUXION_HANDLE_IS_VALID(record->variants[i].pipeline)) Fluxion_RHI_DestroyPipeline(record->variants[i].pipeline);
        if (FLUXION_HANDLE_IS_VALID(record->variants[i].frameLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->variants[i].frameLayout);
        if (FLUXION_HANDLE_IS_VALID(record->variants[i].objectLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->variants[i].objectLayout);
    }

    // AND THE OTHER CACHE. Two arrays, two loops: a pipeline that was
    // also drawn before the lighting holds a second set of native objects
    // and a second pair of layouts per vertex layout, and a device counts
    // every one of them.
    for (u32 i = 0; i < record->prepassVariantCount; ++i)
    {
        if (!record->prepassVariants[i].used) continue;
        if (FLUXION_HANDLE_IS_VALID(record->prepassVariants[i].pipeline)) Fluxion_RHI_DestroyPipeline(record->prepassVariants[i].pipeline);
        if (FLUXION_HANDLE_IS_VALID(record->prepassVariants[i].frameLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->prepassVariants[i].frameLayout);
        if (FLUXION_HANDLE_IS_VALID(record->prepassVariants[i].objectLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->prepassVariants[i].objectLayout);
    }

    record->alive = false;
    ++record->generation;
}

extern "C" void FluxionRendererInternal_RenderPipeline_InvalidateVariantsUsingProgram(FluxionShaderProgramHandle program)
{
    for (u32 p = 0; p < FLUXION_RENDERER_MAX_RENDER_PIPELINES; ++p)
    {
        FluxionRenderPipelineRecord* record = &s_pipelines[p];
        if (!record->alive) continue;
        if (record->program.index != program.index || record->program.generation != program.generation) continue;

        for (u32 i = 0; i < record->variantCount; ++i)
        {
            if (!record->variants[i].used) continue;
            if (FLUXION_HANDLE_IS_VALID(record->variants[i].pipeline)) Fluxion_RHI_DestroyPipeline(record->variants[i].pipeline);
            if (FLUXION_HANDLE_IS_VALID(record->variants[i].frameLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->variants[i].frameLayout);
            if (FLUXION_HANDLE_IS_VALID(record->variants[i].objectLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->variants[i].objectLayout);
            record->variants[i] = FluxionPipelineVariant{};
        }

        // Back to none rather than to holes: Resolve appends, and a
        // partially reused array would keep the old count forever while
        // the same handful of vertex layouts are asked for again.
        record->variantCount = 0;
    }
}

// ONE BUILDER, TWO CALLERS. The pass that lights a surface and the pass
// that records it differ in three things -- which program, which colour
// format, and whether they blend -- and in nothing else. Written once, so
// that the two cannot drift apart in the ways nothing would report: the
// same vertex layout, the same bind group shapes, the same depth
// comparison.
static FluxionRHIPipelineHandle BuildOrFindVariant(FluxionRenderPipelineRecord* record, FluxionRHIDeviceHandle device,
                                                   const FluxionRHIVertexLayout* vertexLayout, FluxionShaderProgramHandle program,
                                                   FluxionRHIFormat colorFormat, bool blends, bool writesDepth,
                                                   FluxionPipelineVariant* variants, u32* variantCount)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    u64 hash = HashVertexLayout(*vertexLayout);
    for (u32 i = 0; i < *variantCount; ++i)
    {
        if (variants[i].layoutHash == hash && std::memcmp(&variants[i].layout, vertexLayout, sizeof(*vertexLayout)) == 0)
        {
            return variants[i].pipeline;
        }
    }

    if (*variantCount >= FLUXION_RENDERER_MAX_PIPELINE_VARIANTS)
    {
        FLUXION_ASSERT_MSG(false, "RenderPipeline: exceeded FLUXION_RENDERER_MAX_PIPELINE_VARIANTS distinct vertex layouts for one pipeline");
        return invalid;
    }

    FluxionRHIBindGroupLayoutDesc frameLayoutDesc = FluxionRendererInternal_MakeFrameLayoutDesc();
    FluxionRHIBindGroupLayoutDesc objectLayoutDesc = FluxionRendererInternal_MakeObjectLayoutDesc();
    FluxionRHIBindGroupLayoutHandle frameLayout = Fluxion_RHI_CreateBindGroupLayout(device, &frameLayoutDesc);
    FluxionRHIBindGroupLayoutHandle objectLayout = Fluxion_RHI_CreateBindGroupLayout(device, &objectLayoutDesc);
    FluxionRHIBindGroupLayoutHandle materialLayout = FluxionRendererInternal_ShaderProgram_GetMaterialBindGroupLayout(program);

    FluxionRHIGraphicsPipelineDesc desc{};
    desc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(program);
    desc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(program);
    desc.vertexLayout = *vertexLayout;
    desc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_BACK;
    desc.rasterState.frontFaceCounterClockwise = false;
    desc.rasterState.wireframe = false;
    desc.depthState.testEnable = true;
    desc.depthState.writeEnable = writesDepth;
    desc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;
    desc.blendState.mode = blends ? FLUXION_RHI_BLEND_MODE_ALPHA : FLUXION_RHI_BLEND_MODE_NONE;
    desc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.colorFormats[0] = colorFormat;
    desc.colorFormatCount = 1;
    desc.depthFormat = record->depthFormat;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 }; // unused by this pipeline -- explicit, not a stray zero-valued "valid-looking" handle
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = frameLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = materialLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = objectLayout;
    desc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_OBJECT + 1;
    // A driver's pipeline library is keyed on this name, so it has to be
    // two things at once: different for every pipeline that is genuinely
    // different, and identical to what the previous run called the same
    // pipeline. Everything here is derived from what the pipeline is --
    // never from a pool index, which is assignment order and would change
    // between runs, turning every lookup into a miss.
    //
    // A name that is merely constant is worse than useless: every
    // pipeline then collides on one key, so only the first one ever gets
    // stored and the rest are rebuilt from scratch every run.
    char name[192];
    std::snprintf(name, sizeof(name), "Fluxion.RenderPipeline|%s|%u|%u|%u|%016llx",
        FluxionRendererInternal_ShaderProgram_GetDebugName(program),
        (u32)record->category, (u32)colorFormat, (u32)record->depthFormat,
        (unsigned long long)hash);
    desc.debugName = name;

    FluxionRHIPipelineHandle rhiPipeline;
    {
        // Only the miss path is inside the zone: a cache hit returned
        // above, so every sample of this zone is a real native pipeline
        // build -- the expensive thing worth seeing on a timeline.
        FLUXION_PROFILE_SCOPE("RenderPipeline.BuildVariant");
        rhiPipeline = Fluxion_RHI_CreateGraphicsPipeline(device, &desc);
    }

    FluxionPipelineVariant& variant = variants[(*variantCount)++];
    variant.used = true;
    variant.layoutHash = hash;
    variant.layout = *vertexLayout;
    variant.pipeline = rhiPipeline;
    variant.frameLayout = frameLayout;
    variant.objectLayout = objectLayout;
    return rhiPipeline;
}

extern "C" FluxionRHIPipelineHandle FluxionRendererInternal_RenderPipeline_Resolve(FluxionRenderPipelineHandle pipeline, FluxionRHIDeviceHandle device, const FluxionRHIVertexLayout* vertexLayout)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRenderPipelineRecord* record = Resolve(pipeline);
    if (record == nullptr || vertexLayout == nullptr) return invalid;

    return BuildOrFindVariant(record, device, vertexLayout, record->program, record->colorFormat,
                              record->category == FLUXION_RENDER_PIPELINE_CATEGORY_TRANSPARENT,
                              record->category == FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
                              record->variants, &record->variantCount);
}

extern "C" FluxionRHIPipelineHandle FluxionRendererInternal_RenderPipeline_ResolvePrepass(FluxionRenderPipelineHandle pipeline, FluxionRHIDeviceHandle device, const FluxionRHIVertexLayout* vertexLayout, FluxionRHIFormat colorFormat)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRenderPipelineRecord* record = Resolve(pipeline);
    if (record == nullptr || vertexLayout == nullptr) return invalid;
    if (!FLUXION_HANDLE_IS_VALID(record->prepassProgram)) return invalid;

    // THE FORMAT COMES FROM THE PASS rather than from the pipeline,
    // because it belongs to the pass: every material records its surface
    // into the same one texture. Remembered so that a later ask with a
    // different format is refused rather than quietly answered with a
    // pipeline built for the first one.
    if (record->prepassVariantCount != 0 && record->prepassColorFormat != colorFormat) return invalid;
    record->prepassColorFormat = colorFormat;

    // NEVER BLENDED, ALWAYS WRITES DEPTH, whatever the material category
    // says. What this pass records is one surface per pixel, and a blend
    // of two surfaces gives a direction neither of them faces.
    return BuildOrFindVariant(record, device, vertexLayout, record->prepassProgram, colorFormat, false, true,
                              record->prepassVariants, &record->prepassVariantCount);
}

extern "C" void Fluxion_RenderPipeline_SetPrepassProgram(FluxionRenderPipelineHandle pipeline, FluxionShaderProgramHandle program)
{
    FluxionRenderPipelineRecord* record = Resolve(pipeline);
    if (record == nullptr) return;
    record->prepassProgram = program;
}

extern "C" bool Fluxion_RenderPipeline_HasPrepassProgram(FluxionRenderPipelineHandle pipeline)
{
    const FluxionRenderPipelineRecord* record = Resolve(pipeline);
    return record != nullptr && FLUXION_HANDLE_IS_VALID(record->prepassProgram);
}

// The pipeline the sky wants.
//
// Not a variant of the one above and not reachable through a
// FluxionRenderPipeline: a sky is not a material, has no category, and
// wants a depth state no surface does. Writing it as a category would
// mean every material could ask to be drawn as sky.
//
// The three differences from a surface, and each of them load-bearing:
// it never WRITES depth, so it leaves the buffer for whatever comes
// after; it keeps what is equal or nearer, so a triangle sitting exactly
// on the far plane fills only where nothing else drew; and it culls
// nothing, because a triangle covering the screen has no facing worth
// respecting.
extern "C" FluxionRHIPipelineHandle FluxionRendererInternal_ShaderProgram_CreateSkyboxPipeline(
    FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program, const FluxionRHIVertexLayout* vertexLayout,
    FluxionRHIFormat colorFormat, FluxionRHIFormat depthFormat, FluxionRHIBindGroupLayoutHandle* outFrameLayout)
{
    FluxionRHIBindGroupLayoutDesc frameLayoutDesc = FluxionRendererInternal_MakeFrameLayoutDesc();
    FluxionRHIBindGroupLayoutHandle frameLayout = Fluxion_RHI_CreateBindGroupLayout(device, &frameLayoutDesc);

    // Handed back rather than dropped here: the pipeline is built from it
    // and the caller has to give it back when the pipeline goes. Letting
    // it fall out of scope leaks one object per rebuild, which a device
    // reports at shutdown and nothing reports before that.
    *outFrameLayout = frameLayout;

    FluxionRHIGraphicsPipelineDesc desc{};
    desc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(program);
    desc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(program);
    desc.vertexLayout = *vertexLayout;
    desc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    desc.rasterState.frontFaceCounterClockwise = false;
    desc.rasterState.wireframe = false;
    desc.depthState.testEnable = true;
    desc.depthState.writeEnable = false;
    desc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;
    desc.blendState.mode = FLUXION_RHI_BLEND_MODE_NONE;
    desc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.colorFormats[0] = colorFormat;
    desc.colorFormatCount = 1;
    desc.depthFormat = depthFormat;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = frameLayout;
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    desc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    desc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_FRAME + 1;
    desc.debugName = "Fluxion.Renderer.Skybox.Pipeline";

    return Fluxion_RHI_CreateGraphicsPipeline(device, &desc);
}
