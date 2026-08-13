// C++ only because it sits directly downstream of ShaderProgram's
// accessor functions (declared extern "C", trivially callable from a
// plain-C file too) -- kept .cpp to match this file's place in the
// spec's file list, nothing here actually requires C++.

#include <Fluxion/RenderCore/Renderer/Material.h>

#include "RendererInternal.h"

#include <Fluxion/Foundation/Assert.h>

#include <cstring>

namespace
{

// Sized off Fluxion::ShaderCompiler::IRBuildOptions::maxUniformBufferBytesPerGroup's
// default (128 bytes) with headroom -- the real, authoritative size for
// a given material is FluxionMaterialRecord::uniformBufferSize, always
// <= this.
constexpr u32 kMaterialCPUBufferCapacity = 256;

struct FluxionMaterialRecord
{
    bool alive = false;
    u32 generation = 0;

    FluxionRHIDeviceHandle device{};
    FluxionShaderProgramHandle program{};

    FluxionMaterialParameterInfo params[FLUXION_RENDERER_MAX_MATERIAL_PARAMETERS]{};
    u32 paramCount = 0;
    u32 uniformBufferSize = 0;

    u8 cpuBuffer[kMaterialCPUBufferCapacity]{};

    // Texture-kind parameter state, parallel-indexed with `params` (only
    // entries whose params[i].kind == FLUXION_MATERIAL_PARAMETER_TEXTURE
    // are meaningful).
    FluxionRHITextureViewHandle textureViews[FLUXION_RENDERER_MAX_MATERIAL_PARAMETERS]{};
    FluxionRHISamplerHandle textureSamplers[FLUXION_RENDERER_MAX_MATERIAL_PARAMETERS]{};

    bool dirty = false;

    FluxionRHIBufferHandle uniformBuffer{ FLUXION_HANDLE_INVALID_INDEX, 0 }; // valid only when uniformBufferSize > 0
    FluxionRHIBindGroupLayoutHandle bindGroupLayout{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBindGroupHandle bindGroup{ FLUXION_HANDLE_INVALID_INDEX, 0 };
};

FluxionMaterialRecord s_materials[FLUXION_RENDERER_MAX_MATERIALS];

FluxionMaterialRecord* Resolve(FluxionMaterialHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_MATERIALS) return nullptr;
    FluxionMaterialRecord* record = &s_materials[handle.index];
    if (!record->alive || record->generation != handle.generation) return nullptr;
    return record;
}

i32 FindParam(const FluxionMaterialRecord* record, const char* name, FluxionMaterialParameterKind kind)
{
    for (u32 i = 0; i < record->paramCount; ++i)
    {
        if (record->params[i].kind == kind && std::strcmp(record->params[i].name, name) == 0) return (i32)i;
    }
    return -1;
}

} // namespace

extern "C" FluxionMaterialHandle Fluxion_Material_Create(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program)
{
    FluxionMaterialHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    u32 index = FLUXION_RENDERER_MAX_MATERIALS;
    for (u32 i = 0; i < FLUXION_RENDERER_MAX_MATERIALS; ++i)
    {
        if (!s_materials[i].alive) { index = i; break; }
    }
    if (index == FLUXION_RENDERER_MAX_MATERIALS) return invalid;

    FluxionMaterialRecord record;
    record.device = device;
    record.program = program;
    record.paramCount = FluxionRendererInternal_ShaderProgram_GetMaterialParameters(program, record.params, FLUXION_RENDERER_MAX_MATERIAL_PARAMETERS, &record.uniformBufferSize);
    FLUXION_ASSERT_MSG(record.uniformBufferSize <= kMaterialCPUBufferCapacity, "Material: shader program's Material uniform buffer exceeds the fixed CPU parameter buffer capacity");

    if (record.uniformBufferSize > 0)
    {
        FluxionRHIBufferDesc bufferDesc;
        bufferDesc.size = record.uniformBufferSize;
        bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
        bufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
        bufferDesc.debugName = "Fluxion.Material.UniformBuffer";
        record.uniformBuffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);
    }

    record.bindGroupLayout = FluxionRendererInternal_ShaderProgram_GetMaterialBindGroupLayout(program);
    record.dirty = true; // force FlushDirty to build the bind group at least once, even with zero parameters

    record.alive = true;
    record.generation = s_materials[index].generation;
    s_materials[index] = record;

    FluxionMaterialHandle handle = { index, s_materials[index].generation };
    return handle;
}

extern "C" void Fluxion_Material_Destroy(FluxionMaterialHandle material)
{
    FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion_Material_Destroy called with an invalid or already-destroyed handle");
        return;
    }

    if (FLUXION_HANDLE_IS_VALID(record->uniformBuffer)) Fluxion_RHI_DestroyBuffer(record->uniformBuffer);
    if (FLUXION_HANDLE_IS_VALID(record->bindGroup)) Fluxion_RHI_DestroyBindGroup(record->bindGroup);

    record->alive = false;
    ++record->generation;
}

extern "C" bool Fluxion_Material_SetFloat(FluxionMaterialHandle material, const char* name, f32 value)
{
    FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr) return false;

    i32 index = FindParam(record, name, FLUXION_MATERIAL_PARAMETER_FLOAT);
    if (index < 0) return false;

    std::memcpy(record->cpuBuffer + record->params[index].offset, &value, sizeof(value));
    record->dirty = true;
    return true;
}

extern "C" bool Fluxion_Material_SetVec3(FluxionMaterialHandle material, const char* name, FluxionVec3 value)
{
    FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr) return false;

    i32 index = FindParam(record, name, FLUXION_MATERIAL_PARAMETER_VEC3);
    if (index < 0) return false;

    std::memcpy(record->cpuBuffer + record->params[index].offset, &value, sizeof(value));
    record->dirty = true;
    return true;
}

extern "C" bool Fluxion_Material_SetVec4(FluxionMaterialHandle material, const char* name, FluxionVec4 value)
{
    FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr) return false;

    i32 index = FindParam(record, name, FLUXION_MATERIAL_PARAMETER_VEC4);
    if (index < 0) return false;

    std::memcpy(record->cpuBuffer + record->params[index].offset, &value, sizeof(value));
    record->dirty = true;
    return true;
}

extern "C" bool Fluxion_Material_SetTexture(FluxionMaterialHandle material, const char* name, FluxionRHITextureViewHandle view, FluxionRHISamplerHandle sampler)
{
    FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr) return false;

    i32 index = FindParam(record, name, FLUXION_MATERIAL_PARAMETER_TEXTURE);
    if (index < 0) return false;

    record->textureViews[index] = view;
    record->textureSamplers[index] = sampler;
    record->dirty = true;
    return true;
}

extern "C" void Fluxion_Material_FlushDirty(FluxionMaterialHandle material)
{
    FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr || !record->dirty) return;

    if (record->uniformBufferSize > 0 && FLUXION_HANDLE_IS_VALID(record->uniformBuffer))
    {
        void* mapped = Fluxion_RHI_MapBuffer(record->uniformBuffer);
        if (mapped != nullptr)
        {
            std::memcpy(mapped, record->cpuBuffer, record->uniformBufferSize);
            Fluxion_RHI_UnmapBuffer(record->uniformBuffer);
        }
    }

    FluxionRHIBindGroupEntry entries[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    u32 entryCount = 0;

    if (record->uniformBufferSize > 0)
    {
        FluxionRHIBindGroupEntry& entry = entries[entryCount++];
        entry.binding = 0;
        entry.type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entry.buffer = record->uniformBuffer;
        entry.bufferOffset = 0;
        entry.bufferSize = record->uniformBufferSize;
        entry.textureView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        entry.sampler = FluxionRHISamplerHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    for (u32 i = 0; i < record->paramCount && entryCount + 2 <= FLUXION_RHI_MAX_BIND_GROUP_ENTRIES; ++i)
    {
        if (record->params[i].kind != FLUXION_MATERIAL_PARAMETER_TEXTURE) continue;

        FluxionRHIBindGroupEntry& textureEntry = entries[entryCount++];
        textureEntry.binding = record->params[i].binding;
        textureEntry.type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        textureEntry.textureView = record->textureViews[i];
        textureEntry.buffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        textureEntry.bufferOffset = 0;
        textureEntry.bufferSize = 0;
        textureEntry.sampler = FluxionRHISamplerHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

        FluxionRHIBindGroupEntry& samplerEntry = entries[entryCount++];
        samplerEntry.binding = record->params[i].samplerBinding;
        samplerEntry.type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        samplerEntry.sampler = record->textureSamplers[i];
        samplerEntry.buffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        samplerEntry.bufferOffset = 0;
        samplerEntry.bufferSize = 0;
        samplerEntry.textureView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }

    if (FLUXION_HANDLE_IS_VALID(record->bindGroup)) Fluxion_RHI_DestroyBindGroup(record->bindGroup);

    FluxionRHIBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = record->bindGroupLayout;
    bindGroupDesc.entries = entries;
    bindGroupDesc.entryCount = entryCount;
    record->bindGroup = Fluxion_RHI_CreateBindGroup(record->device, &bindGroupDesc);

    record->dirty = false;
}

extern "C" FluxionRHIBindGroupHandle FluxionRendererInternal_Material_GetBindGroup(FluxionMaterialHandle material)
{
    FluxionMaterialRecord* record = Resolve(material);
    FluxionRHIBindGroupHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return record != nullptr ? record->bindGroup : invalid;
}
