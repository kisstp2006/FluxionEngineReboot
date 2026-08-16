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

#include <Fluxion/ShaderCompiler/Frontend/Preprocessor.hpp>

#include <string>

using namespace Fluxion::ShaderCompiler;

namespace
{

// The three bytes are written as escapes in their own string literal
// rather than pasted in as characters, for two reasons: a source file
// containing a real marker mid-line is the kind of thing an editor
// silently rewrites, and keeping them separate stops "\xBF" from
// swallowing the next character as another hex digit.
const char* const kBom = "\xEF\xBB\xBF";

IncludeResolver MakeResolver(const std::string& content)
{
    return [content](const std::string& name, std::string& outContent) -> bool
    {
        if (name != "shared.jsl") return false;
        outContent = content;
        return true;
    };
}

} // namespace

void Test_Preprocessor_Run(TestContext& ctx)
{
    {
        // The case this exists for. A marker in front of the '#' stops
        // the line being a directive at all, so it is copied out
        // verbatim: the include silently does not happen, and what
        // surfaces later is a missing definition somewhere else entirely.
        DiagnosticList diagnostics;
        const std::string source = std::string(kBom) + "#include \"shared.jsl\"\nfloat b = 2.0;\n";
        const std::string out = Preprocess(source, "<test>", MakeResolver("float a = 1.0;\n"), diagnostics);

        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, out.find("float a = 1.0;") != std::string::npos);
        TEST_CHECK(ctx, out.find("float b = 2.0;") != std::string::npos);

        // And the directive itself must not have survived into the
        // output -- that is exactly what the broken behaviour looked
        // like, and it would otherwise reach the lexer as garbage.
        TEST_CHECK(ctx, out.find("#include") == std::string::npos);
    }
    {
        // An included file can carry a marker of its own, and it is a
        // separate read: the top-level file being clean says nothing
        // about this one.
        DiagnosticList diagnostics;
        const std::string included = std::string(kBom) + "#define VALUE 3\n";
        const std::string source = "#include \"shared.jsl\"\n#ifdef VALUE\nfloat c = 3.0;\n#endif\n";
        const std::string out = Preprocess(source, "<test>", MakeResolver(included), diagnostics);

        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, out.find("float c = 3.0;") != std::string::npos);
    }
    {
        // Without a marker, nothing changes -- otherwise the checks
        // above could be passing because of something unrelated to the
        // marker at all.
        DiagnosticList diagnostics;
        const std::string out = Preprocess("#include \"shared.jsl\"\n", "<test>", MakeResolver("float a = 1.0;\n"), diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, out.find("float a = 1.0;") != std::string::npos);
    }
    {
        // A marker is not a licence to ignore a real failure: the
        // include still has to be reported as unresolvable.
        DiagnosticList diagnostics;
        const std::string source = std::string(kBom) + "#include \"missing.jsl\"\n";
        Preprocess(source, "<test>", MakeResolver("float a = 1.0;\n"), diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
}
