#include "TestFramework.h"

#include <Fluxion/RenderCore/Renderer/MaterialShader.h>
#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace Fluxion::RenderCore
{
Fluxion::ShaderCompiler::IncludeResolver MakeShaderLibraryResolver();
} // namespace Fluxion::RenderCore

namespace
{

using namespace Fluxion::ShaderCompiler;

// One material, written once. Everything below reads THIS string -- if a
// pass needed its own copy, the arrangement would not be worth having.
const char* const kMaterial = R"(
#include "Fluxion/Surface.jsl"

[Input] Vector2 vUV;
[Uniform(Material)] Vector3 baseColorFactor;

SurfaceData EvaluateSurface() {
  SurfaceData surface = DefaultSurface();
  surface.baseColor = baseColorFactor * vUV.x;
  surface.perceptualRoughness = vUV.y;
  surface.metallic = 0.0;
  surface.emissive = Vector3(0.0, 0.0, 0.0);
  return surface;
}
)";

CompileOptions FragmentOptions()
{
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<Test_SurfaceData>";
    options.includeResolver = Fluxion::RenderCore::MakeShaderLibraryResolver();
    return options;
}

void Report(TestContext* ctx, const DiagnosticList& diagnostics)
{
    (void)ctx;
    for (const Diagnostic& d : diagnostics.entries)
        std::fprintf(stderr, "    %s:%u: %s\n", d.location.file.c_str(), d.location.line, d.message.c_str());
}

bool Mentions(const DiagnosticList& diagnostics, const char* text)
{
    for (const Diagnostic& d : diagnostics.entries)
        if (d.message.find(text) != std::string::npos) return true;
    return false;
}

void TheSourceIsAssembledInTheRightOrder(TestContext* ctx)
{
    char* forward = Fluxion_MaterialShader_BuildFragmentSource("MATERIAL_BODY", FLUXION_MATERIAL_PASS_FORWARD);
    TEST_CHECK(ctx, forward != nullptr);
    if (forward)
    {
        const std::string text = forward;
        const std::size_t bodyAt = text.find("MATERIAL_BODY");
        const std::size_t includeAt = text.find("Fluxion/Pass/Forward.jsl");

        TEST_CHECK(ctx, bodyAt != std::string::npos);
        TEST_CHECK(ctx, includeAt != std::string::npos);

        // The pass comes after the material, because its entry point
        // calls what the material declares.
        TEST_CHECK(ctx, bodyAt < includeAt);

        // And on its own line: a directive that does not start a line is
        // not a directive, and the failure would be a parse error inside
        // the material about something its author never wrote.
        TEST_CHECK(ctx, text.find("MATERIAL_BODY\n#include") != std::string::npos);

        Fluxion_MaterialShader_FreeSource(forward);
    }

    char* depth = Fluxion_MaterialShader_BuildFragmentSource("MATERIAL_BODY", FLUXION_MATERIAL_PASS_DEPTH_ONLY);
    TEST_CHECK(ctx, depth != nullptr);
    if (depth)
    {
        TEST_CHECK(ctx, std::strstr(depth, "Fluxion/Pass/DepthOnly.jsl") != nullptr);
        Fluxion_MaterialShader_FreeSource(depth);
    }

    TEST_CHECK(ctx, Fluxion_MaterialShader_BuildFragmentSource("x", FLUXION_MATERIAL_PASS_COUNT) == nullptr);
    TEST_CHECK(ctx, Fluxion_MaterialShader_BuildFragmentSource(nullptr, FLUXION_MATERIAL_PASS_FORWARD) == nullptr);

    // Every pass in the range has somewhere to get its entry point from.
    for (int pass = 0; pass < FLUXION_MATERIAL_PASS_COUNT; ++pass)
        TEST_CHECK(ctx, Fluxion_MaterialShader_GetPassInclude((FluxionMaterialPass)pass) != nullptr);
}

// The claim the whole arrangement exists for.
void OneMaterialCompilesUnderBothPasses(TestContext* ctx)
{
    std::string forwardGlsl;
    std::string depthGlsl;

    {
        char* source = Fluxion_MaterialShader_BuildFragmentSource(kMaterial, FLUXION_MATERIAL_PASS_FORWARD);
        TEST_CHECK(ctx, source != nullptr);
        if (!source) return;

        DiagnosticList diagnostics;
        auto result = Compile(source, FragmentOptions(), diagnostics);
        if (!result.IsOk()) Report(ctx, diagnostics);
        TEST_CHECK(ctx, result.IsOk());
        if (result.IsOk()) forwardGlsl = result.Value().glslSource;

        Fluxion_MaterialShader_FreeSource(source);
    }

    {
        char* source = Fluxion_MaterialShader_BuildFragmentSource(kMaterial, FLUXION_MATERIAL_PASS_DEPTH_ONLY);
        TEST_CHECK(ctx, source != nullptr);
        if (!source) return;

        DiagnosticList diagnostics;
        auto result = Compile(source, FragmentOptions(), diagnostics);
        if (!result.IsOk()) Report(ctx, diagnostics);
        TEST_CHECK(ctx, result.IsOk());
        if (result.IsOk()) depthGlsl = result.Value().glslSource;

        Fluxion_MaterialShader_FreeSource(source);
    }

    if (forwardGlsl.empty() || depthGlsl.empty()) return;

    // Both really did read the same material.
    TEST_CHECK(ctx, forwardGlsl.find("EvaluateSurface") != std::string::npos);
    TEST_CHECK(ctx, depthGlsl.find("EvaluateSurface") != std::string::npos);

    // And they are not the same shader. Without this the check above
    // would pass on a build where the pass made no difference at all.
    TEST_CHECK(ctx, forwardGlsl != depthGlsl);

    // The difference has to be looked for in the ENTRY POINT, not in the
    // whole file: the struct and its helpers come from the library and
    // appear in both, so searching the text as a whole would find every
    // field name in both and prove nothing.
    const std::size_t forwardMain = forwardGlsl.rfind("void main(");
    const std::size_t depthMain = depthGlsl.rfind("void main(");
    TEST_CHECK(ctx, forwardMain != std::string::npos && depthMain != std::string::npos);
    if (forwardMain == std::string::npos || depthMain == std::string::npos) return;

    const std::string forwardBody = forwardGlsl.substr(forwardMain);
    const std::string depthBody = depthGlsl.substr(depthMain);

    // The forward pass reads the surface's colour and its occlusion; the
    // depth pass reads the opacity and nothing else.
    TEST_CHECK(ctx, forwardBody.find("baseColor") != std::string::npos);
    TEST_CHECK(ctx, forwardBody.find("ambientOcclusion") != std::string::npos);

    TEST_CHECK(ctx, depthBody.find("baseColor") == std::string::npos);
    TEST_CHECK(ctx, depthBody.find("ambientOcclusion") == std::string::npos);
    TEST_CHECK(ctx, depthBody.find("opacity") != std::string::npos);
}

// Structs have to survive all the way to the emitted text, not merely
// parse. Until this was checked, a struct-typed thing became a float
// silently -- which compiles, and is wrong.
void TheStructReachesTheOutput(TestContext* ctx)
{
    char* source = Fluxion_MaterialShader_BuildFragmentSource(kMaterial, FLUXION_MATERIAL_PASS_FORWARD);
    if (!source) { TEST_CHECK(ctx, false); return; }

    DiagnosticList diagnostics;
    auto result = Compile(source, FragmentOptions(), diagnostics);
    Fluxion_MaterialShader_FreeSource(source);

    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) { Report(ctx, diagnostics); return; }

    for (const std::string& text : { result.Value().glslSource, result.Value().hlslSource })
    {
        // The declaration, with its fields.
        TEST_CHECK(ctx, text.find("struct SurfaceData") != std::string::npos);
        TEST_CHECK(ctx, text.find("perceptualRoughness") != std::string::npos);

        // And the type used under its own name rather than quietly
        // becoming a float.
        TEST_CHECK(ctx, text.find("SurfaceData EvaluateSurface(") != std::string::npos);
        TEST_CHECK(ctx, text.find("SurfaceData DefaultSurface(") != std::string::npos);
    }

    // The struct is declared before whatever returns one.
    const std::string& hlsl = result.Value().hlslSource;
    TEST_CHECK(ctx, hlsl.find("struct SurfaceData") < hlsl.find("SurfaceData DefaultSurface("));
}

void MistakesAreNamed(TestContext* ctx)
{
    // A material that never declares the function the pass calls.
    {
        char* source = Fluxion_MaterialShader_BuildFragmentSource(
            "#include \"Fluxion/Surface.jsl\"\n", FLUXION_MATERIAL_PASS_FORWARD);
        DiagnosticList diagnostics;
        auto result = Compile(source, FragmentOptions(), diagnostics);
        Fluxion_MaterialShader_FreeSource(source);

        TEST_CHECK(ctx, !result.IsOk());
        TEST_CHECK(ctx, Mentions(diagnostics, FLUXION_MATERIAL_SURFACE_FUNCTION));
    }

    // A field the struct does not have. Reading it would otherwise be
    // guessed at, and a guess here is a value that looks plausible.
    {
        const char* const material = R"(
#include "Fluxion/Surface.jsl"
SurfaceData EvaluateSurface() {
  SurfaceData surface = DefaultSurface();
  surface.shininess = 1.0;
  return surface;
}
)";
        char* source = Fluxion_MaterialShader_BuildFragmentSource(material, FLUXION_MATERIAL_PASS_FORWARD);
        DiagnosticList diagnostics;
        auto result = Compile(source, FragmentOptions(), diagnostics);
        Fluxion_MaterialShader_FreeSource(source);

        TEST_CHECK(ctx, !result.IsOk());
        TEST_CHECK(ctx, Mentions(diagnostics, "shininess"));
    }

    // A type nobody declared.
    {
        const char* const material = R"(
#include "Fluxion/Surface.jsl"
SurfaceData EvaluateSurface() {
  SurfaceData surface = DefaultSurface();
  return surface;
}
NotAType Helper() { return DefaultSurface(); }
)";
        char* source = Fluxion_MaterialShader_BuildFragmentSource(material, FLUXION_MATERIAL_PASS_FORWARD);
        DiagnosticList diagnostics;
        auto result = Compile(source, FragmentOptions(), diagnostics);
        Fluxion_MaterialShader_FreeSource(source);

        TEST_CHECK(ctx, !result.IsOk());
    }
}

} // namespace

extern "C" void Test_SurfaceData_Run(TestContext* ctx)
{
    std::fprintf(stderr, "  Test_SurfaceData\n");

    TheSourceIsAssembledInTheRightOrder(ctx);
    OneMaterialCompilesUnderBothPasses(ctx);
    TheStructReachesTheOutput(ctx);
    MistakesAreNamed(ctx);
}
