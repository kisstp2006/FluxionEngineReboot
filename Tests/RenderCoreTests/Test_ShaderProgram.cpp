#include "TestFramework.h"

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstdio>

namespace
{

struct NullRHIFixture
{
    FluxionRHIInstanceHandle instance;
    FluxionRHIDeviceHandle device;
};

NullRHIFixture CreateNullRHIFixture()
{
    NullRHIFixture fixture;
    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    fixture.instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);

    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(fixture.instance, adapters, 1);

    FluxionRHIDeviceDesc deviceDesc = { 0 };
    fixture.device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    return fixture;
}

void DestroyNullRHIFixture(const NullRHIFixture& fixture)
{
    Fluxion_RHI_DestroyDevice(fixture.device);
    Fluxion_RHI_DestroyInstance(fixture.instance);
}

const char* kVertexSource =
    "[Input] Vector3 position;\n"
    "[Output] Vector4 Position;\n"
    "void main() {\n"
    "  Position = Vector4(position, 1.0);\n"
    "}\n";

const char* kFragmentSource =
    "[Uniform(Material)] Vector3 tint;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint, 1.0);\n"
    "}\n";

const char* kComputeSource =
    "[Buffer(Object)] float brightness;\n"
    "void main() {\n"
    "  brightness[ThreadID] = 0.5;\n"
    "}\n";

} // namespace

extern "C" void Test_ShaderProgram_Run(TestContext* ctx)
{
    // ShaderProgram compiles through dxc (see ShaderProgram.cpp) -- on a
    // machine without it on PATH (e.g. a Linux CI image with no Vulkan
    // SDK), skip rather than fail, matching Test_DXCAdapter.cpp's own
    // precedent for the same underlying dependency.
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        std::fprintf(stderr, "  SKIP: dxc not found on this machine -- skipping ShaderProgram checks\n");
        return;
    }

    NullRHIFixture fixture = CreateNullRHIFixture();

    FluxionShaderProgramDesc graphicsDesc = { 0 };
    graphicsDesc.debugName = "Test_ShaderProgram.Graphics";
    graphicsDesc.vertexSource = kVertexSource;
    graphicsDesc.fragmentSource = kFragmentSource;
    FluxionShaderProgramHandle graphicsProgram = Fluxion_ShaderProgram_Create(fixture.device, &graphicsDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(graphicsProgram));

    FluxionShaderProgramDesc computeDesc = { 0 };
    computeDesc.debugName = "Test_ShaderProgram.Compute";
    computeDesc.computeSource = kComputeSource;
    FluxionShaderProgramHandle computeProgram = Fluxion_ShaderProgram_Create(fixture.device, &computeDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(computeProgram));

    // Handles from two independent Create calls must never alias.
    TEST_CHECK(ctx, graphicsProgram.index != computeProgram.index || graphicsProgram.generation != computeProgram.generation);

    Fluxion_ShaderProgram_Destroy(graphicsProgram);
    Fluxion_ShaderProgram_Destroy(computeProgram);

    DestroyNullRHIFixture(fixture);
}
