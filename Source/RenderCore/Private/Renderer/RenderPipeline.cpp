#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>

#include "RendererInternal.h"

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

extern "C" FluxionRHIPipelineHandle FluxionRendererInternal_RenderPipeline_Resolve(FluxionRenderPipelineHandle pipeline, FluxionRHIDeviceHandle device, const FluxionRHIVertexLayout* vertexLayout)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRenderPipelineRecord* record = Resolve(pipeline);
    if (record == nullptr || vertexLayout == nullptr) return invalid;

    u64 hash = HashVertexLayout(*vertexLayout);
    for (u32 i = 0; i < record->variantCount; ++i)
    {
        if (record->variants[i].layoutHash == hash && std::memcmp(&record->variants[i].layout, vertexLayout, sizeof(*vertexLayout)) == 0)
        {
            return record->variants[i].pipeline;
        }
    }

    if (record->variantCount >= FLUXION_RENDERER_MAX_PIPELINE_VARIANTS)
    {
        FLUXION_ASSERT_MSG(false, "RenderPipeline: exceeded FLUXION_RENDERER_MAX_PIPELINE_VARIANTS distinct vertex layouts for one pipeline");
        return invalid;
    }

    FluxionRHIBindGroupLayoutDesc frameLayoutDesc = FluxionRendererInternal_MakeFrameLayoutDesc();
    FluxionRHIBindGroupLayoutDesc objectLayoutDesc = FluxionRendererInternal_MakeObjectLayoutDesc();
    FluxionRHIBindGroupLayoutHandle frameLayout = Fluxion_RHI_CreateBindGroupLayout(device, &frameLayoutDesc);
    FluxionRHIBindGroupLayoutHandle objectLayout = Fluxion_RHI_CreateBindGroupLayout(device, &objectLayoutDesc);
    FluxionRHIBindGroupLayoutHandle materialLayout = FluxionRendererInternal_ShaderProgram_GetMaterialBindGroupLayout(record->program);

    FluxionRHIGraphicsPipelineDesc desc{};
    desc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(record->program);
    desc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(record->program);
    desc.vertexLayout = *vertexLayout;
    desc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_BACK;
    desc.rasterState.frontFaceCounterClockwise = false;
    desc.rasterState.wireframe = false;
    desc.depthState.testEnable = true;
    desc.depthState.writeEnable = (record->category == FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE);
    desc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;
    desc.blendState.blendEnable = (record->category == FLUXION_RENDER_PIPELINE_CATEGORY_TRANSPARENT);
    desc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.colorFormats[0] = record->colorFormat;
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
        FluxionRendererInternal_ShaderProgram_GetDebugName(record->program),
        (u32)record->category, (u32)record->colorFormat, (u32)record->depthFormat,
        (unsigned long long)hash);
    desc.debugName = name;

    FluxionRHIPipelineHandle rhiPipeline = Fluxion_RHI_CreateGraphicsPipeline(device, &desc);

    FluxionPipelineVariant& variant = record->variants[record->variantCount++];
    variant.used = true;
    variant.layoutHash = hash;
    variant.layout = *vertexLayout;
    variant.pipeline = rhiPipeline;
    variant.frameLayout = frameLayout;
    variant.objectLayout = objectLayout;
    return rhiPipeline;
}
