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

// Keeping a finished shader beside the source it came from, and reading
// it back only when reading it back is certainly right.
//
// Two rules run through all of this. The first: the key names everything
// that decides the answer. Anything left out of it is a stale result
// served silently, which is the worst outcome available here -- nothing
// fails, nothing is logged, and the device is handed bytes belonging to a
// different compilation. The second: a cache is an optimization and never
// a source of failure. A file that is missing, truncated, filled with
// something else, or written by an older build is a miss and nothing
// more.

#include <Fluxion/ShaderCompiler/ShaderCache.hpp>

#include <Fluxion/Foundation/Hashing.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Fluxion::ShaderCompiler
{
namespace
{

unsigned long FluxionShaderCache_CurrentProcessId()
{
#if defined(_WIN32)
    return (unsigned long)_getpid();
#else
    return (unsigned long)getpid();
#endif
}

// Raised when the bytes on disk stop meaning what they used to. Separate
// from the compiler's own versions: those say what a source is allowed to
// mean and what it turns into, this says how the result was written down.
constexpr u32 kCacheFormatVersion = 1;
constexpr char kMagic[8] = { 'F', 'L', 'X', 'S', 'H', 'C', 'A', '1' };

std::atomic<u64> s_compiled{ 0 };
std::atomic<u64> s_loaded{ 0 };

// --- Byte-level writing and reading -----------------------------------
//
// Explicit little-endian, one byte at a time, no struct ever copied
// wholesale: the bytes have to mean the same thing on a machine that did
// not write them, and padding and byte order are exactly what a raw copy
// would let differ.

struct ByteWriter
{
    std::vector<u8>& out;

    void U8(u8 value) { out.push_back(value); }
    void U32(u32 value)
    {
        out.push_back((u8)(value & 0xFFu));
        out.push_back((u8)((value >> 8) & 0xFFu));
        out.push_back((u8)((value >> 16) & 0xFFu));
        out.push_back((u8)((value >> 24) & 0xFFu));
    }
    void U64(u64 value)
    {
        U32((u32)(value & 0xFFFFFFFFull));
        U32((u32)((value >> 32) & 0xFFFFFFFFull));
    }
    void I32(int value) { U32((u32)value); }
    void Text(const std::string& value)
    {
        U32((u32)value.size());
        out.insert(out.end(), value.begin(), value.end());
    }
    void Bytes(const std::vector<u8>& value)
    {
        U32((u32)value.size());
        out.insert(out.end(), value.begin(), value.end());
    }
};

// Every read is checked against what is actually left. Once anything has
// gone wrong the reader stays failed, so a caller can run a whole
// sequence of reads and ask once at the end -- no single read can be
// forgotten, which is how a checked reader usually ends up unchecked.
struct ByteReader
{
    const u8* data;
    size_t size;
    size_t cursor = 0;
    bool failed = false;

    size_t Remaining() const { return failed ? 0 : size - cursor; }

    bool Take(size_t count)
    {
        if (failed || size - cursor < count) { failed = true; return false; }
        cursor += count;
        return true;
    }

    u8 U8()
    {
        if (!Take(1)) return 0;
        return data[cursor - 1];
    }
    u32 U32()
    {
        if (!Take(4)) return 0;
        const u8* p = data + cursor - 4;
        return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
    }
    u64 U64()
    {
        const u64 low = U32();
        const u64 high = U32();
        return low | (high << 32);
    }
    int I32() { return (int)U32(); }

    // A length read out of the very bytes being checked is not to be
    // trusted with an allocation: it is compared against what actually
    // arrived before anything is reserved, so a made-up count cannot ask
    // for memory or read past the end.
    std::string Text()
    {
        const u32 length = U32();
        if (failed || length > Remaining()) { failed = true; return std::string(); }
        std::string value((const char*)(data + cursor), length);
        cursor += length;
        return value;
    }
    std::vector<u8> Bytes()
    {
        const u32 length = U32();
        if (failed || length > Remaining()) { failed = true; return std::vector<u8>(); }
        std::vector<u8> value(data + cursor, data + cursor + length);
        cursor += length;
        return value;
    }

    // How many entries a list may claim, given how many bytes each one
    // needs at minimum and how many bytes are actually left.
    u32 Count(size_t minBytesPerEntry)
    {
        const u32 count = U32();
        if (failed || (minBytesPerEntry > 0 && count > Remaining() / minBytesPerEntry)) { failed = true; return 0; }
        return count;
    }
};

// --- Reflection ---------------------------------------------------------

constexpr u32 kTypeKindCount = (u32)TypeKind::Unresolved + 1;
constexpr u32 kBindingGroupCount = (u32)BindingGroup::Object + 1;
constexpr u32 kShaderStageCount = (u32)ShaderStage::Compute + 1;

void WriteType(ByteWriter& w, const ShaderType& type)
{
    w.U32((u32)type.kind);
    w.Text(type.structName);
}

ShaderType ReadType(ByteReader& r)
{
    const u32 kind = r.U32();
    if (kind >= kTypeKindCount) { r.failed = true; return ShaderType{}; }
    ShaderType type;
    type.kind = (TypeKind)kind;
    type.structName = r.Text();
    return type;
}

void WriteResource(ByteWriter& w, const IRResourceBinding& binding)
{
    w.Text(binding.name);
    WriteType(w, binding.type);
    w.U32((u32)binding.group);
    w.I32(binding.binding);
    w.I32(binding.samplerBinding);
}

IRResourceBinding ReadResource(ByteReader& r)
{
    IRResourceBinding binding;
    binding.name = r.Text();
    binding.type = ReadType(r);
    const u32 group = r.U32();
    if (group >= kBindingGroupCount) { r.failed = true; return binding; }
    binding.group = (BindingGroup)group;
    binding.binding = r.I32();
    binding.samplerBinding = r.I32();
    return binding;
}

void WriteReflection(ByteWriter& w, const ShaderIRModule& module)
{
    w.U32((u32)module.stage);
    w.Text(module.entryPoint);

    w.U32((u32)module.inputs.size());
    for (const IRStageIOField& field : module.inputs) { w.Text(field.name); WriteType(w, field.type); w.I32(field.location); }

    w.U32((u32)module.outputs.size());
    for (const IRStageIOField& field : module.outputs) { w.Text(field.name); WriteType(w, field.type); w.I32(field.location); }

    w.U32((u32)module.outputSlots.size());
    for (const IROutputSlot& slot : module.outputSlots) { w.Text(slot.name); WriteType(w, slot.type); w.I32(slot.slot); }

    w.U32((u32)module.resources.size());
    for (const IRResourceBinding& binding : module.resources) WriteResource(w, binding);

    w.U32((u32)module.uniformBuffers.size());
    for (const IRUniformBufferBinding& buffer : module.uniformBuffers)
    {
        w.U32((u32)buffer.group);
        w.U32(buffer.size);
        w.U32((u32)buffer.members.size());
        for (const IRUniformBufferMember& member : buffer.members)
        {
            w.Text(member.name);
            WriteType(w, member.type);
            w.U32(member.offset);
        }
    }

    w.U32((u32)module.storageBuffers.size());
    for (const IRResourceBinding& binding : module.storageBuffers) WriteResource(w, binding);

    // An absent return target and an empty one are different things, and
    // writing only the text would make them the same.
    w.U8(module.returnTarget.has_value() ? 1u : 0u);
    if (module.returnTarget.has_value()) w.Text(*module.returnTarget);

    w.U32(module.localSizeX);
}

bool ReadReflection(ByteReader& r, ShaderIRModule& outModule)
{
    const u32 stage = r.U32();
    if (stage >= kShaderStageCount) return false;
    outModule.stage = (ShaderStage)stage;
    outModule.entryPoint = r.Text();

    // A field is a string plus a type plus a number: nothing shorter than
    // this can be one, which is what bounds the claimed count.
    constexpr size_t kMinBytesStageIOField = 4 + 4 + 4 + 4;
    constexpr size_t kMinBytesResource = 4 + 4 + 4 + 4 + 4 + 4;
    constexpr size_t kMinBytesUniformBuffer = 4 + 4 + 4;
    constexpr size_t kMinBytesUniformMember = 4 + 4 + 4 + 4;

    u32 count = r.Count(kMinBytesStageIOField);
    outModule.inputs.clear();
    for (u32 i = 0; i < count && !r.failed; ++i)
    {
        IRStageIOField field;
        field.name = r.Text();
        field.type = ReadType(r);
        field.location = r.I32();
        outModule.inputs.push_back(std::move(field));
    }

    count = r.Count(kMinBytesStageIOField);
    outModule.outputs.clear();
    for (u32 i = 0; i < count && !r.failed; ++i)
    {
        IRStageIOField field;
        field.name = r.Text();
        field.type = ReadType(r);
        field.location = r.I32();
        outModule.outputs.push_back(std::move(field));
    }

    count = r.Count(kMinBytesStageIOField);
    outModule.outputSlots.clear();
    for (u32 i = 0; i < count && !r.failed; ++i)
    {
        IROutputSlot slot;
        slot.name = r.Text();
        slot.type = ReadType(r);
        slot.slot = r.I32();
        outModule.outputSlots.push_back(std::move(slot));
    }

    count = r.Count(kMinBytesResource);
    outModule.resources.clear();
    for (u32 i = 0; i < count && !r.failed; ++i) outModule.resources.push_back(ReadResource(r));

    count = r.Count(kMinBytesUniformBuffer);
    outModule.uniformBuffers.clear();
    for (u32 i = 0; i < count && !r.failed; ++i)
    {
        IRUniformBufferBinding buffer;
        const u32 group = r.U32();
        if (group >= kBindingGroupCount) { r.failed = true; break; }
        buffer.group = (BindingGroup)group;
        buffer.size = r.U32();

        const u32 memberCount = r.Count(kMinBytesUniformMember);
        for (u32 m = 0; m < memberCount && !r.failed; ++m)
        {
            IRUniformBufferMember member;
            member.name = r.Text();
            member.type = ReadType(r);
            member.offset = r.U32();
            buffer.members.push_back(std::move(member));
        }
        outModule.uniformBuffers.push_back(std::move(buffer));
    }

    count = r.Count(kMinBytesResource);
    outModule.storageBuffers.clear();
    for (u32 i = 0; i < count && !r.failed; ++i) outModule.storageBuffers.push_back(ReadResource(r));

    const u8 hasReturnTarget = r.U8();
    if (hasReturnTarget > 1) return false;
    outModule.returnTarget.reset();
    if (hasReturnTarget == 1) outModule.returnTarget = r.Text();

    outModule.localSizeX = r.U32();
    return !r.failed;
}

// --- The key ------------------------------------------------------------

void KeyText(ByteWriter& w, const std::string& value) { w.Text(value); }

// Everything that decides the answer goes in, and the file name stays out
// -- it reaches diagnostics and nothing else, so two sources compiled
// under different names produce the same bytes and should share an entry.
std::vector<u8> BuildKeyBytes(const std::string& source, const ArtifactRequest& request, const std::vector<ResolvedInclude>& includes)
{
    std::vector<u8> bytes;
    ByteWriter w{ bytes };

    w.U32(kCacheFormatVersion);
    w.U32(kShaderLanguageVersion);
    w.U32(kShaderCompilerVersion);

    // The tool that turns text into bytes lives outside this build; a
    // different one answers differently and nothing in here would say so.
    KeyText(w, DXCIdentity());

    w.U32((u32)request.target);
    w.U32((u32)request.compile.stage);
    KeyText(w, request.compile.entryPoint);
    KeyText(w, source);

    // What was read, in the order it was read. Two compilations of one
    // source only agree if every include agreed too.
    w.U32((u32)includes.size());
    for (const ResolvedInclude& include : includes)
    {
        KeyText(w, include.name);
        w.U64(include.contentHash);
    }

    // Settings that change the output. Kept here rather than assumed
    // constant, because every one of them is a caller's choice.
    w.U32((u32)request.compile.irOptions.maxUniformBufferBytesPerGroup);
    KeyText(w, request.compile.glslOptions.versionDirective);
    KeyText(w, request.spirv.shaderModel);
    KeyText(w, request.spirv.spirvTargetEnv);
    KeyText(w, request.dxil.shaderModel);

    return bytes;
}

std::string KeyFileName(const std::vector<u8>& keyBytes)
{
    const u64 hash = Fluxion_HashBytes64(keyBytes.data(), keyBytes.size());
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.shadercache", (unsigned long long)hash);
    return std::string(name);
}

bool ReadWholeFile(const std::filesystem::path& path, std::vector<u8>& outBytes)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    outBytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// Written under a different name and moved into place, so a run that
// stops halfway leaves nothing that looks finished.
bool WriteWholeFileAtomically(const std::filesystem::path& path, const std::vector<u8>& bytes)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    // The temporary carries the writing process's own id: two builds
    // caching the same shader at once would otherwise pick the same
    // temporary name and rename each other's half-written file into
    // place, which is precisely the outcome writing to a temporary is
    // supposed to prevent.
    std::filesystem::path temporary = path;
    temporary += ".writing.";
    temporary += std::to_string(FluxionShaderCache_CurrentProcessId());
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        if (!bytes.empty()) file.write((const char*)bytes.data(), (std::streamsize)bytes.size());

        // Checking the stream before flushing only reports what the
        // buffer accepted, not what reached the disk -- a full volume
        // surfaces at the flush and nowhere earlier. Without this, a
        // short file gets renamed over a good one and the failure is
        // never reported.
        file.flush();
        if (!file) return false;
    }

    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

// A hit only counts when the whole key matches, not merely the name the
// key hashed to. Two different compilations can land on one file name --
// rarely, but a cache that trusted the name would then hand back the
// wrong shader, and nothing would say so.
bool TryLoad(const std::filesystem::path& path, const std::vector<u8>& keyBytes, CompiledArtifact& outArtifact)
{
    std::vector<u8> fileBytes;
    if (!ReadWholeFile(path, fileBytes)) return false;

    ByteReader r{ fileBytes.data(), fileBytes.size() };
    for (char expected : kMagic)
    {
        if (r.U8() != (u8)expected) return false;
    }
    if (r.failed || r.U32() != kCacheFormatVersion) return false;

    const std::vector<u8> storedKey = r.Bytes();
    if (r.failed || storedKey != keyBytes) return false;

    if (!ReadReflection(r, outArtifact.reflection)) return false;
    outArtifact.bytes = r.Bytes();
    if (r.failed) return false;

    // Bytes left over mean this is not the file it claims to be.
    return r.Remaining() == 0;
}

void Store(const std::filesystem::path& path, const std::vector<u8>& keyBytes, const CompiledArtifact& artifact, ShaderCacheReport& outReport)
{
    std::vector<u8> bytes;
    ByteWriter w{ bytes };
    for (char c : kMagic) w.U8((u8)c);
    w.U32(kCacheFormatVersion);
    w.Bytes(keyBytes);
    WriteReflection(w, artifact.reflection);
    w.Bytes(artifact.bytes);

    outReport.wrote = WriteWholeFileAtomically(path, bytes);
}

} // namespace

Fluxion::Foundation::Result<CompiledArtifact> CompileArtifactCached(const std::string& source, const ArtifactRequest& request,
    const ShaderCacheOptions& cache, DiagnosticList& outDiagnostics, ShaderCacheReport& outReport)
{
    outReport = ShaderCacheReport{};

    // The front end has to run before the key is complete, because the
    // key names every include and only the front end knows which were
    // read. That is the smaller half of the work; the expensive half is
    // what comes after, and that is what a hit skips.
    auto compiled = Compile(source, request.compile, outDiagnostics);
    if (!compiled.IsOk()) return Fluxion::Foundation::Result<CompiledArtifact>::Error(1, "shader compilation failed");

    const std::vector<u8> keyBytes = BuildKeyBytes(source, request, compiled.Value().includes);

    std::filesystem::path path;
    if (!cache.directory.empty())
    {
        path = std::filesystem::path(cache.directory) / KeyFileName(keyBytes);
        outReport.path = path.string();

        CompiledArtifact loaded;
        if (TryLoad(path, keyBytes, loaded))
        {
            outReport.wasCached = true;
            s_loaded.fetch_add(1, std::memory_order_relaxed);
            return Fluxion::Foundation::Result<CompiledArtifact>::Ok(std::move(loaded));
        }
    }

    CompiledArtifact artifact;
    artifact.reflection = compiled.Value().reflection;

    if (request.target == ArtifactTarget::Glsl)
    {
        const std::string& glsl = compiled.Value().glslSource;
        artifact.bytes.assign(glsl.begin(), glsl.end());
    }
    else
    {
        // The entry point is always literally `main` here: the HLSL
        // backend emits a function by that name whatever the source
        // called its own.
        auto bytes = (request.target == ArtifactTarget::Dxil)
            ? CompileToDxil(compiled.Value().hlslSource, request.compile.stage, "main", outDiagnostics, request.dxil)
            : CompileToSpirv(compiled.Value().hlslSource, request.compile.stage, "main", outDiagnostics, request.spirv);
        if (!bytes.IsOk()) return Fluxion::Foundation::Result<CompiledArtifact>::Error(2, "shader artifact generation failed");
        artifact.bytes = std::move(bytes.Value());
    }

    s_compiled.fetch_add(1, std::memory_order_relaxed);

    if (!cache.directory.empty() && !cache.readOnly) Store(path, keyBytes, artifact, outReport);

    return Fluxion::Foundation::Result<CompiledArtifact>::Ok(std::move(artifact));
}

ShaderCacheCounters GetShaderCacheCounters()
{
    ShaderCacheCounters counters;
    counters.compiled = s_compiled.load(std::memory_order_relaxed);
    counters.loaded = s_loaded.load(std::memory_order_relaxed);
    return counters;
}

} // namespace Fluxion::ShaderCompiler
