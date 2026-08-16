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

#include <Fluxion/Foundation/Hashing.h>

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <fstream>
#include <sstream>

using namespace Fluxion::ShaderCompiler;

namespace
{

std::string ReadFixture(const std::string& name)
{
    std::ifstream file(std::string(FLUXION_TEST_SHADERCOMPILER_FIXTURES_DIR) + "/" + name);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

IncludeResolver FixtureIncludeResolver()
{
    return [](const std::string& name, std::string& outContent) -> bool
    {
        std::ifstream file(std::string(FLUXION_TEST_SHADERCOMPILER_FIXTURES_DIR) + "/" + name);
        if (!file) return false;
        std::ostringstream contents;
        contents << file.rdbuf();
        outContent = contents.str();
        return true;
    };
}

void CompileFixture(TestContext& ctx, const char* fixtureName)
{
    std::string source = ReadFixture(fixtureName);
    TEST_CHECK(ctx, !source.empty());

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = fixtureName;
    options.includeResolver = FixtureIncludeResolver();

    auto result = Compile(source, options, diagnostics);
    if (!result.IsOk())
    {
        for (const Diagnostic& d : diagnostics.entries)
            std::fprintf(stderr, "  %s:%u: %s\n", d.location.file.c_str(), d.location.line, d.message.c_str());
    }
    TEST_CHECK(ctx, result.IsOk());
}

} // namespace

void Test_Compatibility_Run(TestContext& ctx)
{
    // A larger, multi-file fixture corpus -- proves the frontend/
    // semantic analyzer/backends handle real, non-trivial source (a
    // shared #include, function overloading, texture sampling, uniforms,
    // swizzles, a real `return` routed to a [Target(N)]), not just the
    // small synthetic snippets the other unit tests use.
    CompileFixture(ctx, "common.jsl");
    CompileFixture(ctx, "lit.jsl");

    {
        // What a compilation actually read has to come back with it. A
        // source that includes something else is only "the same source"
        // as long as that something else has not changed, and the source
        // text on its own cannot say whether it has -- so anything that
        // holds on to a compiler's answer needs this list, and needs it
        // to name what was read and to say what it contained.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.stage = ShaderStage::Fragment;
        options.fileName = "lit.jsl";
        options.includeResolver = FixtureIncludeResolver();

        auto result = Compile(ReadFixture("lit.jsl"), options, diagnostics);
        TEST_CHECK(ctx, result.IsOk());
        if (result.IsOk())
        {
            const std::vector<ResolvedInclude>& includes = result.Value().includes;
            TEST_CHECK(ctx, includes.size() == 1);
            if (includes.size() == 1)
            {
                TEST_CHECK(ctx, includes[0].name == "common.jsl");

                // The hash is of what came back, so it has to match the
                // file itself -- not merely be non-zero, which an empty
                // or wrongly-read include could also manage.
                const std::string common = ReadFixture("common.jsl");
                TEST_CHECK(ctx, includes[0].contentHash == Fluxion_HashBytes64(common.data(), common.size()));
            }
        }

        // A source with nothing to read reports nothing read, rather than
        // whatever the previous call happened to leave behind.
        DiagnosticList plainDiagnostics;
        CompileOptions plainOptions;
        plainOptions.stage = ShaderStage::Fragment;
        plainOptions.fileName = "common.jsl";
        plainOptions.includeResolver = FixtureIncludeResolver();

        auto plain = Compile(ReadFixture("common.jsl"), plainOptions, plainDiagnostics);
        TEST_CHECK(ctx, plain.IsOk());
        if (plain.IsOk()) TEST_CHECK(ctx, plain.Value().includes.empty());
    }
}
