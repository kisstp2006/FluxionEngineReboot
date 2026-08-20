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

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <string>

// A FLOAT LITERAL HAS TO STILL BE A FLOAT WHEN IT ARRIVES.
//
// Both languages this compiles to have integer division, and both spell
// it with the same slash. So a literal written 1.0 and printed "1" turns
// (1.0 / 16.0) into (1 / 16), which is not a rounding error -- it is
// ZERO, and it is zero silently: no diagnostic, no warning, a shader that
// compiles and returns black.
//
// Measured, before this was fixed: the bloom chain multiplied its nine
// weighted taps by (1.0 / 16.0) and produced nothing at all, in a pass
// whose every other part was correct.
//
// What this checks is the emitted text rather than a compiled result,
// because the fault is entirely in the printing: the value is right in
// the compiler and wrong in the file it writes.

namespace
{

using namespace Fluxion::ShaderCompiler;

const char* const kSource = R"(
[Target(0)] Vector4 fragColor;

void main() {
  float whole = 1.0;
  float fraction = 0.5;
  float scaled = whole * (1.0 / 16.0);
  return Vector4(scaled, fraction, whole, 1.0);
}
)";

CompileOptions Options()
{
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<Test_FloatLiterals>";
    return options;
}

bool Contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

void AWholeNumberedFloatIsNotPrintedAsAnInteger(TestContext& ctx)
{
    DiagnosticList diagnostics;
    auto result = Compile(kSource, Options(), diagnostics);
    if (!result.IsOk()) { TEST_CHECK(ctx, false); return; }

    const std::string& glsl = result.Value().glslSource;
    const std::string& hlsl = result.Value().hlslSource;

    // THE DIVISION IS THE CASE THAT MATTERS: both sides are literals, so
    // nothing else in the expression can promote them.
    TEST_CHECK(ctx, Contains(glsl, "1.0 / 16.0"));
    TEST_CHECK(ctx, Contains(hlsl, "1.0 / 16.0"));

    // And "1 / 16" must not appear at all, which is what it used to say.
    TEST_CHECK(ctx, !Contains(glsl, "1 / 16"));
    TEST_CHECK(ctx, !Contains(hlsl, "1 / 16"));

    // A literal that already had a fraction is left as it was, rather
    // than gaining a second point or losing digits.
    TEST_CHECK(ctx, Contains(glsl, "0.5"));
    TEST_CHECK(ctx, Contains(hlsl, "0.5"));
}

} // namespace

void Test_FloatLiterals_Run(TestContext& ctx)
{
    AWholeNumberedFloatIsNotPrintedAsAnInteger(ctx);
}
