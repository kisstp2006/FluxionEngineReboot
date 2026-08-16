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

// C++ only because it sits directly downstream of ShaderProgram's
// accessor functions (declared extern "C", trivially callable from a
// plain-C file too) -- kept .cpp to match this file's place in the
// spec's file list, nothing here actually requires C++.

#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MaterialParameters.h>

#include "RendererInternal.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>

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

    // Not a shader parameter and not derivable from one: it decides which
    // pass a draw belongs to and what blend state its pipeline is built
    // with, both settled before any shader runs.
    FluxionMaterialAlphaMode alphaMode = FLUXION_MATERIAL_ALPHA_OPAQUE;

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

// What each parameter the engine understands is called, and what kind it
// has to be. One table, so a name and its kind cannot drift apart -- and
// the same strings Fluxion/Material.jsl declares, which is the whole of
// the agreement between the two files.
struct CanonicalParameter
{
    const char* name;
    FluxionMaterialParameterKind kind;
};

const CanonicalParameter kCanonicalParameters[FLUXION_MATERIAL_PARAM_COUNT] = {
    { "baseColorFactor", FLUXION_MATERIAL_PARAMETER_VEC4 },
    { "metallicFactor", FLUXION_MATERIAL_PARAMETER_FLOAT },
    { "roughnessFactor", FLUXION_MATERIAL_PARAMETER_FLOAT },
    { "reflectance", FLUXION_MATERIAL_PARAMETER_FLOAT },
    { "emissiveFactor", FLUXION_MATERIAL_PARAMETER_VEC3 },
    { "normalScale", FLUXION_MATERIAL_PARAMETER_FLOAT },
    { "occlusionStrength", FLUXION_MATERIAL_PARAMETER_FLOAT },
    { "alphaCutoff", FLUXION_MATERIAL_PARAMETER_FLOAT },
};

const char* const kCanonicalTextures[FLUXION_MATERIAL_TEXTURE_COUNT] = {
    "baseColorMap",
    "metallicRoughnessMap",
    "normalMap",
    "occlusionMap",
    "emissiveMap",
};

// A parameter of the right kind, or -1.
i32 FindCanonical(const FluxionMaterialRecord* record, FluxionMaterialParameter parameter)
{
    if ((u32)parameter >= (u32)FLUXION_MATERIAL_PARAM_COUNT) return -1;
    return FindParam(record, kCanonicalParameters[parameter].name, kCanonicalParameters[parameter].kind);
}

// Whether a shader used one of the engine's own parameter names to mean
// something else.
//
// Refused rather than tolerated. A material declaring `metallicFactor` as
// a colour is not a material with an unusual parameter -- it is one whose
// metallic value can never be set, and every attempt would answer false
// with nothing to say why. Better to say it once, here, naming the
// parameter and what it should have been.
bool CanonicalKindsAgree(const FluxionMaterialRecord* record)
{
    bool ok = true;

    for (u32 i = 0; i < record->paramCount; ++i)
    {
        const FluxionMaterialParameterInfo& declared = record->params[i];

        for (u32 c = 0; c < (u32)FLUXION_MATERIAL_PARAM_COUNT; ++c)
        {
            if (std::strcmp(declared.name, kCanonicalParameters[c].name) != 0) continue;
            if (declared.kind == kCanonicalParameters[c].kind) break;

            FLUXION_LOG_ERROR("Material", "'%s' is one of the engine's own parameters and has to be declared with its own kind", declared.name);
            ok = false;
            break;
        }

        // A texture name used for something that is not a texture is the
        // same mistake, and worth the same answer.
        if (declared.kind == FLUXION_MATERIAL_PARAMETER_TEXTURE) continue;

        for (u32 t = 0; t < (u32)FLUXION_MATERIAL_TEXTURE_COUNT; ++t)
        {
            if (std::strcmp(declared.name, kCanonicalTextures[t]) != 0) continue;

            FLUXION_LOG_ERROR("Material", "'%s' is one of the engine's own texture slots and has to be declared as a texture", declared.name);
            ok = false;
            break;
        }
    }

    return ok;
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

    if (!CanonicalKindsAgree(&record))
    {
        if (FLUXION_HANDLE_IS_VALID(record.uniformBuffer)) Fluxion_RHI_DestroyBuffer(record.uniformBuffer);
        return invalid;
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

// --- The parameters the engine itself understands ------------------------

extern "C" const char* Fluxion_Material_GetParameterName(FluxionMaterialParameter parameter)
{
    if ((u32)parameter >= (u32)FLUXION_MATERIAL_PARAM_COUNT) return nullptr;
    return kCanonicalParameters[parameter].name;
}

extern "C" FluxionMaterialParameterType Fluxion_Material_GetParameterType(FluxionMaterialParameter parameter)
{
    if ((u32)parameter >= (u32)FLUXION_MATERIAL_PARAM_COUNT) return FLUXION_MATERIAL_PARAM_TYPE_FLOAT;

    switch (kCanonicalParameters[parameter].kind)
    {
        case FLUXION_MATERIAL_PARAMETER_VEC3: return FLUXION_MATERIAL_PARAM_TYPE_VEC3;
        case FLUXION_MATERIAL_PARAMETER_VEC4: return FLUXION_MATERIAL_PARAM_TYPE_VEC4;
        default: return FLUXION_MATERIAL_PARAM_TYPE_FLOAT;
    }
}

extern "C" const char* Fluxion_Material_GetTextureSlotName(FluxionMaterialTextureSlot slot)
{
    if ((u32)slot >= (u32)FLUXION_MATERIAL_TEXTURE_COUNT) return nullptr;
    return kCanonicalTextures[slot];
}

extern "C" bool Fluxion_Material_HasParameter(FluxionMaterialHandle material, FluxionMaterialParameter parameter)
{
    const FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr) return false;
    return FindCanonical(record, parameter) >= 0;
}

extern "C" bool Fluxion_Material_HasTextureSlot(FluxionMaterialHandle material, FluxionMaterialTextureSlot slot)
{
    const FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr || (u32)slot >= (u32)FLUXION_MATERIAL_TEXTURE_COUNT) return false;
    return FindParam(record, kCanonicalTextures[slot], FLUXION_MATERIAL_PARAMETER_TEXTURE) >= 0;
}

namespace
{

// Every canonical setter comes down to this: find the parameter, and copy
// exactly as many bytes as its kind has. The kind cannot be wrong here --
// a material that declared one of these names as something else was
// refused when it was created.
bool WriteCanonical(FluxionMaterialHandle material, FluxionMaterialParameter parameter, const void* value, usize size)
{
    FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr) return false;

    const i32 index = FindCanonical(record, parameter);
    if (index < 0) return false;

    FLUXION_ASSERT_MSG(record->params[index].size >= size, "Material: a parameter's declared size is smaller than its own kind");
    if (record->params[index].size < size) return false;

    std::memcpy(record->cpuBuffer + record->params[index].offset, value, size);
    record->dirty = true;
    return true;
}

} // namespace

extern "C" bool Fluxion_Material_SetBaseColor(FluxionMaterialHandle material, FluxionVec4 baseColor)
{
    return WriteCanonical(material, FLUXION_MATERIAL_PARAM_BASE_COLOR, &baseColor, sizeof(baseColor));
}

extern "C" bool Fluxion_Material_SetMetallic(FluxionMaterialHandle material, f32 metallic)
{
    return WriteCanonical(material, FLUXION_MATERIAL_PARAM_METALLIC, &metallic, sizeof(metallic));
}

extern "C" bool Fluxion_Material_SetRoughness(FluxionMaterialHandle material, f32 perceptualRoughness)
{
    return WriteCanonical(material, FLUXION_MATERIAL_PARAM_ROUGHNESS, &perceptualRoughness, sizeof(perceptualRoughness));
}

extern "C" bool Fluxion_Material_SetReflectance(FluxionMaterialHandle material, f32 reflectance)
{
    return WriteCanonical(material, FLUXION_MATERIAL_PARAM_REFLECTANCE, &reflectance, sizeof(reflectance));
}

extern "C" bool Fluxion_Material_SetEmissive(FluxionMaterialHandle material, FluxionVec3 emissive)
{
    return WriteCanonical(material, FLUXION_MATERIAL_PARAM_EMISSIVE, &emissive, sizeof(emissive));
}

extern "C" bool Fluxion_Material_SetNormalScale(FluxionMaterialHandle material, f32 normalScale)
{
    return WriteCanonical(material, FLUXION_MATERIAL_PARAM_NORMAL_SCALE, &normalScale, sizeof(normalScale));
}

extern "C" bool Fluxion_Material_SetOcclusionStrength(FluxionMaterialHandle material, f32 occlusionStrength)
{
    return WriteCanonical(material, FLUXION_MATERIAL_PARAM_OCCLUSION_STRENGTH, &occlusionStrength, sizeof(occlusionStrength));
}

extern "C" bool Fluxion_Material_SetAlphaCutoff(FluxionMaterialHandle material, f32 alphaCutoff)
{
    return WriteCanonical(material, FLUXION_MATERIAL_PARAM_ALPHA_CUTOFF, &alphaCutoff, sizeof(alphaCutoff));
}

extern "C" bool Fluxion_Material_SetTextureSlot(FluxionMaterialHandle material, FluxionMaterialTextureSlot slot,
                                                FluxionRHITextureViewHandle view, FluxionRHISamplerHandle sampler)
{
    if ((u32)slot >= (u32)FLUXION_MATERIAL_TEXTURE_COUNT) return false;
    return Fluxion_Material_SetTexture(material, kCanonicalTextures[slot], view, sampler);
}

extern "C" void Fluxion_Material_SetAlphaMode(FluxionMaterialHandle material, FluxionMaterialAlphaMode mode)
{
    FluxionMaterialRecord* record = Resolve(material);
    if (record == nullptr) return;
    record->alphaMode = mode;
}

extern "C" FluxionMaterialAlphaMode Fluxion_Material_GetAlphaMode(FluxionMaterialHandle material)
{
    const FluxionMaterialRecord* record = Resolve(material);

    // Opaque for a handle that names nothing: a caller sorting draws gets
    // the cheapest answer rather than one that would put a dead material
    // into the blended pass.
    if (record == nullptr) return FLUXION_MATERIAL_ALPHA_OPAQUE;
    return record->alphaMode;
}
