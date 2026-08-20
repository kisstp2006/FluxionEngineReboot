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

// C++ (not C), same reasoning as RenderGraphCompiler.cpp: this file needs
// Fluxion::ShaderCompiler's ShaderIRModule/CompiledShader (std::string,
// std::vector) for reflection, and the DXC adapter's HLSL text -> SPIR-V
// bytes step -- there is no C-safe way to hold either.

#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#include "RendererInternal.h"

#include <Fluxion/Core/Diagnostics/ProfileScope.hpp>

#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>
#include <Fluxion/ShaderCompiler/ShaderCache.hpp>
#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include "ShaderLibrary.h"

#include <atomic>
#include <cstring>
#include <string>

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

    // Kept past creation because a pipeline built from this program needs
    // a name that means the same thing in the next run, and this is the
    // only part of a program that a caller chose rather than the compiler
    // deriving. A reload does not touch it.
    std::string debugName;
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
// Said once by the host, and empty until it does. Kept here rather than
// on each program's description because it is one answer for a whole run.
std::string s_cacheDirectory;

// Which shape a backend wants handed to it. The compiler names these in
// its own terms and knows nothing about backends; this is the one place
// the two vocabularies meet.
ArtifactTarget TargetForBackend(FluxionRHIBackendType backend)
{
    if (backend == FLUXION_RHI_BACKEND_OPENGL) return ArtifactTarget::Glsl;
    if (backend == FLUXION_RHI_BACKEND_D3D12) return ArtifactTarget::Dxil;
    return ArtifactTarget::Spirv; // Vulkan, Null, and anything else
}

// The half of the work that touches no device.
//
// Split out from the half that does, because this is the slow half and
// the only one that may run anywhere other than the thread that owns the
// device. An OpenGL context belongs to exactly one thread, so a shader
// object may only ever be created on that thread -- what a worker is
// allowed to produce is bytes, and nothing else.
bool CompileArtifact(FluxionRHIBackendType backend, const char* source, const char* entryPoint, const char* debugName, ShaderStage stage,
    CompiledArtifact* outArtifact)
{
    // Shader compilation is the demo's single largest startup cost and
    // the whole of a hot reload's background work -- the zone is what
    // shows whether the cache is actually being hit.
    FLUXION_PROFILE_FUNCTION();

    DiagnosticList diagnostics;

    ArtifactRequest request;
    request.target = TargetForBackend(backend);
    request.compile.stage = stage;
    request.compile.entryPoint = (entryPoint != nullptr) ? entryPoint : "main";
    request.compile.fileName = (debugName != nullptr) ? debugName : "<FluxionShaderProgram>";

    // What a shader is allowed to include. Set on every compilation this
    // module starts, including the ones a worker runs during a reload --
    // the library is read-only text built into the program, so several
    // threads reading it at once is not a question.
    //
    // The cache needs no telling about this. It runs the front end before
    // it builds its key, and puts every include's name and content hash
    // into that key -- so editing a library file changes the key, and a
    // result compiled against the old text can no longer be found.
    request.compile.includeResolver = Fluxion::RenderCore::MakeShaderLibraryResolver();

    ShaderCacheOptions cache;
    cache.directory = s_cacheDirectory;

    ShaderCacheReport report;
    auto artifact = CompileArtifactCached(source, request, cache, diagnostics, report);
    if (!artifact.IsOk())
    {
        // One report for both halves of the work: a failure in the front
        // end and a failure in the external tool both land here, and both
        // have already put what they know into the same list.
        for (const Diagnostic& d : diagnostics.entries)
        {
            FLUXION_LOG_ERROR("ShaderProgram", "%s:%u: %s", d.location.file.c_str(), d.location.line, d.message.c_str());
        }
        return false;
    }

    *outArtifact = std::move(artifact.Value());
    return true;
}

// The half that does. Always on the thread that owns the device.
bool CreateShaderFromArtifact(FluxionRHIDeviceHandle device, const CompiledArtifact& artifact, ShaderStage stage, const char* debugName,
    FluxionRHIShaderHandle* outShader)
{
    FluxionRHIShaderDesc shaderDesc;
    shaderDesc.stage = ToRHIStage(stage);
    shaderDesc.bytecode = artifact.bytes.data();
    shaderDesc.bytecodeSize = artifact.bytes.size();
    // Always literally `main`: the HLSL backend emits a function by that
    // name whatever the source called its own, and the GLSL backend does
    // the same.
    shaderDesc.entryPoint = "main";
    shaderDesc.debugName = debugName;

    *outShader = Fluxion_RHI_CreateShader(device, &shaderDesc);
    return FLUXION_HANDLE_IS_VALID(*outShader);
}

bool CompileStage(FluxionRHIDeviceHandle device, FluxionRHIBackendType backend, const char* source, const char* entryPoint, const char* debugName, ShaderStage stage, ShaderIRModule* outIR, FluxionRHIShaderHandle* outShader)
{
    CompiledArtifact artifact;
    if (!CompileArtifact(backend, source, entryPoint, debugName, stage, &artifact)) return false;

    *outIR = artifact.reflection;
    return CreateShaderFromArtifact(device, artifact, stage, debugName, outShader);
}

// Whether two programs present the same material side. This is exactly
// the set of things a material copies out of a program and then relies on
// forever: how many parameters there are, what each is called, where in
// the buffer it sits, how big it is, and which bindings a texture
// occupies -- plus the total buffer size, which is what the material
// allocated.
//
// Anything outside that set may differ freely: the shader body, the
// stage IO, the order the parameters were declared in. What matters is
// only whether a material built against one can go on being used against
// the other.
bool SameMaterialLayout(const FluxionShaderProgramRecord& a, const FluxionShaderProgramRecord& b)
{
    if (a.materialParamCount != b.materialParamCount) return false;
    if (a.materialUniformBufferSize != b.materialUniformBufferSize) return false;

    for (u32 i = 0; i < a.materialParamCount; ++i)
    {
        const FluxionMaterialParameterInfo& x = a.materialParams[i];
        const FluxionMaterialParameterInfo& y = b.materialParams[i];
        if (std::strcmp(x.name, y.name) != 0) return false;
        if (x.kind != y.kind) return false;
        if (x.offset != y.offset || x.size != y.size) return false;
        if (x.binding != y.binding || x.samplerBinding != y.samplerBinding) return false;
    }
    return true;
}

// One reload, in three steps that can be taken at three different times
// and, for the middle one, on a different thread.
//
// Splitting it this way is what lets the slow part happen off the frame:
// describing is cheap and needs the record, compiling is slow and needs
// nothing, applying is quick and needs the device. Only the first and
// last touch anything shared.
struct ReloadRequest
{
    FluxionRHIDeviceHandle device{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionShaderProgramHandle program{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBackendType backend = FLUXION_RHI_BACKEND_NULL;
    bool isCompute = false;

    // Copied, so a caller's strings need not outlive the call that
    // started this -- a reload begun from a key press is applied frames
    // later, long after whatever held the source has moved on.
    std::string debugName;
    std::string vertexSource, vertexEntry;
    std::string fragmentSource, fragmentEntry;
    std::string computeSource, computeEntry;

    CompiledArtifact vertexArtifact, fragmentArtifact, computeArtifact;
    bool compiled = false;
};

const char* OrNull(const std::string& value) { return value.empty() ? nullptr : value.c_str(); }

// Step one: is this a reload at all, and of what. Reads the record, so
// it belongs on the thread that owns it.
bool DescribeReload(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program, const FluxionShaderProgramDesc* desc, ReloadRequest& out)
{
    if (desc == nullptr) return false;
    if (program.index >= FLUXION_RENDERER_MAX_SHADER_PROGRAMS) return false;

    const FluxionShaderProgramRecord* record = &s_programs[program.index];
    if (!record->alive || record->generation != program.generation) return false;

    const bool wantsGraphics = desc->vertexSource != nullptr && desc->fragmentSource != nullptr;
    const bool wantsCompute = desc->computeSource != nullptr;
    if (wantsGraphics == wantsCompute) return false;

    // A program does not change shape. A graphics program reloaded from a
    // compute source would need a different pipeline entirely, and every
    // pipeline built from it assumes otherwise.
    if (wantsCompute != record->isCompute) return false;

    out.device = device;
    out.program = program;
    out.backend = Fluxion_RHI_GetDeviceBackendType(device);
    out.isCompute = wantsCompute;
    out.debugName = desc->debugName != nullptr ? desc->debugName : "";
    if (wantsCompute)
    {
        out.computeSource = desc->computeSource;
        out.computeEntry = desc->computeEntryPoint != nullptr ? desc->computeEntryPoint : "";
    }
    else
    {
        out.vertexSource = desc->vertexSource;
        out.vertexEntry = desc->vertexEntryPoint != nullptr ? desc->vertexEntryPoint : "";
        out.fragmentSource = desc->fragmentSource;
        out.fragmentEntry = desc->fragmentEntryPoint != nullptr ? desc->fragmentEntryPoint : "";
    }
    return true;
}

// Step two: the slow half. Touches no device and no shared record, which
// is exactly why it may run on a worker.
bool CompileReload(ReloadRequest& request)
{
    const char* name = OrNull(request.debugName);
    bool ok;
    if (request.isCompute)
    {
        ok = CompileArtifact(request.backend, request.computeSource.c_str(), OrNull(request.computeEntry), name, ShaderStage::Compute, &request.computeArtifact);
    }
    else
    {
        ok = CompileArtifact(request.backend, request.vertexSource.c_str(), OrNull(request.vertexEntry), name, ShaderStage::Vertex, &request.vertexArtifact);
        ok = ok && CompileArtifact(request.backend, request.fragmentSource.c_str(), OrNull(request.fragmentEntry), name, ShaderStage::Fragment, &request.fragmentArtifact);
    }
    request.compiled = ok;
    return ok;
}

// Step three: makes the shader objects and swaps them in. Back on the
// thread that owns the device, and between frames.
FluxionShaderProgramReloadOutcome ApplyReload(const ReloadRequest& request)
{
    // The record is re-checked rather than remembered: a reload begun
    // frames ago may have outlived the program it was for.
    if (request.program.index >= FLUXION_RENDERER_MAX_SHADER_PROGRAMS) return FLUXION_SHADER_PROGRAM_RELOAD_INVALID_REQUEST;
    FluxionShaderProgramRecord* record = &s_programs[request.program.index];
    if (!record->alive || record->generation != request.program.generation) return FLUXION_SHADER_PROGRAM_RELOAD_INVALID_REQUEST;
    if (!request.compiled) return FLUXION_SHADER_PROGRAM_RELOAD_COMPILE_FAILED;

    const char* name = OrNull(request.debugName);

    // Everything new is built beside the live program, and only swapped
    // in once it is known to be both good and compatible. Until the swap,
    // a failure costs nothing: what was rendering is still rendering.
    FluxionShaderProgramRecord candidate;
    candidate.isCompute = record->isCompute;

    bool ok;
    if (request.isCompute)
    {
        ok = CreateShaderFromArtifact(request.device, request.computeArtifact, ShaderStage::Compute, name, &candidate.computeShader);
    }
    else
    {
        ok = CreateShaderFromArtifact(request.device, request.vertexArtifact, ShaderStage::Vertex, name, &candidate.vertexShader);
        ok = ok && CreateShaderFromArtifact(request.device, request.fragmentArtifact, ShaderStage::Fragment, name, &candidate.fragmentShader);
    }

    auto discardCandidate = [&candidate]
    {
        if (FLUXION_HANDLE_IS_VALID(candidate.vertexShader)) Fluxion_RHI_DestroyShader(candidate.vertexShader);
        if (FLUXION_HANDLE_IS_VALID(candidate.fragmentShader)) Fluxion_RHI_DestroyShader(candidate.fragmentShader);
        if (FLUXION_HANDLE_IS_VALID(candidate.computeShader)) Fluxion_RHI_DestroyShader(candidate.computeShader);
    };

    if (!ok)
    {
        discardCandidate();
        return FLUXION_SHADER_PROGRAM_RELOAD_COMPILE_FAILED;
    }

    // What the new source says the material side looks like, compared
    // against what is in use. Only the derived answer is compared, not
    // the reflection it came from: two sources that describe the same
    // parameters in a different order are the same thing to a material.
    if (request.isCompute) CollectMaterialParameters(candidate, nullptr, nullptr, &request.computeArtifact.reflection);
    else CollectMaterialParameters(candidate, &request.vertexArtifact.reflection, &request.fragmentArtifact.reflection, nullptr);

    if (!SameMaterialLayout(*record, candidate))
    {
        discardCandidate();
        FLUXION_LOG_ERROR("ShaderProgram",
            "'%s' was not reloaded: its material parameters changed, and every material built from it holds offsets from the old ones",
            name != nullptr ? name : "<unnamed>");
        return FLUXION_SHADER_PROGRAM_RELOAD_LAYOUT_CHANGED;
    }

    // Past this point nothing can fail, which is what makes the swap
    // safe to do in pieces.
    //
    // The bind group layout is deliberately kept rather than rebuilt. It
    // describes a shape that was just proven unchanged, and every
    // material holds this exact handle -- replacing it would leave all of
    // them pointing at a destroyed object for no gain. The material
    // parameter table is kept for the same reason.
    const FluxionRHIShaderHandle oldVertex = record->vertexShader;
    const FluxionRHIShaderHandle oldFragment = record->fragmentShader;
    const FluxionRHIShaderHandle oldCompute = record->computeShader;

    record->vertexShader = candidate.vertexShader;
    record->fragmentShader = candidate.fragmentShader;
    record->computeShader = candidate.computeShader;

    // Every pipeline built from this program baked the old shader handles
    // into a native pipeline object at build time and never re-reads
    // them, so each one has to go. They are rebuilt on next use.
    FluxionRendererInternal_RenderPipeline_InvalidateVariantsUsingProgram(request.program);

    if (FLUXION_HANDLE_IS_VALID(oldVertex)) Fluxion_RHI_DestroyShader(oldVertex);
    if (FLUXION_HANDLE_IS_VALID(oldFragment)) Fluxion_RHI_DestroyShader(oldFragment);
    if (FLUXION_HANDLE_IS_VALID(oldCompute)) Fluxion_RHI_DestroyShader(oldCompute);

    return FLUXION_SHADER_PROGRAM_RELOAD_OK;
}

} // namespace

// The middle step, wrapped so a worker can run it and the thread that
// started it can tell when it is over without waiting.
struct FluxionShaderProgramReloadJob
{
    ReloadRequest request;

    // Explicitly nothing, not a zeroed handle: index zero is a perfectly
    // good slot, so a default-constructed handle reads as valid and would
    // send a wait to a job that was never submitted.
    FluxionJobHandle handle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    // Released by the worker, acquired by whoever asks. Everything the
    // worker wrote into `request` is visible to a reader that has seen
    // this as true, and to no one before that.
    std::atomic<bool> done{ false };
};

extern "C" void Fluxion_ShaderProgram_SetCacheDirectory(const char* directory)
{
    s_cacheDirectory = (directory != nullptr) ? directory : "";
}

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
    record.debugName = desc->debugName != nullptr ? desc->debugName : "";
    s_programs[index] = record;

    FluxionShaderProgramHandle handle = { index, s_programs[index].generation };
    return handle;
}

extern "C" FluxionShaderProgramReloadOutcome Fluxion_ShaderProgram_Reload(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program,
    const FluxionShaderProgramDesc* desc)
{
    ReloadRequest request;
    if (!DescribeReload(device, program, desc, request)) return FLUXION_SHADER_PROGRAM_RELOAD_INVALID_REQUEST;
    if (!CompileReload(request)) return FLUXION_SHADER_PROGRAM_RELOAD_COMPILE_FAILED;
    return ApplyReload(request);
}

namespace
{

void RunReloadJob(void* data)
{
    FluxionShaderProgramReloadJob* job = *static_cast<FluxionShaderProgramReloadJob**>(data);
    CompileReload(job->request);
    job->done.store(true, std::memory_order_release);
}

} // namespace

extern "C" FluxionShaderProgramReloadJob* Fluxion_ShaderProgram_BeginReload(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program,
    const FluxionShaderProgramDesc* desc)
{
    auto* job = new FluxionShaderProgramReloadJob();
    if (!DescribeReload(device, program, desc, job->request))
    {
        delete job;
        return nullptr;
    }

    // Asked rather than attempted: submitting to a job system that was
    // never started asserts, and rightly so -- handing work to something
    // that does not exist is a mistake. But a host that never started one
    // is not making a mistake by reloading a shader, so the question is
    // asked first and the work simply done here if the answer is no.
    if (Fluxion_JobSystem_IsInitialized())
    {
        FluxionShaderProgramReloadJob* payload = job;

        FluxionJobDesc jobDesc = {};
        jobDesc.function = RunReloadJob;
        std::memcpy(jobDesc.data, &payload, sizeof(payload));
        jobDesc.dataSize = sizeof(payload);

        job->handle = Fluxion_JobSystem_Submit(&jobDesc);
    }

    if (!FLUXION_HANDLE_IS_VALID(job->handle))
    {
        // Either there was no job system, or it had no room, or it is
        // running everything inline. None of those is a failure: the work
        // gets done here and now, and the only difference the caller sees
        // is that the answer is already waiting the first time it asks.
        CompileReload(job->request);
        job->done.store(true, std::memory_order_release);
    }
    return job;
}

extern "C" bool Fluxion_ShaderProgram_IsReloadReady(const FluxionShaderProgramReloadJob* job)
{
    return job != nullptr && job->done.load(std::memory_order_acquire);
}

extern "C" FluxionShaderProgramReloadOutcome Fluxion_ShaderProgram_FinishReload(FluxionShaderProgramReloadJob* job)
{
    if (job == nullptr) return FLUXION_SHADER_PROGRAM_RELOAD_INVALID_REQUEST;

    if (!job->done.load(std::memory_order_acquire)) Fluxion_JobSystem_Wait(job->handle);

    const FluxionShaderProgramReloadOutcome outcome = ApplyReload(job->request);
    delete job;
    return outcome;
}

extern "C" FluxionRHIShaderHandle Fluxion_ShaderProgram_GetVertexShader(FluxionShaderProgramHandle program)
{
    return FluxionRendererInternal_ShaderProgram_GetVertexShader(program);
}

extern "C" FluxionRHIShaderHandle Fluxion_ShaderProgram_GetFragmentShader(FluxionShaderProgramHandle program)
{
    return FluxionRendererInternal_ShaderProgram_GetFragmentShader(program);
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

extern "C" const char* FluxionRendererInternal_ShaderProgram_GetDebugName(FluxionShaderProgramHandle program)
{
    const FluxionShaderProgramRecord* record = Resolve(program);
    return record != nullptr ? record->debugName.c_str() : "";
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
