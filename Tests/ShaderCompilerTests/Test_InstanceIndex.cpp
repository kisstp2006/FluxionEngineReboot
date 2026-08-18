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

#include <cstdio>

using namespace Fluxion::ShaderCompiler;

// Which instance a vertex belongs to.
//
// A built-in rather than an input, for the same reason ThreadID is one:
// nothing feeds it, the hardware knows it. What makes it worth its own
// file is that the two target languages disagree about what the number
// MEANS when a draw starts at a non-zero instance -- HLSL's counts from
// zero, Vulkan's includes the start instance -- and the engine's answer
// is to never start anywhere but zero. These checks hold both ends of
// that: the right built-in on each side, and a refusal where the number
// does not exist.

namespace
{

Fluxion::Foundation::Result<CompiledShader> CompileFor(ShaderStage stage, const char* source, DiagnosticList& diagnostics)
{
    CompileOptions options;
    options.stage = stage;
    options.fileName = "<test>";
    return Compile(source, options, diagnostics);
}

const char* const kVertexUsingIt =
    "[Input] Vector3 position;\n"
    "[Output] Vector4 Position;\n"
    "void main() {\n"
    "  float offset = float(InstanceIndex);\n"
    "  Position = Vector4(position.x + offset, position.y, position.z, 1.0);\n"
    "}\n";

void EachLanguageGetsItsOwnBuiltIn(TestContext& ctx)
{
    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Vertex, kVertexUsingIt, diagnostics);

    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    const std::string& glsl = result.Value().glslSource;
    const std::string& hlsl = result.Value().hlslSource;

    // GLSL: the OpenGL built-in, which counts from zero within the draw.
    // NOT gl_InstanceIndex -- that is the Vulkan-flavoured one, and it
    // adds the base instance.
    TEST_CHECK(ctx, glsl.find("gl_InstanceID") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("gl_InstanceIndex") == std::string::npos);

    // And it is not declared as an attribute in either language: a
    // built-in that also took a vertex-attribute location would push
    // every real attribute along by one, and the mesh would come apart.
    TEST_CHECK(ctx, glsl.find("in int InstanceIndex") == std::string::npos);
    TEST_CHECK(ctx, glsl.find("layout(location = 1) in") == std::string::npos);

    // HLSL: a system value on the entry point, mirrored into the name the
    // shader's author wrote.
    TEST_CHECK(ctx, hlsl.find("SV_InstanceID") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("InstanceIndex = (int)fluxionInstanceID;") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("static int InstanceIndex;") != std::string::npos);
}

void AFragmentShaderIsToldItHasNoInstance(TestContext& ctx)
{
    const char* source =
        "[Input] Vector2 uv;\n"
        "[Target(0)] Vector4 outColor;\n"
        "void main() {\n"
        "  return Vector4(float(InstanceIndex), uv.x, 0.0, 1.0);\n"
        "}\n";

    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Fragment, source, diagnostics);

    // Refused here rather than by the driver, which would report it
    // against generated text nobody wrote.
    TEST_CHECK(ctx, !result.IsOk());

    bool saidWhich = false;
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.message.find("InstanceIndex") != std::string::npos) saidWhich = true;
    }
    TEST_CHECK(ctx, saidWhich);
}

void ItIsFoundHoweverDeepItIs(TestContext& ctx)
{
    // Inside a loop inside a branch, in a function that is not the entry
    // point -- a check that only read the top of main would let this
    // through to the driver.
    const char* source =
        "[Input] Vector2 uv;\n"
        "[Target(0)] Vector4 outColor;\n"
        "float Helper(float x) {\n"
        "  float total = 0.0;\n"
        "  for (int i = 0; i < 4; i = i + 1) {\n"
        "    if (x > 0.0) {\n"
        "      total = total + float(InstanceIndex);\n"
        "    }\n"
        "  }\n"
        "  return total;\n"
        "}\n"
        "void main() {\n"
        "  return Vector4(Helper(uv.x), 0.0, 0.0, 1.0);\n"
        "}\n";

    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Fragment, source, diagnostics);
    TEST_CHECK(ctx, !result.IsOk());
}

void TheComputeBuiltInIsHeldToTheSameRule(TestContext& ctx)
{
    // ThreadID was seeded permissively long before this file existed, so
    // a vertex shader naming it compiled here and failed in the driver.
    // The same check that guards InstanceIndex now guards it.
    const char* source =
        "[Input] Vector3 position;\n"
        "[Output] Vector4 Position;\n"
        "void main() {\n"
        "  Position = Vector4(position, float(ThreadID));\n"
        "}\n";

    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Vertex, source, diagnostics);
    TEST_CHECK(ctx, !result.IsOk());

    bool saidWhich = false;
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.message.find("ThreadID") != std::string::npos) saidWhich = true;
    }
    TEST_CHECK(ctx, saidWhich);
}

// --- Claiming a slot ------------------------------------------------------

const char* const kComputeWithAtomic =
    "[Buffer(Global)] int counters;\n"
    "[Buffer(Global)] int slots;\n"
    "void main() {\n"
    "  int slot = AtomicAdd(counters[0], 1);\n"
    "  slots[slot] = int(ThreadID);\n"
    "}\n";

void AnAtomicAddIsSpelledDifferentlyInEachLanguage(TestContext& ctx)
{
    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Compute, kComputeWithAtomic, diagnostics);

    if (!result.IsOk())
    {
        for (const Diagnostic& entry : diagnostics.entries)
            std::fprintf(stderr, "    %s:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.message.c_str());
    }
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    const std::string& glsl = result.Value().glslSource;
    const std::string& hlsl = result.Value().hlslSource;

    // One language answers with the old value, so it is an expression.
    TEST_CHECK(ctx, glsl.find("int slot = atomicAdd(counters[0], 1);") != std::string::npos);

    // The other fills in an out parameter, so it is two statements -- and
    // the variable has to exist before the call that fills it.
    TEST_CHECK(ctx, hlsl.find("int slot;") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("InterlockedAdd(counters[0], 1, slot);") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("= InterlockedAdd") == std::string::npos);
}

void AnAtomicAddIsRefusedWhereItCannotBeWritten(TestContext& ctx)
{
    // Not in a fragment shader: a storage buffer is read-only there.
    const char* const inFragment =
        "[Buffer(Frame)] int counters;\n"
        "[Target(0)] Vector4 outColor;\n"
        "void main() {\n"
        "  int slot = AtomicAdd(counters[0], 1);\n"
        "  return Vector4(float(slot), 0.0, 0.0, 1.0);\n"
        "}\n";

    DiagnosticList fragmentDiagnostics;
    TEST_CHECK(ctx, !CompileFor(ShaderStage::Fragment, inFragment, fragmentDiagnostics).IsOk());

    // And not in the middle of an expression, however true that would be
    // in one of the two languages.
    const char* const insideAnExpression =
        "[Buffer(Global)] int counters;\n"
        "[Buffer(Global)] int slots;\n"
        "void main() {\n"
        "  slots[0] = AtomicAdd(counters[0], 1) + 1;\n"
        "}\n";

    DiagnosticList shapeDiagnostics;
    TEST_CHECK(ctx, !CompileFor(ShaderStage::Compute, insideAnExpression, shapeDiagnostics).IsOk());

    bool saidWhy = false;
    for (const Diagnostic& entry : shapeDiagnostics.entries)
    {
        if (entry.message.find("initialiser of a variable") != std::string::npos) saidWhy = true;
    }
    TEST_CHECK(ctx, saidWhy);
}

} // namespace

void Test_InstanceIndex_Run(TestContext& ctx)
{
    EachLanguageGetsItsOwnBuiltIn(ctx);
    AFragmentShaderIsToldItHasNoInstance(ctx);
    ItIsFoundHoweverDeepItIs(ctx);
    TheComputeBuiltInIsHeldToTheSameRule(ctx);
    AnAtomicAddIsSpelledDifferentlyInEachLanguage(ctx);
    AnAtomicAddIsRefusedWhereItCannotBeWritten(ctx);
}
