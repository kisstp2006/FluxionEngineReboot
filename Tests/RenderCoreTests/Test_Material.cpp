#include "TestFramework.h"

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
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

// Reflects one MATERIAL-group uniform (tint, Vector3) and no textures --
// enough to exercise SetVec3/SetFloat's name+kind matching without
// needing a real texture/sampler round trip too.
const char* kFragmentSource =
    "[Uniform(Material)] Vector3 tint;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint, 1.0);\n"
    "}\n";

} // namespace

extern "C" void Test_Material_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        std::fprintf(stderr, "  SKIP: dxc not found on this machine -- skipping Material checks\n");
        return;
    }

    NullRHIFixture fixture = CreateNullRHIFixture();

    FluxionShaderProgramDesc programDesc = { 0 };
    programDesc.debugName = "Test_Material.Program";
    programDesc.vertexSource = kVertexSource;
    programDesc.fragmentSource = kFragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(fixture.device, &programDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));

    FluxionMaterialHandle material = Fluxion_Material_Create(fixture.device, program);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(material));

    // "tint" is a Vector3 (Material group) -- SetVec3 must succeed,
    // SetFloat/SetVec4 on the same name must not (wrong kind), and any
    // name the shader never declared must fail regardless of kind.
    TEST_CHECK(ctx, Fluxion_Material_SetVec3(material, "tint", FluxionVec3{ 1.0f, 0.5f, 0.25f }));
    TEST_CHECK(ctx, !Fluxion_Material_SetFloat(material, "tint", 1.0f));
    TEST_CHECK(ctx, !Fluxion_Material_SetVec4(material, "tint", FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f }));
    TEST_CHECK(ctx, !Fluxion_Material_SetVec3(material, "doesNotExist", FluxionVec3{ 0.0f, 0.0f, 0.0f }));

    FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHISamplerHandle noSampler = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    TEST_CHECK(ctx, !Fluxion_Material_SetTexture(material, "tint", noView, noSampler)); // "tint" is a uniform, not a texture

    Fluxion_Material_FlushDirty(material);
    Fluxion_Material_FlushDirty(material); // a second, no-op flush must not assert or crash

    Fluxion_Material_Destroy(material);
    Fluxion_ShaderProgram_Destroy(program);

    DestroyNullRHIFixture(fixture);
}
