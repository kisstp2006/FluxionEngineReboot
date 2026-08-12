#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

using namespace Fluxion::ShaderCompiler;

void Test_BindingGroups_Run(TestContext& ctx)
{
    const char* source =
        "[Uniform(Frame)] Matrix4x4 viewProjection;\n"
        "[Uniform(Material)] float exposure;\n"
        "[Texture(Material)] Texture2D albedoMap;\n"
        "[Input] Vector2 uv;\n"
        "[Target(0)] Vector4 outColor;\n"
        "void main() { return Vector4(exposure, exposure, exposure, 1.0); }\n";

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<test>";

    auto result = Compile(source, options, diagnostics);
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    const ShaderIRModule& ir = result.Value().reflection;

    // Two uniform buffers: Frame (set 1) with viewProjection, Material
    // (set 2) with exposure -- each group's non-opaque uniforms are
    // merged into exactly one buffer, not flattened across groups.
    TEST_CHECK(ctx, ir.uniformBuffers.size() == 2);
    bool foundFrame = false, foundMaterial = false;
    for (const IRUniformBufferBinding& ub : ir.uniformBuffers)
    {
        if (ub.group == BindingGroup::Frame)
        {
            foundFrame = true;
            TEST_CHECK(ctx, ub.members.size() == 1 && ub.members[0].name == "viewProjection");
        }
        if (ub.group == BindingGroup::Material)
        {
            foundMaterial = true;
            TEST_CHECK(ctx, ub.members.size() == 1 && ub.members[0].name == "exposure");
        }
    }
    TEST_CHECK(ctx, foundFrame && foundMaterial);

    // The Material-group texture starts at binding 1 (0 is taken by the
    // Material uniform buffer), with its paired sampler at binding 2.
    TEST_CHECK(ctx, ir.resources.size() == 1);
    TEST_CHECK(ctx, ir.resources[0].group == BindingGroup::Material);
    TEST_CHECK(ctx, ir.resources[0].binding == 1);
    TEST_CHECK(ctx, ir.resources[0].samplerBinding == 2);

    const std::string& hlsl = result.Value().hlslSource;
    TEST_CHECK(ctx, hlsl.find("cbuffer GroupFrameConstants") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("register(b0, space1)") != std::string::npos); // Frame == set 1
    TEST_CHECK(ctx, hlsl.find("cbuffer GroupMaterialConstants") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("register(b0, space2)") != std::string::npos); // Material == set 2
    TEST_CHECK(ctx, hlsl.find("register(t1, space2)") != std::string::npos); // albedoMap
    TEST_CHECK(ctx, hlsl.find("register(s2, space2)") != std::string::npos); // albedoMap_sampler

    // GLSL has no `set` qualifier a real OpenGL driver accepts -- the
    // GLSL backend instead flattens every group into one GL binding-point
    // namespace with a fixed per-group stride (16, OpenGLFlatBinding in
    // GLSLBackend.cpp): Frame (group 1) starts at binding 16, Material
    // (group 2) at binding 32; the Material texture takes local binding 1
    // within its group (0 is the Material uniform buffer), so 32 + 1 = 33.
    const std::string& glsl = result.Value().glslSource;
    TEST_CHECK(ctx, glsl.find("layout(binding = 16) uniform GroupFrameBlock") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("layout(binding = 32) uniform GroupMaterialBlock") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("layout(binding = 33) uniform sampler2D albedoMap") != std::string::npos);
}

void Test_BindingGroups_DefaultIsMaterial_Run(TestContext& ctx)
{
    // [Uniform]/[Texture] with no explicit group argument default to
    // Material -- the most common case for a per-material constant.
    const char* source =
        "[Uniform] float roughness;\n"
        "[Target(0)] Vector4 outColor;\n"
        "void main() { return Vector4(roughness, roughness, roughness, 1.0); }\n";

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<test>";

    auto result = Compile(source, options, diagnostics);
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    const ShaderIRModule& ir = result.Value().reflection;
    TEST_CHECK(ctx, ir.uniformBuffers.size() == 1);
    if (ir.uniformBuffers.size() == 1)
        TEST_CHECK(ctx, ir.uniformBuffers[0].group == BindingGroup::Material);
}
