#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

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

} // namespace

void Test_Discard_Run(TestContext& ctx)
{
    ItReachesBothTargetLanguages(ctx);
    AVertexShaderIsToldItHasNoPixelsToDrop(ctx);
    ItIsFoundHoweverDeepItIs(ctx);
    AFragmentShaderMayDiscardFromAnywhere(ctx);
}
