// C++ (not C), same reasoning as RenderGraphCompiler.cpp: this file needs
// Fluxion::ShaderCompiler's ShaderIRModule/CompiledShader (std::string,
// std::vector) for reflection, and the DXC adapter's HLSL text -> SPIR-V
// bytes step -- there is no C-safe way to hold either.

#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#include "RendererInternal.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>
#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <cstring>

using namespace Fluxion::ShaderCompiler;

namespace
{
// MSVC's CRT flags plain strncpy as deprecated regardless of safety --
// same platform difference NullBackend.c already wraps for strncpy_s.
void CopyBoundedName(char* dest, usize destSize, const std::string& src)
{
#if defined(_MSC_VER)
    strncpy_s(dest, destSize, src.c_str(), destSize - 1);
#else
    std::strncpy(dest, src.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
#endif
}
} // namespace

namespace
{

struct FluxionShaderProgramRecord
{
    bool alive = false;
    u32 generation = 0;

    bool isCompute = false;

    FluxionRHIShaderHandle vertexShader{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIShaderHandle fragmentShader{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIShaderHandle computeShader{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    // MATERIAL-frequency bind group layout, derived once here from
    // whichever stage(s) reflect a [Uniform(Material)]/[Texture(Material)]
    // -- shared by every RenderPipeline/Material built from this program.
    FluxionRHIBindGroupLayoutHandle materialBindGroupLayout{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionMaterialParameterInfo materialParams[FLUXION_RENDERER_MAX_MATERIAL_PARAMETERS]{};
    u32 materialParamCount = 0;
    u32 materialUniformBufferSize = 0;
};

FluxionShaderProgramRecord s_programs[FLUXION_RENDERER_MAX_SHADER_PROGRAMS];

const FluxionShaderProgramRecord* Resolve(FluxionShaderProgramHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_SHADER_PROGRAMS) return nullptr;
    const FluxionShaderProgramRecord* record = &s_programs[handle.index];
    if (!record->alive || record->generation != handle.generation) return nullptr;
    return record;
}

// Merges every stage module's MATERIAL-group entries into one bind group
// layout desc -- a uniform buffer (if any) always occupies binding 0,
// each texture occupies its own reflected binding/samplerBinding pair
// (see IRResourceBinding's comment in ShaderIR.hpp). Real .jsl shaders
// only ever declare a given group's resources in one stage in practice,
// but this merges across both anyway rather than assuming that.
FluxionRHIBindGroupLayoutDesc BuildMaterialLayoutDesc(const ShaderIRModule* vertexIR, const ShaderIRModule* fragmentIR, const ShaderIRModule* computeIR)
{
    const ShaderIRModule* modules[3] = { vertexIR, fragmentIR, computeIR };

    FluxionRHIBindGroupLayoutDesc desc{};
    desc.debugName = "Fluxion.Material.BindGroupLayout";

    bool haveUniformEntry = false;
    for (const ShaderIRModule* module : modules)
    {
        if (module == nullptr) continue;
        for (const IRUniformBufferBinding& uniformBuffer : module->uniformBuffers)
        {
            if (uniformBuffer.group != BindingGroup::Material || haveUniformEntry) continue;
            if (desc.entryCount >= FLUXION_RHI_MAX_BIND_GROUP_ENTRIES) break;

            FluxionRHIBindGroupLayoutEntryDesc& entry = desc.entries[desc.entryCount++];
            entry.binding = 0;
            entry.type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
            entry.visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX | FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT | FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;
            haveUniformEntry = true;
        }
    }
    for (const ShaderIRModule* module : modules)
    {
        if (module == nullptr) continue;
        for (const IRResourceBinding& resource : module->resources)
        {
            if (resource.group != BindingGroup::Material) continue;
            if (desc.entryCount + 2 > FLUXION_RHI_MAX_BIND_GROUP_ENTRIES) break;

            FluxionRHIBindGroupLayoutEntryDesc& textureEntry = desc.entries[desc.entryCount++];
            textureEntry.binding = (u32)resource.binding;
            textureEntry.type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
            textureEntry.visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX | FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT | FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;

            FluxionRHIBindGroupLayoutEntryDesc& samplerEntry = desc.entries[desc.entryCount++];
            samplerEntry.binding = (u32)resource.samplerBinding;
            samplerEntry.type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
            samplerEntry.visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX | FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT | FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE;
        }
    }
    return desc;
}

usize TypeKindByteSize(TypeKind kind)
{
    switch (kind)
    {
        case TypeKind::Bool:
        case TypeKind::Int:
        case TypeKind::Uint:
        case TypeKind::Float: return 4;
        case TypeKind::Vec2: return 8;
        case TypeKind::Vec3: return 12;
        case TypeKind::Vec4: return 16;
        case TypeKind::Mat3: return 48;
        case TypeKind::Mat4: return 64;
        default: return 0;
    }
}

FluxionMaterialParameterKind TypeKindToParameterKind(TypeKind kind)
{
    switch (kind)
    {
        case TypeKind::Float: return FLUXION_MATERIAL_PARAMETER_FLOAT;
        case TypeKind::Vec3: return FLUXION_MATERIAL_PARAMETER_VEC3;
        case TypeKind::Vec4: return FLUXION_MATERIAL_PARAMETER_VEC4;
        default: return FLUXION_MATERIAL_PARAMETER_OTHER;
    }
}

// Collects every MATERIAL-group uniform member (as a scalar/vector
// parameter) and every MATERIAL-group texture resource, across whichever
// stage(s) are present, into `record` -- run once at Create time, never
// recomputed (Material.h's contract: a material's parameter layout is
// fixed for its lifetime).
void CollectMaterialParameters(FluxionShaderProgramRecord& record, const ShaderIRModule* vertexIR, const ShaderIRModule* fragmentIR, const ShaderIRModule* computeIR)
{
    const ShaderIRModule* modules[3] = { vertexIR, fragmentIR, computeIR };

    for (const ShaderIRModule* module : modules)
    {
        if (module == nullptr) continue;
        for (const IRUniformBufferBinding& uniformBuffer : module->uniformBuffers)
        {
            if (uniformBuffer.group != BindingGroup::Material) continue;
            record.materialUniformBufferSize = uniformBuffer.size;
            for (const IRUniformBufferMember& member : uniformBuffer.members)
            {
                if (record.materialParamCount >= FLUXION_RENDERER_MAX_MATERIAL_PARAMETERS) break;
                FluxionMaterialParameterInfo& info = record.materialParams[record.materialParamCount++];
                CopyBoundedName(info.name, sizeof(info.name), member.name);
                info.kind = TypeKindToParameterKind(member.type.kind);
                info.offset = member.offset;
                info.size = (u32)TypeKindByteSize(member.type.kind);
                info.binding = 0;
                info.samplerBinding = 0;
            }
        }
    }
    for (const ShaderIRModule* module : modules)
    {
        if (module == nullptr) continue;
        for (const IRResourceBinding& resource : module->resources)
        {
            if (resource.group != BindingGroup::Material) continue;
            if (record.materialParamCount >= FLUXION_RENDERER_MAX_MATERIAL_PARAMETERS) break;
            FluxionMaterialParameterInfo& info = record.materialParams[record.materialParamCount++];
            CopyBoundedName(info.name, sizeof(info.name), resource.name);
            info.kind = FLUXION_MATERIAL_PARAMETER_TEXTURE;
            info.offset = 0;
            info.size = 0;
            info.binding = (u32)resource.binding;
            info.samplerBinding = (u32)resource.samplerBinding;
        }
    }
}

FluxionRHIShaderStage ToRHIStage(ShaderStage stage)
{
    switch (stage)
    {
        case ShaderStage::Vertex: return FLUXION_RHI_SHADER_STAGE_VERTEX;
        case ShaderStage::Fragment: return FLUXION_RHI_SHADER_STAGE_FRAGMENT;
        case ShaderStage::Compute: return FLUXION_RHI_SHADER_STAGE_COMPUTE;
    }
    return FLUXION_RHI_SHADER_STAGE_FRAGMENT;
}

// Compiles one .jsl source string through the shared front end, then
// branches on the target backend to produce whatever bytecode/source form
// that backend's Fluxion_RHI_CreateShader expects: OpenGL takes the GLSL
// source text directly (its RHI backend compiles/links it with
// glShaderSource/glCompileShader itself), D3D12 wants HLSL text run
// through dxc to DXIL, and Vulkan (and everything else, e.g. the Null
// backend) wants HLSL text run through dxc to SPIR-V. Mirrors
// Samples/ForwardRendererDemo's CompileShaderStage exactly.
bool CompileStage(FluxionRHIDeviceHandle device, FluxionRHIBackendType backend, const char* source, const char* entryPoint, const char* debugName, ShaderStage stage, ShaderIRModule* outIR, FluxionRHIShaderHandle* outShader)
{
    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = stage;
    options.entryPoint = (entryPoint != nullptr) ? entryPoint : "main";
    options.fileName = (debugName != nullptr) ? debugName : "<FluxionShaderProgram>";

    auto compiled = Compile(source, options, diagnostics);
    if (!compiled.IsOk())
    {
        for (const Diagnostic& d : diagnostics.entries)
        {
            FLUXION_LOG_ERROR("ShaderProgram", "%s:%u: %s", d.location.file.c_str(), d.location.line, d.message.c_str());
        }
        return false;
    }

    *outIR = compiled.Value().reflection;

    if (backend == FLUXION_RHI_BACKEND_OPENGL)
    {
        const std::string& glsl = compiled.Value().glslSource;

        FluxionRHIShaderDesc shaderDesc;
        shaderDesc.stage = ToRHIStage(stage);
        shaderDesc.bytecode = reinterpret_cast<const u8*>(glsl.data());
        shaderDesc.bytecodeSize = glsl.size();
        shaderDesc.entryPoint = "main";
        shaderDesc.debugName = debugName;

        *outShader = Fluxion_RHI_CreateShader(device, &shaderDesc);
        return FLUXION_HANDLE_IS_VALID(*outShader);
    }

    DiagnosticList dxcDiagnostics;

    if (backend == FLUXION_RHI_BACKEND_D3D12)
    {
        auto dxil = CompileToDxil(compiled.Value().hlslSource, stage, "main", dxcDiagnostics);
        if (!dxil.IsOk())
        {
            for (const Diagnostic& d : dxcDiagnostics.entries)
            {
                FLUXION_LOG_ERROR("ShaderProgram", "dxc: %s", d.message.c_str());
            }
            return false;
        }

        FluxionRHIShaderDesc shaderDesc;
        shaderDesc.stage = ToRHIStage(stage);
        shaderDesc.bytecode = dxil.Value().data();
        shaderDesc.bytecodeSize = dxil.Value().size();
        shaderDesc.entryPoint = "main"; // the HLSL backend always emits a function literally named `main`, regardless of the .jsl source's own entry name
        shaderDesc.debugName = debugName;

        *outShader = Fluxion_RHI_CreateShader(device, &shaderDesc);
        return FLUXION_HANDLE_IS_VALID(*outShader);
    }

    // Vulkan, Null, and anything else: HLSL text -> dxc -> SPIR-V.
    auto spirv = CompileToSpirv(compiled.Value().hlslSource, stage, "main", dxcDiagnostics);
    if (!spirv.IsOk())
    {
        for (const Diagnostic& d : dxcDiagnostics.entries)
        {
            FLUXION_LOG_ERROR("ShaderProgram", "dxc: %s", d.message.c_str());
        }
        return false;
    }

    FluxionRHIShaderDesc shaderDesc;
    shaderDesc.stage = ToRHIStage(stage);
    shaderDesc.bytecode = spirv.Value().data();
    shaderDesc.bytecodeSize = spirv.Value().size();
    shaderDesc.entryPoint = "main"; // the HLSL backend always emits a function literally named `main`, regardless of the .jsl source's own entry name
    shaderDesc.debugName = debugName;

    *outShader = Fluxion_RHI_CreateShader(device, &shaderDesc);
    return FLUXION_HANDLE_IS_VALID(*outShader);
}

} // namespace

extern "C" FluxionShaderProgramHandle Fluxion_ShaderProgram_Create(FluxionRHIDeviceHandle device, const FluxionShaderProgramDesc* desc)
{
    FluxionShaderProgramHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(desc != nullptr);

    bool wantsGraphics = desc->vertexSource != nullptr && desc->fragmentSource != nullptr;
    bool wantsCompute = desc->computeSource != nullptr;
    FLUXION_ASSERT_MSG(wantsGraphics != wantsCompute, "FluxionShaderProgramDesc must set exactly one of (vertexSource+fragmentSource) or computeSource");
    if (wantsGraphics == wantsCompute) return invalid;

    u32 index = FLUXION_RENDERER_MAX_SHADER_PROGRAMS;
    for (u32 i = 0; i < FLUXION_RENDERER_MAX_SHADER_PROGRAMS; ++i)
    {
        if (!s_programs[i].alive) { index = i; break; }
    }
    if (index == FLUXION_RENDERER_MAX_SHADER_PROGRAMS)
    {
        FLUXION_LOG_ERROR("ShaderProgram", "Exceeded FLUXION_RENDERER_MAX_SHADER_PROGRAMS");
        return invalid;
    }

    FluxionRHIBackendType backend = Fluxion_RHI_GetDeviceBackendType(device);

    ShaderIRModule vertexIR, fragmentIR, computeIR;
    FluxionShaderProgramRecord record;
    record.isCompute = wantsCompute;

    bool ok = true;
    if (wantsCompute)
    {
        ok = CompileStage(device, backend, desc->computeSource, desc->computeEntryPoint, desc->debugName, ShaderStage::Compute, &computeIR, &record.computeShader);
    }
    else
    {
        ok = CompileStage(device, backend, desc->vertexSource, desc->vertexEntryPoint, desc->debugName, ShaderStage::Vertex, &vertexIR, &record.vertexShader);
        ok = ok && CompileStage(device, backend, desc->fragmentSource, desc->fragmentEntryPoint, desc->debugName, ShaderStage::Fragment, &fragmentIR, &record.fragmentShader);
    }

    if (!ok)
    {
        if (FLUXION_HANDLE_IS_VALID(record.vertexShader)) Fluxion_RHI_DestroyShader(record.vertexShader);
        if (FLUXION_HANDLE_IS_VALID(record.fragmentShader)) Fluxion_RHI_DestroyShader(record.fragmentShader);
        if (FLUXION_HANDLE_IS_VALID(record.computeShader)) Fluxion_RHI_DestroyShader(record.computeShader);
        return invalid;
    }

    FluxionRHIBindGroupLayoutDesc materialLayoutDesc = wantsCompute
        ? BuildMaterialLayoutDesc(nullptr, nullptr, &computeIR)
        : BuildMaterialLayoutDesc(&vertexIR, &fragmentIR, nullptr);
    record.materialBindGroupLayout = Fluxion_RHI_CreateBindGroupLayout(device, &materialLayoutDesc);

    if (wantsCompute)
    {
        CollectMaterialParameters(record, nullptr, nullptr, &computeIR);
    }
    else
    {
        CollectMaterialParameters(record, &vertexIR, &fragmentIR, nullptr);
    }

    record.alive = true;
    record.generation = s_programs[index].generation;
    s_programs[index] = record;

    FluxionShaderProgramHandle handle = { index, s_programs[index].generation };
    return handle;
}

extern "C" void Fluxion_ShaderProgram_Destroy(FluxionShaderProgramHandle program)
{
    if (program.index >= FLUXION_RENDERER_MAX_SHADER_PROGRAMS) return;
    FluxionShaderProgramRecord* record = &s_programs[program.index];
    if (!record->alive || record->generation != program.generation)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion_ShaderProgram_Destroy called with an invalid or already-destroyed handle");
        return;
    }

    if (FLUXION_HANDLE_IS_VALID(record->vertexShader)) Fluxion_RHI_DestroyShader(record->vertexShader);
    if (FLUXION_HANDLE_IS_VALID(record->fragmentShader)) Fluxion_RHI_DestroyShader(record->fragmentShader);
    if (FLUXION_HANDLE_IS_VALID(record->computeShader)) Fluxion_RHI_DestroyShader(record->computeShader);
    if (FLUXION_HANDLE_IS_VALID(record->materialBindGroupLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->materialBindGroupLayout);

    record->alive = false;
    ++record->generation;
}

extern "C" bool FluxionRendererInternal_ShaderProgram_IsCompute(FluxionShaderProgramHandle program)
{
    const FluxionShaderProgramRecord* record = Resolve(program);
    return record != nullptr && record->isCompute;
}

extern "C" FluxionRHIShaderHandle FluxionRendererInternal_ShaderProgram_GetVertexShader(FluxionShaderProgramHandle program)
{
    const FluxionShaderProgramRecord* record = Resolve(program);
    FluxionRHIShaderHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return record != nullptr ? record->vertexShader : invalid;
}

extern "C" FluxionRHIShaderHandle FluxionRendererInternal_ShaderProgram_GetFragmentShader(FluxionShaderProgramHandle program)
{
    const FluxionShaderProgramRecord* record = Resolve(program);
    FluxionRHIShaderHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return record != nullptr ? record->fragmentShader : invalid;
}

extern "C" FluxionRHIShaderHandle FluxionRendererInternal_ShaderProgram_GetComputeShader(FluxionShaderProgramHandle program)
{
    const FluxionShaderProgramRecord* record = Resolve(program);
    FluxionRHIShaderHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return record != nullptr ? record->computeShader : invalid;
}

extern "C" FluxionRHIBindGroupLayoutHandle FluxionRendererInternal_ShaderProgram_GetMaterialBindGroupLayout(FluxionShaderProgramHandle program)
{
    const FluxionShaderProgramRecord* record = Resolve(program);
    FluxionRHIBindGroupLayoutHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return record != nullptr ? record->materialBindGroupLayout : invalid;
}

extern "C" u32 FluxionRendererInternal_ShaderProgram_GetMaterialParameters(FluxionShaderProgramHandle program, FluxionMaterialParameterInfo* outParams, u32 maxParams, u32* outUniformBufferSize)
{
    const FluxionShaderProgramRecord* record = Resolve(program);
    if (record == nullptr)
    {
        if (outUniformBufferSize != nullptr) *outUniformBufferSize = 0;
        return 0;
    }

    if (outUniformBufferSize != nullptr) *outUniformBufferSize = record->materialUniformBufferSize;

    u32 count = record->materialParamCount < maxParams ? record->materialParamCount : maxParams;
    for (u32 i = 0; i < count; ++i) outParams[i] = record->materialParams[i];
    return count;
}
