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

#include <Fluxion/Script/Runtime/CompileCache.hpp>

#include <Compiler/Prelude.hpp>

#include <Fluxion/Script/Runtime/ModuleSerializer.hpp>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Fluxion::Script
{

namespace
{

// Bumped when the arrangement of a cache file changes -- which is not the
// same thing as the module inside it changing. A file written by a build
// that laid it out differently is a miss, exactly as a stale one is.
constexpr u32 kCacheFormatVersion = 1;

constexpr u8 kCacheMagic[4] = { 'F', 'L', 'X', 'C' };

// The key is the whole of what went into the compilation, run through a
// hash. Two compilations that would produce different images must not
// produce the same key, so everything that can change the answer is fed in
// -- and, since a hash cannot rule collisions out on its own, the key is
// also written inside the file and compared against on the way back in.
constexpr u64 kHashOffset = 1469598103934665603ull;
constexpr u64 kHashPrime = 1099511628211ull;

class KeyBuilder
{
public:
    void Byte(u8 value)
    {
        m_hash ^= (u64)value;
        m_hash *= kHashPrime;
    }

    void Number(u64 value)
    {
        for (int i = 0; i < 8; ++i) Byte((u8)((value >> (i * 8)) & 0xFFull));
    }

    // Length-prefixed, so two adjacent pieces of text cannot be run
    // together into the same sequence of bytes a different pair would
    // produce.
    void Text(const std::string& value)
    {
        Number((u64)value.size());
        for (char character : value) Byte((u8)character);
    }

    void Text(const char* value) { Text(value != nullptr ? std::string(value) : std::string()); }

    u64 Value() const { return m_hash; }

private:
    u64 m_hash = kHashOffset;
};

// What the engine offered the compilation, in the terms the compilation
// saw it: names, shapes and nothing else. The invokers a table carries are
// addresses in this run and mean nothing in the next, so they are left
// out -- what matters is that a module built against a set of engine types
// is only ever answered back to a compilation offering that same set.
void HashBindings(KeyBuilder& key, const BindingTable* bindings)
{
    key.Number(bindings != nullptr ? (u64)bindings->types.size() : 0ull);
    if (!bindings) return;

    for (const BoundType& type : bindings->types)
    {
        key.Text(type.name);
        key.Number((u64)type.methods.size());

        for (const BoundMethod& method : type.methods)
        {
            key.Text(method.name);
            key.Byte(method.isInstance ? 1u : 0u);
            key.Byte(method.takesScriptType ? 1u : 0u);
            key.Byte((u8)method.returnType);
            key.Number((u64)method.returnBoundType);

            key.Number((u64)method.parameterTypes.size());
            for (ValueType parameter : method.parameterTypes) key.Byte((u8)parameter);
            for (u32 bound : method.parameterBoundTypes) key.Number((u64)bound);
            for (const std::string& set : method.parameterConstantSets) key.Text(set);
        }
    }
}

u64 ComputeKey(const std::string& source, const CompileOptions& options)
{
    KeyBuilder key;

    key.Number(kCacheFormatVersion);
    key.Number(kLanguageVersion);
    key.Number(kBytecodeVersion);
    key.Number(kEngineAbiVersion);

    // The name and the revision are part of the answer, not just of the
    // request: both are stamped into the image, and every method carries
    // the file its body was written in.
    key.Text(options.fileName);
    key.Number(options.moduleVersion);

    key.Text(PreludeSource());
    key.Text(options.hostPreludeName);
    key.Text(options.hostPrelude);
    key.Text(source);

    HashBindings(key, options.bindings);
    return key.Value();
}

std::string KeyFileName(u64 key)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.flsmod", (unsigned long long)key);
    return std::string(name);
}

// Counted rather than timed: a run that is quicker than the last one is
// not evidence of anything, and these are.
//
// Atomic because compiling is something a caller is free to do from more
// than one thread, and a plain ++ from two of them loses counts -- which
// would show up as a cache that looks slightly less effective than it is,
// with nothing to suggest the number itself is wrong.
std::atomic<u64> g_compiled{ 0 };
std::atomic<u64> g_loaded{ 0 };

bool ReadWholeFile(const std::filesystem::path& path, std::vector<u8>& outBytes)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return false;

    // A file too large to be a module of this build's making is not read
    // at all: whatever it is, it is not what was left here.
    constexpr std::uintmax_t kLargestSensibleImage = 256ull * 1024ull * 1024ull;
    if (size == 0 || size > kLargestSensibleImage) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    outBytes.resize((size_t)size);
    file.read((char*)outBytes.data(), (std::streamsize)size);
    return (size_t)file.gcount() == outBytes.size();
}

// Written beside the entry and moved into place, so a run interrupted
// partway leaves a stray file rather than an entry that is half of one
// image and half of another. A reader that meets the stray one refuses it
// like any other file it cannot account for.
bool WriteWholeFile(const std::filesystem::path& path, const std::vector<u8>& bytes)
{
    // The staging name carries the writing process's own id. Two builds
    // caching the same module at once would otherwise choose the same
    // staging path and rename each other's half-written file into place,
    // which is the exact outcome staging exists to prevent.
    std::filesystem::path staging = path;
    staging += ".writing.";
#if defined(_WIN32)
    staging += std::to_string((unsigned long)_getpid());
#else
    staging += std::to_string((unsigned long)getpid());
#endif

    {
        std::ofstream file(staging, std::ios::binary | std::ios::trunc);
        if (!file) return false;

        file.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        file.flush();
        if (!file) return false;
    }

    std::error_code error;
    std::filesystem::rename(staging, path, error);
    if (!error) return true;

    std::filesystem::remove(staging, error);
    return false;
}

bool TryLoad(const std::filesystem::path& path, u64 key, CompiledModule& outModule)
{
    std::vector<u8> bytes;
    if (!ReadWholeFile(path, bytes)) return false;

    constexpr size_t kPrefix = sizeof(kCacheMagic) + 4 + 8;
    if (bytes.size() <= kPrefix) return false;

    for (size_t i = 0; i < sizeof(kCacheMagic); ++i)
    {
        if (bytes[i] != kCacheMagic[i]) return false;
    }

    u32 format = 0;
    for (int i = 0; i < 4; ++i) format |= (u32)bytes[sizeof(kCacheMagic) + (size_t)i] << (i * 8);
    if (format != kCacheFormatVersion) return false;

    u64 stored = 0;
    for (int i = 0; i < 8; ++i) stored |= (u64)bytes[sizeof(kCacheMagic) + 4 + (size_t)i] << (i * 8);
    if (stored != key) return false;

    // Whatever the reader makes of the rest is its business, and anything
    // it refuses is a miss here. The diagnostics it produces are dropped:
    // the caller asked for a module, not for an account of a file it never
    // knew existed.
    DiagnosticList ignored;
    return ReadModule(bytes.data() + kPrefix, bytes.size() - kPrefix, outModule, ignored);
}

void TryStore(const std::filesystem::path& path, u64 key, const CompiledModule& module, CompileCacheReport& outReport)
{
    std::vector<u8> image;
    if (!WriteModule(module, image)) return;

    std::vector<u8> bytes;
    bytes.reserve(image.size() + sizeof(kCacheMagic) + 12);
    for (u8 byte : kCacheMagic) bytes.push_back(byte);
    for (int i = 0; i < 4; ++i) bytes.push_back((u8)((kCacheFormatVersion >> (i * 8)) & 0xFFu));
    for (int i = 0; i < 8; ++i) bytes.push_back((u8)((key >> (i * 8)) & 0xFFull));
    bytes.insert(bytes.end(), image.begin(), image.end());

    outReport.wrote = WriteWholeFile(path, bytes);
}

} // namespace

Fluxion::Foundation::Result<CompiledModule> CompileCached(const std::string& source, const CompileOptions& options,
    const CompileCacheOptions& cache, DiagnosticList& outDiagnostics, CompileCacheReport& outReport)
{
    using ResultType = Fluxion::Foundation::Result<CompiledModule>;

    outReport = CompileCacheReport{};

    std::filesystem::path path;
    u64 key = 0;
    const bool usable = !cache.directory.empty();
    if (usable)
    {
        key = ComputeKey(source, options);
        path = std::filesystem::path(cache.directory) / KeyFileName(key);
        outReport.path = path.string();

        CompiledModule loaded;
        if (TryLoad(path, key, loaded))
        {
            g_loaded.fetch_add(1, std::memory_order_relaxed);
            outReport.wasCached = true;
            return ResultType::Ok(std::move(loaded));
        }
    }

    auto compiled = Compile(source, options, outDiagnostics);
    g_compiled.fetch_add(1, std::memory_order_relaxed);
    if (!compiled.IsOk()) return ResultType::Error(compiled.Status().code, compiled.Status().message);

    if (usable && !cache.readOnly)
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(cache.directory), error);

        // A directory that could not be made means there is nowhere to put
        // this, which changes nothing about the module that was just
        // produced.
        if (!error) TryStore(path, key, compiled.Value(), outReport);
    }

    return ResultType::Ok(std::move(compiled.Value()));
}

CompileCacheCounters GetCompileCacheCounters()
{
    CompileCacheCounters snapshot;
    snapshot.compiled = g_compiled.load(std::memory_order_relaxed);
    snapshot.loaded = g_loaded.load(std::memory_order_relaxed);
    return snapshot;
}

} // namespace Fluxion::Script
