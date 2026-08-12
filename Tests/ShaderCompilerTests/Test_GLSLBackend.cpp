#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

using namespace Fluxion::ShaderCompiler;

void Test_GLSLBackend_Run(TestContext& ctx)
{
    const char* source =
        "[Uniform] Vector3 color;\n"
        "[Target(0)] Vector4 outColor;\n"
        "[Input] Vector2 uv;\n"
        "void main() { return Vector4(color * uv.x, 1.0); }\n";

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<test>";

    auto result = Compile(source, options, diagnostics);
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    const std::string& glsl = result.Value().glslSource;
    TEST_CHECK(ctx, glsl.find("#version 450 core") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("layout(location = 0) out vec4 outColor") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("layout(location = 0) in vec2 uv") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("outColor = ") != std::string::npos); // return -> target assignment
}
