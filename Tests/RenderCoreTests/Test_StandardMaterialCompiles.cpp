#include "TestFramework.h"

#include <Fluxion/RenderCore/Renderer/MaterialShader.h>
#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <cstdio>
#include <string>

namespace Fluxion::RenderCore
{
Fluxion::ShaderCompiler::IncludeResolver MakeShaderLibraryResolver();
} // namespace Fluxion::RenderCore

// The engine's own standard material, put through BOTH target languages.
//
// Why this exists: the two languages do not agree on which words are
// taken. A variable named after one of GLSL's reserved words compiles
// perfectly through the HLSL path -- which is what the Vulkan and D3D12
// backends use -- and fails only on OpenGL, as a syntax error in
// generated text nobody wrote, at a line number that means nothing.
//
// The GPU checks elsewhere in this suite run on Vulkan and D3D12, because
// those are the backends that work without a window. So they cannot catch
// this, and did not: the standard material carried a variable called
// `packed` until somebody ran the sample on OpenGL. This check needs no
// device at all -- it only asks the compiler for both languages, which is
// exactly the question that was going unasked.

namespace
{

// The smallest material there is: everything comes from the library.
const char* const kStandardMaterial = R"(
#include "Fluxion/Material.jsl"

SurfaceData EvaluateSurface() {
  return StandardSurface();
}
)";

// And one that touches the parts a minimal material does not, so the
// check covers more of the library than the shortest path through it.
const char* const kElaborateMaterial = R"(
#include "Fluxion/Material.jsl"

SurfaceData EvaluateSurface() {
  SurfaceData surface = StandardSurface();
  surface.normal = ApplyNormalMap(vWorldNormal, vWorldTangent, vUV, normalScale * 2.0);
  surface.perceptualRoughness = clamp(surface.perceptualRoughness * 0.5, FLUXION_MIN_PERCEPTUAL_ROUGHNESS, 1.0);
  surface.emissive = surface.emissive + surface.baseColor * 0.1;
  return surface;
}
)";

void CompileOneStage(TestContext* ctx, const std::string& source, Fluxion::ShaderCompiler::ShaderStage stage, const char* what)
{
    using namespace Fluxion::ShaderCompiler;

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = stage;
    options.fileName = "<standard-material>";
    options.includeResolver = Fluxion::RenderCore::MakeShaderLibraryResolver();

    auto result = Compile(source, options, diagnostics);
    if (!result.IsOk())
    {
        std::fprintf(stderr, "  %s did not compile:\n", what);
        for (const Diagnostic& entry : diagnostics.entries)
            std::fprintf(stderr, "    %s:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.message.c_str());
    }
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    // Both, every time. One of them succeeding says nothing about the
    // other -- that is the whole reason this check exists.
    TEST_CHECK(ctx, !result.Value().glslSource.empty());
    TEST_CHECK(ctx, !result.Value().hlslSource.empty());
}

void CompileMaterialForEveryPass(TestContext* ctx, const char* materialSource, const char* what)
{
    for (u32 pass = 0; pass < (u32)FLUXION_MATERIAL_PASS_COUNT; ++pass)
    {
        char* vertexSource = Fluxion_MaterialShader_BuildVertexSource((FluxionMaterialPass)pass);
        char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(materialSource, (FluxionMaterialPass)pass);

        TEST_CHECK(ctx, vertexSource != nullptr && fragmentSource != nullptr);
        if (vertexSource != nullptr && fragmentSource != nullptr)
        {
            CompileOneStage(ctx, vertexSource, Fluxion::ShaderCompiler::ShaderStage::Vertex, what);
            CompileOneStage(ctx, fragmentSource, Fluxion::ShaderCompiler::ShaderStage::Fragment, what);
        }

        Fluxion_MaterialShader_FreeSource(vertexSource);
        Fluxion_MaterialShader_FreeSource(fragmentSource);
    }
}

} // namespace

extern "C" void Test_StandardMaterialCompiles_Run(TestContext* ctx)
{
    CompileMaterialForEveryPass(ctx, kStandardMaterial, "the standard material");
    CompileMaterialForEveryPass(ctx, kElaborateMaterial, "a material that changes what the library gave it");
}
