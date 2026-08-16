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

// Dropping a pixel.
//
// There is no way to express this with the rest of the language: it is
// not a return, because a return still writes a value, and it is not a
// zero opacity, because a pass that does not blend would write that zero
// as black. So it is its own statement, and these are the checks that it
// reaches both target languages and is refused where it means nothing.

namespace
{

const char* const kFragmentWithDiscard =
    "[Input] Vector2 uv;\n"
    "[Target(0)] Vector4 outColor;\n"
    "void main() {\n"
    "  if (uv.x < 0.5) {\n"
    "    discard;\n"
    "  }\n"
    "  return Vector4(1.0, 1.0, 1.0, 1.0);\n"
    "}\n";

Fluxion::Foundation::Result<CompiledShader> CompileFor(ShaderStage stage, const char* source, DiagnosticList& diagnostics)
{
    CompileOptions options;
    options.stage = stage;
    options.fileName = "<test>";
    return Compile(source, options, diagnostics);
}

void ItReachesBothTargetLanguages(TestContext& ctx)
{
    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Fragment, kFragmentWithDiscard, diagnostics);

    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    // Both languages spell it the same way, which is luck rather than
    // design -- so both are checked rather than one standing in for the
    // other.
    TEST_CHECK(ctx, result.Value().glslSource.find("discard;") != std::string::npos);
    TEST_CHECK(ctx, result.Value().hlslSource.find("discard;") != std::string::npos);

    // And the rest of the function survived it. A statement that swallowed
    // what followed would still contain the word.
    TEST_CHECK(ctx, result.Value().glslSource.find("outColor = ") != std::string::npos);
    TEST_CHECK(ctx, result.Value().hlslSource.find("outColor = ") != std::string::npos);
}

void AVertexShaderIsToldItHasNoPixelsToDrop(TestContext& ctx)
{
    const char* source =
        "[Input] Vector3 position;\n"
        "[Output] Vector4 Position;\n"
        "void main() {\n"
        "  discard;\n"
        "  Position = Vector4(position, 1.0);\n"
        "}\n";

    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Vertex, source, diagnostics);

    // Refused here rather than by whatever compiles the generated text,
    // which would report it against a line nobody wrote.
    TEST_CHECK(ctx, !result.IsOk());

    bool saidWhy = false;
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.message.find("discard") != std::string::npos) saidWhy = true;
    }
    TEST_CHECK(ctx, saidWhy);
}

void ItIsFoundHoweverDeepItIs(TestContext& ctx)
{
    // Nested inside a loop inside a branch, in a function that is not the
    // entry point. A check that only looked at the top level of main
    // would pass this and let the real error through.
    const char* source =
        "[Input] Vector3 position;\n"
        "[Output] Vector4 Position;\n"
        "void Helper(float x) {\n"
        "  for (int i = 0; i < 4; i = i + 1) {\n"
        "    if (x > 0.0) {\n"
        "      discard;\n"
        "    }\n"
        "  }\n"
        "}\n"
        "void main() {\n"
        "  Helper(position.x);\n"
        "  Position = Vector4(position, 1.0);\n"
        "}\n";

    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Vertex, source, diagnostics);
    TEST_CHECK(ctx, !result.IsOk());
}

void AFragmentShaderMayDiscardFromAnywhere(TestContext& ctx)
{
    // The mirror of the check above: the same shape must be accepted in
    // the stage where it does mean something, or the rule would be
    // refusing more than it meant to.
    const char* source =
        "[Input] Vector2 uv;\n"
        "[Target(0)] Vector4 outColor;\n"
        "void Helper(float x) {\n"
        "  if (x > 0.0) {\n"
        "    discard;\n"
        "  }\n"
        "}\n"
        "void main() {\n"
        "  Helper(uv.x);\n"
        "  return Vector4(1.0, 1.0, 1.0, 1.0);\n"
        "}\n";

    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Fragment, source, diagnostics);
    TEST_CHECK(ctx, result.IsOk());
}


void AStorageBufferOfStructsDeclaresTheStructFirst(TestContext& ctx)
{
    // The shape a light list has: a buffer whose element is a struct the
    // shader itself declares.
    //
    // Both backends used to write the buffer out before the struct, so
    // the generated text named a type that had not been declared yet.
    // Nothing noticed, because the only storage buffer anywhere was a
    // buffer of floats -- and a float needs no declaration. The moment an
    // element type was a struct, both target languages would have
    // rejected it, at a line nobody wrote.
    const char* source =
        "struct LightData {\n"
        "  Vector4 positionRange;\n"
        "  Vector4 color;\n"
        "};\n"
        "[Buffer(Frame)] LightData lights;\n"
        "[Input] Vector2 uv;\n"
        "[Target(0)] Vector4 outColor;\n"
        "void main() {\n"
        "  return lights[0].color;\n"
        "}\n";

    DiagnosticList diagnostics;
    auto result = CompileFor(ShaderStage::Fragment, source, diagnostics);
    if (!result.IsOk())
    {
        for (const Diagnostic& entry : diagnostics.entries)
            std::fprintf(stderr, "    %s:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.message.c_str());
    }
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    // Declared before it is used, in both languages. Comparing positions
    // rather than merely looking for the word: the word was always there,
    // just in the wrong place.
    const std::string& glsl = result.Value().glslSource;
    const std::string& hlsl = result.Value().hlslSource;

    TEST_CHECK(ctx, glsl.find("struct LightData") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("struct LightData") < glsl.find("LightData lights[]"));

    TEST_CHECK(ctx, hlsl.find("struct LightData") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("struct LightData") < hlsl.find("StructuredBuffer<LightData>"));

    // And in a fragment shader it is READ-ONLY. A writable one is
    // rejected by Vulkan unless a device feature nothing here asks for is
    // switched on, so the backend picks the read-only form -- which is
    // exactly what a light list wants anyway.
    TEST_CHECK(ctx, hlsl.find("RWStructuredBuffer") == std::string::npos);
}

} // namespace

void Test_Discard_Run(TestContext& ctx)
{
    ItReachesBothTargetLanguages(ctx);
    AVertexShaderIsToldItHasNoPixelsToDrop(ctx);
    ItIsFoundHoweverDeepItIs(ctx);
    AFragmentShaderMayDiscardFromAnywhere(ctx);
    AStorageBufferOfStructsDeclaresTheStructFirst(ctx);
}
