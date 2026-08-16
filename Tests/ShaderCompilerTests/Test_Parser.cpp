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

#include <Fluxion/ShaderCompiler/Frontend/Lexer.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Parser.hpp>

using namespace Fluxion::ShaderCompiler;

namespace
{

Program ParseSource(const char* source, DiagnosticList& diagnostics)
{
    std::vector<Token> tokens = Lex(source, "<test>", diagnostics);
    return Parse(tokens, diagnostics);
}

} // namespace

void Test_Parser_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        Program program = ParseSource("void main() { float x = 1.0; }", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 1);
        TEST_CHECK(ctx, program.declarations[0]->kind == DeclKind::Function);
    }
    {
        DiagnosticList diagnostics;
        ParseSource("void main() { float x = ; }", diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
    {
        // Swizzle, function call, and if/for control flow.
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "float f(Vector3 v) {\n"
            "  float total = 0.0;\n"
            "  for (int i = 0; i < 3; i += 1) { total += v.x; }\n"
            "  if (total > 0.5) return v.y; else return v.z;\n"
            "}\n",
            diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 1);
    }
    {
        // The attribute declarations + spelled-out type names this
        // language exclusively uses.
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "[Input] Vector3 vColor;\n"
            "[Target(0)] Vector4 fragColor;\n"
            "[Uniform] Vector3 tint;\n"
            "[Texture] Texture2D albedoMap;\n"
            "void main() { return Vector4(vColor, 1.0); }\n",
            diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 5);
        TEST_CHECK(ctx, program.declarations[0]->kind == DeclKind::StageIO);
        TEST_CHECK(ctx, program.declarations[1]->kind == DeclKind::OutputSlot);
        TEST_CHECK(ctx, program.declarations[2]->kind == DeclKind::Uniform);
        TEST_CHECK(ctx, program.declarations[3]->kind == DeclKind::Uniform);
        TEST_CHECK(ctx, program.declarations[4]->kind == DeclKind::Function);
    }
    {
        // [Target] with no explicit slot index is a parse error.
        DiagnosticList diagnostics;
        ParseSource("[Target] Vector4 fragColor;\n", diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
}
