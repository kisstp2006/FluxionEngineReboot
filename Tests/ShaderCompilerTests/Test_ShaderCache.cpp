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

#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>
#include <Fluxion/ShaderCompiler/ShaderCache.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace Fluxion::ShaderCompiler;

namespace
{

// Enough shape to make the reflection worth writing down: stage inputs, a
// declared output slot, a texture, and a uniform group with more than one
// member in it. A round trip over something with one field in it proves
// almost nothing.
const char* const kSource =
    "[Input] Vector2 vUV;\n"
    "[Input] Vector3 vNormal;\n"
    "\n"
    "[Texture(Material)] Texture2D albedoMap;\n"
    "[Uniform(Material)] Vector3 tint;\n"
    "[Uniform(Material)] float strength;\n"
    "\n"
    "[Target(0)] Vector4 fragColor;\n"
    "\n"
    "void main() {\n"
    "  return texture(albedoMap, vUV) * Vector4(tint * strength * vNormal.y, 1.0);\n"
    "}\n";

std::filesystem::path MakeDirectory(const char* name)
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error) return std::filesystem::path();

    const std::filesystem::path directory = root / "FluxionShaderCacheTests" / name;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return error ? std::filesystem::path() : directory;
}

ArtifactRequest MakeRequest(ArtifactTarget target)
{
    ArtifactRequest request;
    request.target = target;
    request.compile.stage = ShaderStage::Fragment;
    request.compile.fileName = "cache-subject.jsl";
    return request;
}

bool SameType(const ShaderType& a, const ShaderType& b)
{
    return a.kind == b.kind && a.structName == b.structName;
}

bool SameReflection(const ShaderIRModule& a, const ShaderIRModule& b)
{
    if (a.stage != b.stage || a.entryPoint != b.entryPoint) return false;
    if (a.localSizeX != b.localSizeX) return false;
    if (a.returnTarget.has_value() != b.returnTarget.has_value()) return false;
    if (a.returnTarget.has_value() && *a.returnTarget != *b.returnTarget) return false;

    if (a.inputs.size() != b.inputs.size() || a.outputs.size() != b.outputs.size()) return false;
    if (a.outputSlots.size() != b.outputSlots.size()) return false;
    if (a.resources.size() != b.resources.size() || a.storageBuffers.size() != b.storageBuffers.size()) return false;
    if (a.uniformBuffers.size() != b.uniformBuffers.size()) return false;

    for (size_t i = 0; i < a.inputs.size(); ++i)
        if (a.inputs[i].name != b.inputs[i].name || !SameType(a.inputs[i].type, b.inputs[i].type) || a.inputs[i].location != b.inputs[i].location) return false;
    for (size_t i = 0; i < a.outputs.size(); ++i)
        if (a.outputs[i].name != b.outputs[i].name || !SameType(a.outputs[i].type, b.outputs[i].type) || a.outputs[i].location != b.outputs[i].location) return false;
    for (size_t i = 0; i < a.outputSlots.size(); ++i)
        if (a.outputSlots[i].name != b.outputSlots[i].name || !SameType(a.outputSlots[i].type, b.outputSlots[i].type) || a.outputSlots[i].slot != b.outputSlots[i].slot) return false;

    auto sameResources = [](const std::vector<IRResourceBinding>& x, const std::vector<IRResourceBinding>& y)
    {
        for (size_t i = 0; i < x.size(); ++i)
        {
            if (x[i].name != y[i].name || x[i].group != y[i].group) return false;
            if (x[i].binding != y[i].binding || x[i].samplerBinding != y[i].samplerBinding) return false;
            if (!SameType(x[i].type, y[i].type)) return false;
        }
        return true;
    };
    if (!sameResources(a.resources, b.resources)) return false;
    if (!sameResources(a.storageBuffers, b.storageBuffers)) return false;

    for (size_t i = 0; i < a.uniformBuffers.size(); ++i)
    {
        const IRUniformBufferBinding& x = a.uniformBuffers[i];
        const IRUniformBufferBinding& y = b.uniformBuffers[i];
        if (x.group != y.group || x.size != y.size || x.members.size() != y.members.size()) return false;
        for (size_t m = 0; m < x.members.size(); ++m)
            if (x.members[m].name != y.members[m].name || x.members[m].offset != y.members[m].offset || !SameType(x.members[m].type, y.members[m].type)) return false;
    }
    return true;
}

} // namespace

void Test_ShaderCache_Run(TestContext& ctx)
{
    // GLSL is produced by this build itself and needs no external tool, so
    // every check below uses it. The paths that do need one are the same
    // code with different bytes at the end, and a machine without the tool
    // would otherwise skip the whole suite.
    {
        // The second time answers out of the file, and answers the same
        // thing. Counted rather than timed: a run being quicker proves
        // nothing on a machine doing anything else at the same time.
        const std::filesystem::path directory = MakeDirectory("hit");
        TEST_CHECK(ctx, !directory.empty());
        if (!directory.empty())
        {
            ShaderCacheOptions cache;
            cache.directory = directory.string();

            DiagnosticList firstDiagnostics;
            ShaderCacheReport firstReport;
            const ShaderCacheCounters before = GetShaderCacheCounters();
            auto first = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), cache, firstDiagnostics, firstReport);
            TEST_CHECK(ctx, first.IsOk());
            TEST_CHECK(ctx, !firstReport.wasCached);
            TEST_CHECK(ctx, firstReport.wrote);
            TEST_CHECK(ctx, GetShaderCacheCounters().compiled == before.compiled + 1);

            DiagnosticList secondDiagnostics;
            ShaderCacheReport secondReport;
            const ShaderCacheCounters middle = GetShaderCacheCounters();
            auto second = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), cache, secondDiagnostics, secondReport);
            TEST_CHECK(ctx, second.IsOk());
            TEST_CHECK(ctx, secondReport.wasCached);
            TEST_CHECK(ctx, !secondReport.wrote);
            TEST_CHECK(ctx, GetShaderCacheCounters().compiled == middle.compiled);
            TEST_CHECK(ctx, GetShaderCacheCounters().loaded == middle.loaded + 1);

            if (first.IsOk() && second.IsOk())
            {
                // Byte-for-byte, and reflection field for reflection
                // field. A cache that returned something merely similar
                // would be worse than no cache at all.
                TEST_CHECK(ctx, first.Value().bytes == second.Value().bytes);
                TEST_CHECK(ctx, !first.Value().bytes.empty());
                TEST_CHECK(ctx, SameReflection(first.Value().reflection, second.Value().reflection));
            }
        }
    }
    {
        // Everything the key is made of, one at a time. Each of these
        // changes the answer, so each has to miss -- and a key that
        // forgot any one of them would quietly hand back the other's
        // result.
        const std::filesystem::path directory = MakeDirectory("key");
        TEST_CHECK(ctx, !directory.empty());
        if (!directory.empty())
        {
            ShaderCacheOptions cache;
            cache.directory = directory.string();

            DiagnosticList diagnostics;
            ShaderCacheReport report;
            CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), cache, diagnostics, report);

            // Every one of these has to have compiled as well as missed.
            // A variant that failed to compile would also report "not
            // cached", and the check would pass for the wrong reason --
            // which is how a key test ends up proving nothing.
            // Named apart from the outer pair on purpose: this lambda
            // must report on its own compilation, and a name that
            // shadowed the outer one would still compile if the capture
            // were ever changed to take them by reference instead.
            auto missedAndCompiled = [&ctx, &cache](const std::string& text, const ArtifactRequest& request)
            {
                DiagnosticList variantDiagnostics;
                ShaderCacheReport variantReport;
                auto result = CompileArtifactCached(text, request, cache, variantDiagnostics, variantReport);
                if (!result.IsOk())
                {
                    for (const Diagnostic& d : variantDiagnostics.entries)
                        std::fprintf(stderr, "  %s:%u: %s\n", d.location.file.c_str(), d.location.line, d.message.c_str());
                }
                TEST_CHECK(ctx, result.IsOk());
                TEST_CHECK(ctx, !variantReport.wasCached);
            };

            // A different source.
            missedAndCompiled("float scale() { return 2.0; }\n" + std::string(kSource), MakeRequest(ArtifactTarget::Glsl));

            // A different GLSL version directive changes the text that
            // comes out, so it cannot share an entry.
            ArtifactRequest otherVersion = MakeRequest(ArtifactTarget::Glsl);
            otherVersion.compile.glslOptions.versionDirective = "460 core";
            missedAndCompiled(kSource, otherVersion);

            // A different uniform-buffer budget changes how bindings are
            // laid out, which the reflection carries.
            //
            // The number has to be one the default is NOT, or this checks
            // nothing: it has now been overtaken twice -- it said 256 and
            // the default became 256, then it said 512 and the default
            // became 512. Both times this failed, correctly, which is the
            // only reason either was noticed. So it is a number no budget
            // would be chosen for: not a power of two, and not round.
            ArtifactRequest otherBudget = MakeRequest(ArtifactTarget::Glsl);
            otherBudget.compile.irOptions.maxUniformBufferBytesPerGroup = 336;
            missedAndCompiled(kSource, otherBudget);

            // The same source under a different name: the name reaches
            // diagnostics and nothing else, so this one has to HIT --
            // keying on it would split entries that are the same work.
            ArtifactRequest renamed = MakeRequest(ArtifactTarget::Glsl);
            renamed.compile.fileName = "some-other-name.jsl";
            ShaderCacheReport renamedReport;
            DiagnosticList renamedDiagnostics;
            CompileArtifactCached(kSource, renamed, cache, renamedDiagnostics, renamedReport);
            TEST_CHECK(ctx, renamedReport.wasCached);
        }
    }
    {
        // Asking for a different shape must never be answered with the
        // shape already on disk. This is the one that is not merely a
        // wasted recompile if it goes wrong: bytes meant for one device
        // handed to another are not a wrong answer, they are a crash or
        // worse, and nothing would report it.
        const std::filesystem::path directory = MakeDirectory("target");
        TEST_CHECK(ctx, !directory.empty());
        if (!directory.empty() && IsDXCAvailable())
        {
            ShaderCacheOptions cache;
            cache.directory = directory.string();

            DiagnosticList glslDiagnostics;
            ShaderCacheReport glslReport;
            auto glsl = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), cache, glslDiagnostics, glslReport);
            TEST_CHECK(ctx, glsl.IsOk());

            DiagnosticList spirvDiagnostics;
            ShaderCacheReport spirvReport;
            auto spirv = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Spirv), cache, spirvDiagnostics, spirvReport);
            TEST_CHECK(ctx, !spirvReport.wasCached);
            TEST_CHECK(ctx, spirv.IsOk());

            if (glsl.IsOk() && spirv.IsOk())
            {
                TEST_CHECK(ctx, glsl.Value().bytes != spirv.Value().bytes);

                // SPIR-V starts with a word every SPIR-V module starts
                // with; GLSL is text. Checking the shape rather than just
                // "not equal" catches a swap that happened to differ.
                const std::vector<u8>& bytes = spirv.Value().bytes;
                TEST_CHECK(ctx, bytes.size() >= 4);
                if (bytes.size() >= 4)
                {
                    const bool littleEndianMagic = bytes[0] == 0x03 && bytes[1] == 0x02 && bytes[2] == 0x23 && bytes[3] == 0x07;
                    TEST_CHECK(ctx, littleEndianMagic);
                }
            }
        }
    }
    {
        // A file is whatever happens to be at that path: a half-finished
        // write, a truncated copy, another program's output. None of it
        // is an error -- the compiler runs and the answer replaces it.
        const std::filesystem::path directory = MakeDirectory("damaged");
        TEST_CHECK(ctx, !directory.empty());
        if (!directory.empty())
        {
            ShaderCacheOptions cache;
            cache.directory = directory.string();

            DiagnosticList diagnostics;
            ShaderCacheReport report;
            auto original = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), cache, diagnostics, report);
            TEST_CHECK(ctx, original.IsOk());
            TEST_CHECK(ctx, !report.path.empty());

            std::vector<u8> good;
            {
                std::ifstream file(report.path, std::ios::binary);
                good.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
            TEST_CHECK(ctx, !good.empty());

            // Every length short of the whole thing.
            bool everyTruncationMissed = true;
            for (size_t length = 0; length < good.size() && everyTruncationMissed; length += 1 + good.size() / 64)
            {
                {
                    std::ofstream file(report.path, std::ios::binary | std::ios::trunc);
                    if (length > 0) file.write((const char*)good.data(), (std::streamsize)length);
                }
                DiagnosticList truncatedDiagnostics;
                ShaderCacheReport truncatedReport;
                auto result = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), cache, truncatedDiagnostics, truncatedReport);
                if (!result.IsOk() || truncatedReport.wasCached) everyTruncationMissed = false;
            }
            TEST_CHECK(ctx, everyTruncationMissed);

            // Bytes that were never this at all.
            {
                std::ofstream file(report.path, std::ios::binary | std::ios::trunc);
                const char junk[] = "this is not a shader cache entry, it is a sentence";
                file.write(junk, (std::streamsize)sizeof(junk));
            }
            DiagnosticList junkDiagnostics;
            ShaderCacheReport junkReport;
            auto afterJunk = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), cache, junkDiagnostics, junkReport);
            TEST_CHECK(ctx, afterJunk.IsOk());
            TEST_CHECK(ctx, !junkReport.wasCached);

            // Having recompiled over it, the entry works again.
            DiagnosticList healedDiagnostics;
            ShaderCacheReport healedReport;
            auto healed = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), cache, healedDiagnostics, healedReport);
            TEST_CHECK(ctx, healed.IsOk());
            TEST_CHECK(ctx, healedReport.wasCached);
        }
    }
    {
        // No directory means no cache, and no complaint about it either.
        DiagnosticList diagnostics;
        ShaderCacheReport report;
        ShaderCacheOptions off;
        auto result = CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), off, diagnostics, report);
        TEST_CHECK(ctx, result.IsOk());
        TEST_CHECK(ctx, !report.wasCached);
        TEST_CHECK(ctx, !report.wrote);
        TEST_CHECK(ctx, report.path.empty());
    }
    {
        // Read-only: a hit still answers, a miss still compiles, and
        // nothing new appears on disk.
        const std::filesystem::path directory = MakeDirectory("readonly");
        TEST_CHECK(ctx, !directory.empty());
        if (!directory.empty())
        {
            ShaderCacheOptions writable;
            writable.directory = directory.string();

            ShaderCacheOptions readOnly = writable;
            readOnly.readOnly = true;

            DiagnosticList firstDiagnostics;
            ShaderCacheReport firstReport;
            CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), readOnly, firstDiagnostics, firstReport);
            TEST_CHECK(ctx, !firstReport.wasCached);
            TEST_CHECK(ctx, !firstReport.wrote);

            std::error_code error;
            TEST_CHECK(ctx, !std::filesystem::exists(firstReport.path, error));

            DiagnosticList seedDiagnostics;
            ShaderCacheReport seedReport;
            CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), writable, seedDiagnostics, seedReport);
            TEST_CHECK(ctx, seedReport.wrote);

            DiagnosticList hitDiagnostics;
            ShaderCacheReport hitReport;
            CompileArtifactCached(kSource, MakeRequest(ArtifactTarget::Glsl), readOnly, hitDiagnostics, hitReport);
            TEST_CHECK(ctx, hitReport.wasCached);
        }
    }
}
