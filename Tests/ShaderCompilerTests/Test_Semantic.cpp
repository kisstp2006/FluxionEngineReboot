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
#include <Fluxion/ShaderCompiler/Semantic/SemanticAnalyzer.hpp>

using namespace Fluxion::ShaderCompiler;

namespace
{

bool AnalyzeSource(const char* source, DiagnosticList& diagnostics)
{
    std::vector<Token> tokens = Lex(source, "<test>", diagnostics);
    Program program = Parse(tokens, diagnostics);
    if (diagnostics.HasErrors()) return false;
    return Analyze(program, diagnostics);
}

} // namespace

void Test_Semantic_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource("void main() { Vector3 v = Vector3(1.0, 2.0, 3.0); float x = v.x + v.y; }", diagnostics);
        TEST_CHECK(ctx, ok);
    }
    {
        // Swizzle out of range for a Vector2.
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource("void main() { Vector2 v = Vector2(1.0, 2.0); float x = v.z; }", diagnostics);
        TEST_CHECK(ctx, !ok);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
    {
        // Overload resolution by argument type/count.
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource(
            "float avg(Vector2 x) { return (x.x + x.y) / 2.0; }\n"
            "float avg(Vector3 x) { return (x.x + x.y + x.z) / 3.0; }\n"
            "void main() { Vector2 a = Vector2(1.0, 2.0); float r = avg(a); }\n",
            diagnostics);
        TEST_CHECK(ctx, ok);
    }
    {
        // Use of an undeclared identifier.
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource("void main() { float x = undeclaredThing; }", diagnostics);
        TEST_CHECK(ctx, !ok);
    }
    {
        // Attribute declarations + a real `return` routed to [Target(0)].
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource(
            "[Input] Vector3 vColor;\n"
            "[Target(0)] Vector4 fragColor;\n"
            "void main() { return Vector4(vColor, 1.0); }\n",
            diagnostics);
        TEST_CHECK(ctx, ok);
    }
    {
        // A name one of the target languages has taken.
        //
        // This is not pedantry about a word nobody would use: `packed`
        // describes exactly what a normal map holds, it reads perfectly
        // in this language, HLSL accepted it, and the shader failed only
        // on OpenGL -- as a syntax error against generated text, at a
        // line the author could not look at. Refused here, where the
        // name is, and where the message can name it.
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource("void main() { Vector3 packed = Vector3(1.0, 1.0, 1.0); }", diagnostics);
        TEST_CHECK(ctx, !ok);

        bool namedIt = false;
        for (const Diagnostic& entry : diagnostics.entries)
        {
            if (entry.message.find("'packed'") != std::string::npos) namedIt = true;
        }
        TEST_CHECK(ctx, namedIt);
    }
    {
        // Everywhere a name can be introduced, not only local variables:
        // a uniform, a function, a parameter and a struct field all end
        // up in the generated text too.
        DiagnosticList uniformDiagnostics;
        TEST_CHECK(ctx, !AnalyzeSource("[Uniform] float sample;\nvoid main() { }\n", uniformDiagnostics));

        DiagnosticList functionDiagnostics;
        TEST_CHECK(ctx, !AnalyzeSource("float filter(float x) { return x; }\nvoid main() { }\n", functionDiagnostics));

        DiagnosticList parameterDiagnostics;
        TEST_CHECK(ctx, !AnalyzeSource("float twice(float input) { return input; }\nvoid main() { }\n", parameterDiagnostics));

        DiagnosticList fieldDiagnostics;
        TEST_CHECK(ctx, !AnalyzeSource("struct Thing { float shared; }\nvoid main() { }\n", fieldDiagnostics));
    }
    {
        // And a name that merely CONTAINS one is fine -- the rule is
        // about whole words, or it would refuse half the sensible names
        // there are.
        DiagnosticList diagnostics;
        TEST_CHECK(ctx, AnalyzeSource("void main() { Vector3 packedNormal = Vector3(1.0, 1.0, 1.0); float sampleCount = 4.0; }", diagnostics));
    }
}
