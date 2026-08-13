#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

using namespace Fluxion::ShaderCompiler;

void Test_HLSLBackend_Run(TestContext& ctx)
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

    const std::string& hlsl = result.Value().hlslSource;
    // [Uniform] with no explicit group defaults to Material (set 2).
    TEST_CHECK(ctx, hlsl.find("cbuffer GroupMaterialConstants") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("[[vk::binding(0, 2)]]") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("register(b0, space2)") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("SV_Target0") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("shaderMain") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("float3") != std::string::npos); // Vector3 -> float3
}

void Test_HLSLBackend_EntryReturn_Run(TestContext& ctx)
{
    // A real `return expr;` inside the entry function must route to the
    // designated [Target(N)] -- not emit `return value;` inside what
    // HLSL sees as a void shaderMain() (which would be invalid HLSL).
    const char* source =
        "[Input] Vector3 vColor;\n"
        "[Target(0)] Vector4 fragColor;\n"
        "void main() { return Vector4(vColor, 1.0); }\n";

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<test>";

    auto result = Compile(source, options, diagnostics);
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    const std::string& hlsl = result.Value().hlslSource;
    TEST_CHECK(ctx, hlsl.find("fragColor = ") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("return Vector4") == std::string::npos);
    TEST_CHECK(ctx, hlsl.find("return float4") == std::string::npos);
}
