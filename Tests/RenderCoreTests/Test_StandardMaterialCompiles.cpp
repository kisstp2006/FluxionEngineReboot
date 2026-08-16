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
