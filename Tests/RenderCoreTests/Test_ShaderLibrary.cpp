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

#include <Fluxion/ShaderCompiler/ShaderCache.hpp>
#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

// The engine's own shader library, and the resolver that hands it to a
// compilation.
//
// Declared here rather than included: the header lives in RenderCore's
// private directory, which a test does not see. Declaring them is also
// the point -- if the generated table were hidden or the names were
// mangled differently than expected, this file would not link, which is
// exactly the failure worth catching.
namespace Fluxion::RenderCore
{
const char* FindShaderLibraryFile(const char* name);
unsigned int GetShaderLibraryFileCount();
const char* GetShaderLibraryFileNameAt(unsigned int index);
Fluxion::ShaderCompiler::IncludeResolver MakeShaderLibraryResolver();
} // namespace Fluxion::RenderCore

namespace
{

using namespace Fluxion::ShaderCompiler;
using Fluxion::RenderCore::FindShaderLibraryFile;
using Fluxion::RenderCore::GetShaderLibraryFileCount;
using Fluxion::RenderCore::GetShaderLibraryFileNameAt;
using Fluxion::RenderCore::MakeShaderLibraryResolver;

CompileOptions FragmentOptions()
{
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<Test_ShaderLibrary>";
    return options;
}

bool Mentions(const DiagnosticList& diagnostics, const char* text)
{
    for (const Diagnostic& d : diagnostics.entries)
    {
        if (d.message.find(text) != std::string::npos) return true;
    }
    return false;
}

// A shader that uses something out of every part of the library, so a
// library file that is present but empty does not pass.
const char* const kUsesTheLibrary = R"(
#include "Fluxion/Math.jsl"

[Input] Vector2 vUV;
[Target(0)] Vector4 fragColor;

void main() {
  float lit = Saturate(vUV.x) * FLUXION_INV_PI;
  float rim = Pow5(1.0 - Saturate(vUV.y));
  float sq = Square(vUV.x);
  return Vector4(lit, rim, sq, FLUXION_PI * FLUXION_EPSILON);
}
)";

void TheLibraryIsBuiltIn(TestContext* ctx)
{
    // A generator that produced an empty table would leave every include
    // failing, and the first symptom would be a shader error about
    // something else entirely.
    TEST_CHECK(ctx, GetShaderLibraryFileCount() > 0);

    bool foundMath = false;
    for (unsigned int i = 0; i < GetShaderLibraryFileCount(); ++i)
    {
        const char* name = GetShaderLibraryFileNameAt(i);
        TEST_CHECK(ctx, name != nullptr && name[0] != '\0');
        if (name && std::strcmp(name, "Fluxion/Math.jsl") == 0) foundMath = true;
    }
    TEST_CHECK(ctx, foundMath);

    // Asked the other way round too: what the listing names must also be
    // findable by that name.
    for (unsigned int i = 0; i < GetShaderLibraryFileCount(); ++i)
    {
        const char* text = FindShaderLibraryFile(GetShaderLibraryFileNameAt(i));
        TEST_CHECK(ctx, text != nullptr && text[0] != '\0');
    }

    TEST_CHECK(ctx, FindShaderLibraryFile("Fluxion/NotThere.jsl") == nullptr);
    TEST_CHECK(ctx, FindShaderLibraryFile("") == nullptr);
    TEST_CHECK(ctx, FindShaderLibraryFile(nullptr) == nullptr);

    // A prefix of a real name is not that name.
    TEST_CHECK(ctx, FindShaderLibraryFile("Fluxion/Math") == nullptr);
    TEST_CHECK(ctx, FindShaderLibraryFile("Fluxion/Math.jsl.extra") == nullptr);
}

void AShaderCanIncludeIt(TestContext* ctx)
{
    CompileOptions options = FragmentOptions();
    options.includeResolver = MakeShaderLibraryResolver();

    DiagnosticList diagnostics;
    auto result = Compile(kUsesTheLibrary, options, diagnostics);

    if (!result.IsOk())
    {
        for (const Diagnostic& d : diagnostics.entries)
        {
            std::fprintf(stderr, "    %s:%u: %s\n", d.location.file.c_str(), d.location.line, d.message.c_str());
        }
    }

    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    // The include really was read, and is recorded as read -- which is
    // what the shader cache later builds its key out of.
    bool recorded = false;
    for (const ResolvedInclude& include : result.Value().includes)
    {
        if (include.name == "Fluxion/Math.jsl")
        {
            recorded = true;
            TEST_CHECK(ctx, include.contentHash != 0);
        }
    }
    TEST_CHECK(ctx, recorded);

    // Saturate exists because the library defines it. It is the reason
    // the library exists at all: written directly at a call site it
    // compiles on one backend and fails on the other.
    TEST_CHECK(ctx, result.Value().glslSource.find("Saturate") != std::string::npos);
    TEST_CHECK(ctx, result.Value().hlslSource.find("Saturate") != std::string::npos);

    // The DECLARATION of each constant, not merely a mention of it.
    //
    // Searching for the bare name would pass on output that uses a
    // constant it never declares -- which is exactly what one of these
    // backends did until this check existed: the value came out fine
    // under one target and would not compile under the other.
    //
    // `static` matters on the second one. A global in that language
    // without it is not a constant at all but a member of a buffer
    // nothing fills in, so the value would read as zero at runtime
    // rather than as what the source said -- a wrong picture, not an
    // error message.
    TEST_CHECK(ctx, result.Value().glslSource.find("const float FLUXION_INV_PI") != std::string::npos);
    TEST_CHECK(ctx, result.Value().glslSource.find("const float FLUXION_PI") != std::string::npos);
    TEST_CHECK(ctx, result.Value().hlslSource.find("static const float FLUXION_INV_PI") != std::string::npos);
    TEST_CHECK(ctx, result.Value().hlslSource.find("static const float FLUXION_PI") != std::string::npos);

    // And the functions are declared before whatever calls them.
    const std::string& hlsl = result.Value().hlslSource;
    const std::size_t declaredAt = hlsl.find("float Saturate(");
    const std::size_t usedAt = hlsl.find("Saturate(vUV");
    TEST_CHECK(ctx, declaredAt != std::string::npos);
    TEST_CHECK(ctx, usedAt == std::string::npos || declaredAt < usedAt);
}

void WithoutTheResolverTheSameSourceFails(TestContext* ctx)
{
    // Proves the resolver is what makes the include work, and not some
    // other thing that would have let the shader compile anyway.
    CompileOptions options = FragmentOptions();

    DiagnosticList diagnostics;
    auto result = Compile(kUsesTheLibrary, options, diagnostics);

    TEST_CHECK(ctx, !result.IsOk());
    TEST_CHECK(ctx, Mentions(diagnostics, "Fluxion/Math.jsl"));
}

void AnUnknownIncludeIsRefusedAndNamed(TestContext* ctx)
{
    const char* const source = R"(
#include "Fluxion/NotThere.jsl"

[Target(0)] Vector4 fragColor;
void main() { return Vector4(0.0, 0.0, 0.0, 1.0); }
)";

    CompileOptions options = FragmentOptions();
    options.includeResolver = MakeShaderLibraryResolver();

    DiagnosticList diagnostics;
    auto result = Compile(source, options, diagnostics);

    TEST_CHECK(ctx, !result.IsOk());

    // Named, because "a shader failed to compile" without saying which
    // include went missing is a message that costs an afternoon.
    TEST_CHECK(ctx, Mentions(diagnostics, "Fluxion/NotThere.jsl"));
}

// The claim that matters: editing a library file cannot serve a result
// compiled against the old text.
//
// The cache runs the front end before it builds its key, and puts every
// include's name and content hash into that key. So the same source
// compiled against two different library texts lands in two different
// files. Checked here rather than trusted, because the failure it guards
// against -- a shader silently keeping old lighting maths after the
// library changed -- looks like nothing at all.
void TheCacheKeyFollowsTheLibraryText(TestContext* ctx)
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    const std::filesystem::path directory = root / "FluxionShaderLibraryTests";
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    const char* const source = R"(
#include "Library.jsl"

[Target(0)] Vector4 fragColor;
void main() { return Vector4(LibraryValue(), 0.0, 0.0, 1.0); }
)";

    // GLSL rather than SPIR-V or DXIL on purpose: it is produced by this
    // compiler alone, so this check runs on a machine with no external
    // shader tool installed.
    ArtifactRequest request;
    request.target = ArtifactTarget::Glsl;
    request.compile = FragmentOptions();

    ShaderCacheOptions cache;
    cache.directory = directory.string();

    auto resolverReturning = [](const char* text) {
        std::string copy = text;
        return [copy](const std::string& name, std::string& outContent) -> bool {
            if (name != "Library.jsl") return false;
            outContent = copy;
            return true;
        };
    };

    DiagnosticList firstDiagnostics;
    ShaderCacheReport firstReport;
    request.compile.includeResolver = resolverReturning("float LibraryValue() { return 1.0; }\n");
    auto first = CompileArtifactCached(source, request, cache, firstDiagnostics, firstReport);
    TEST_CHECK(ctx, first.IsOk());
    TEST_CHECK(ctx, !firstReport.wasCached);

    // The very same request again: this one must come out of the file.
    DiagnosticList againDiagnostics;
    ShaderCacheReport againReport;
    auto again = CompileArtifactCached(source, request, cache, againDiagnostics, againReport);
    TEST_CHECK(ctx, again.IsOk());
    TEST_CHECK(ctx, againReport.wasCached);
    TEST_CHECK(ctx, againReport.path == firstReport.path);

    // Now the library says something else, and the source has not
    // changed one character.
    DiagnosticList secondDiagnostics;
    ShaderCacheReport secondReport;
    request.compile.includeResolver = resolverReturning("float LibraryValue() { return 0.25; }\n");
    auto second = CompileArtifactCached(source, request, cache, secondDiagnostics, secondReport);
    TEST_CHECK(ctx, second.IsOk());

    // A different file, and freshly compiled -- not the old one handed
    // back because the source text happened to match.
    TEST_CHECK(ctx, secondReport.path != firstReport.path);
    TEST_CHECK(ctx, !secondReport.wasCached);

    if (first.IsOk() && second.IsOk())
    {
        TEST_CHECK(ctx, first.Value().bytes != second.Value().bytes);
    }

    std::filesystem::remove_all(directory, error);
}

} // namespace

// The irradiance projection, compiled.
//
// It is a compute shader, and compute is the one stage nothing in this
// engine had ever actually emitted: the language knew the word and the
// RHI could build the pipeline, but no shader in the tree was one. So this
// checks the whole path at once -- the library resolves, the pass parses,
// and both backends produce something.
void TheIrradianceProjectionCompiles(TestContext* ctx)
{
    const char* source = "#include \"Fluxion/Pass/IrradianceProject.jsl\"\n";

    CompileOptions options;
    options.stage = ShaderStage::Compute;
    options.fileName = "<irradiance>";
    options.includeResolver = MakeShaderLibraryResolver();

    DiagnosticList diagnostics;
    auto result = Compile(source, options, diagnostics);

    if (!result.IsOk())
    {
        for (const Diagnostic& d : diagnostics.entries)
        {
            std::fprintf(stderr, "    %s:%u: %s\n", d.location.file.c_str(), d.location.line, d.message.c_str());
        }
    }

    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    // The sky is read at a level the shader names, which is the only kind
    // of sampling a dispatch can do -- and the two backends spell that
    // differently enough that checking one proves nothing about the other.
    TEST_CHECK(ctx, result.Value().glslSource.find("textureLod(environmentSource") != std::string::npos);
    TEST_CHECK(ctx, result.Value().hlslSource.find("environmentSource.SampleLevel(") != std::string::npos);

    // Nine coefficients out, into a buffer a compute pass can write.
    TEST_CHECK(ctx, result.Value().reflection.storageBuffers.size() == 1);
}

extern "C" void Test_ShaderLibrary_Run(TestContext* ctx)
{
    std::fprintf(stderr, "  Test_ShaderLibrary\n");

    TheLibraryIsBuiltIn(ctx);
    AShaderCanIncludeIt(ctx);
    WithoutTheResolverTheSameSourceFails(ctx);
    AnUnknownIncludeIsRefusedAndNamed(ctx);
    TheCacheKeyFollowsTheLibraryText(ctx);
    TheIrradianceProjectionCompiles(ctx);
}
