#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

using namespace Fluxion::ShaderCompiler;

void Test_ComputeStorageBuffer_Run(TestContext& ctx)
{
    const char* source =
        "[Buffer(Object)] float data;\n"
        "void main() { data[ThreadID] = data[ThreadID] * 2.0; }\n";

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = ShaderStage::Compute;
    options.fileName = "<test>";

    auto result = Compile(source, options, diagnostics);
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    const ShaderIRModule& ir = result.Value().reflection;

    // The Object-group storage buffer has no uniform buffer sharing its
    // group, so it starts numbering from binding 0.
    TEST_CHECK(ctx, ir.storageBuffers.size() == 1);
    if (ir.storageBuffers.size() == 1)
    {
        TEST_CHECK(ctx, ir.storageBuffers[0].name == "data");
        TEST_CHECK(ctx, ir.storageBuffers[0].group == BindingGroup::Object);
        TEST_CHECK(ctx, ir.storageBuffers[0].binding == 0);
    }

    const std::string& glsl = result.Value().glslSource;
    TEST_CHECK(ctx, glsl.find("layout(std430,") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("gl_GlobalInvocationID.x") != std::string::npos);
    TEST_CHECK(ctx, glsl.find("local_size_x = 64") != std::string::npos);

    const std::string& hlsl = result.Value().hlslSource;
    TEST_CHECK(ctx, hlsl.find("RWStructuredBuffer<float>") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("numthreads(64") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("SV_DispatchThreadID") != std::string::npos);
}
