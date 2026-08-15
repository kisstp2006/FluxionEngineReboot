#include "TestFramework.h"

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/MaterialParameters.h>
#include <Fluxion/RenderCore/Renderer/MaterialShader.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>
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

// A material that says nothing beyond "these are my values".
const char* const kStandardMaterial =
    "#include \"Fluxion/Material.jsl\"\n"
    "SurfaceData EvaluateSurface() { return StandardSurface(); }\n";

// One that uses a subset. Perfectly ordinary, and the engine has to say
// which parameters it has rather than assuming all of them.
const char* const kSparseMaterial =
    "#include \"Fluxion/Surface.jsl\"\n"
    "[Uniform(Material)] Vector4 baseColorFactor;\n"
    "SurfaceData EvaluateSurface() {\n"
    "  SurfaceData surface = DefaultSurface();\n"
    "  surface.baseColor = baseColorFactor.xyz;\n"
    "  return surface;\n"
    "}\n";

// One that uses an engine parameter name to mean something else.
const char* const kWrongKindMaterial =
    "#include \"Fluxion/Surface.jsl\"\n"
    "[Uniform(Material)] Vector3 metallicFactor;\n"
    "SurfaceData EvaluateSurface() {\n"
    "  SurfaceData surface = DefaultSurface();\n"
    "  surface.metallic = metallicFactor.x;\n"
    "  return surface;\n"
    "}\n";

const char* const kVertexSource =
    "[Input] Vector3 position;\n"
    "[Output] Vector4 Position;\n"
    "void main() {\n"
    "  Position = Vector4(position, 1.0);\n"
    "}\n";

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

FluxionShaderProgramHandle CreateProgram(FluxionRHIDeviceHandle device, const char* material, const char* debugName)
{
    char* fragment = Fluxion_MaterialShader_BuildFragmentSource(material, FLUXION_MATERIAL_PASS_FORWARD);
    if (!fragment) return FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionShaderProgramDesc desc = {};
    desc.debugName = debugName;
    desc.vertexSource = kVertexSource;
    desc.fragmentSource = fragment;

    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(device, &desc);
    Fluxion_MaterialShader_FreeSource(fragment);
    return program;
}

// The shader-side spelling of what the ENGINE says each parameter is.
//
// Deliberately derived from Fluxion_Material_GetParameterType rather than
// written out again here: a third table would only be a third thing that
// can be wrong, and the point of the check below is to hold the engine's
// own table against the shader library's, not to hold both against the
// test's opinion.
TypeKind ExpectedShaderType(FluxionMaterialParameter parameter)
{
    switch (Fluxion_Material_GetParameterType(parameter))
    {
        case FLUXION_MATERIAL_PARAM_TYPE_VEC3: return TypeKind::Vec3;
        case FLUXION_MATERIAL_PARAM_TYPE_VEC4: return TypeKind::Vec4;
        default: return TypeKind::Float;
    }
}

void TheNamesAreASetOfDistinctStrings(TestContext* ctx)
{
    for (int i = 0; i < FLUXION_MATERIAL_PARAM_COUNT; ++i)
    {
        const char* name = Fluxion_Material_GetParameterName((FluxionMaterialParameter)i);
        TEST_CHECK(ctx, name != nullptr && name[0] != '\0');

        for (int j = i + 1; j < FLUXION_MATERIAL_PARAM_COUNT; ++j)
        {
            const char* other = Fluxion_Material_GetParameterName((FluxionMaterialParameter)j);
            TEST_CHECK(ctx, name == nullptr || other == nullptr || std::strcmp(name, other) != 0);
        }
    }

    for (int i = 0; i < FLUXION_MATERIAL_TEXTURE_COUNT; ++i)
    {
        const char* name = Fluxion_Material_GetTextureSlotName((FluxionMaterialTextureSlot)i);
        TEST_CHECK(ctx, name != nullptr && name[0] != '\0');
    }

    TEST_CHECK(ctx, Fluxion_Material_GetParameterName(FLUXION_MATERIAL_PARAM_COUNT) == nullptr);
    TEST_CHECK(ctx, Fluxion_Material_GetTextureSlotName(FLUXION_MATERIAL_TEXTURE_COUNT) == nullptr);
}

// The one duplication in the whole arrangement: these names exist in
// Material.cpp and in Fluxion/Material.jsl. Nothing but this connects
// them, so this is what stops them drifting -- a renamed parameter on one
// side and not the other would otherwise be a setter that silently stops
// working.
void EveryNameIsDeclaredByTheShaderLibrary(TestContext* ctx)
{
    char* source = Fluxion_MaterialShader_BuildFragmentSource(kStandardMaterial, FLUXION_MATERIAL_PASS_FORWARD);
    TEST_CHECK(ctx, source != nullptr);
    if (!source) return;

    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<Test_MaterialParameters>";
    options.includeResolver = Fluxion::RenderCore::MakeShaderLibraryResolver();

    DiagnosticList diagnostics;
    auto result = Compile(source, options, diagnostics);
    Fluxion_MaterialShader_FreeSource(source);

    if (!result.IsOk())
    {
        for (const Diagnostic& d : diagnostics.entries)
            std::fprintf(stderr, "    %s:%u: %s\n", d.location.file.c_str(), d.location.line, d.message.c_str());
    }
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk()) return;

    for (int i = 0; i < FLUXION_MATERIAL_PARAM_COUNT; ++i)
    {
        const char* wanted = Fluxion_Material_GetParameterName((FluxionMaterialParameter)i);
        bool found = false;

        for (const IRUniformBufferBinding& buffer : result.Value().reflection.uniformBuffers)
        {
            if (buffer.group != BindingGroup::Material) continue;
            for (const IRUniformBufferMember& member : buffer.members)
            {
                if (member.name != wanted) continue;
                found = true;

                // The same kind on both sides, not merely the same name.
                // A base colour declared as a single number would be
                // found here and would then be written four times its
                // size.
                TEST_CHECK(ctx, member.type.kind == ExpectedShaderType((FluxionMaterialParameter)i));
            }
        }

        if (!found) std::fprintf(stderr, "    the shader library declares no '%s'\n", wanted);
        TEST_CHECK(ctx, found);
    }
}

void AlphaModeIsRememberedAndDefaultsToTheCheapest(TestContext* ctx)
{
    // A handle that names nothing answers opaque rather than putting a
    // dead material into the blended pass.
    FluxionMaterialHandle nothing = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    TEST_CHECK(ctx, Fluxion_Material_GetAlphaMode(nothing) == FLUXION_MATERIAL_ALPHA_OPAQUE);
    Fluxion_Material_SetAlphaMode(nothing, FLUXION_MATERIAL_ALPHA_BLEND);
    TEST_CHECK(ctx, Fluxion_Material_GetAlphaMode(nothing) == FLUXION_MATERIAL_ALPHA_OPAQUE);

    TEST_CHECK(ctx, !Fluxion_Material_HasParameter(nothing, FLUXION_MATERIAL_PARAM_BASE_COLOR));
    TEST_CHECK(ctx, !Fluxion_Material_SetMetallic(nothing, 1.0f));
}

// Everything below needs a real shader, and therefore the external
// compiler. Skipped where it is not installed, same as every other check
// in this suite that reaches it.
void TheEngineKnowsWhatAMaterialHas(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        std::fprintf(stderr, "  SKIP: dxc not found on this machine -- skipping the material parameter checks\n");
        return;
    }

    NullRHIFixture fixture = CreateNullRHIFixture();

    {
        FluxionShaderProgramHandle program = CreateProgram(fixture.device, kStandardMaterial, "Test_MaterialParameters.Standard");
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));

        if (FLUXION_HANDLE_IS_VALID(program))
        {
            FluxionMaterialHandle material = Fluxion_Material_Create(fixture.device, program);
            TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(material));

            if (FLUXION_HANDLE_IS_VALID(material))
            {
                // A standard material has all of them, and every setter
                // reaches something.
                for (int i = 0; i < FLUXION_MATERIAL_PARAM_COUNT; ++i)
                    TEST_CHECK(ctx, Fluxion_Material_HasParameter(material, (FluxionMaterialParameter)i));

                TEST_CHECK(ctx, Fluxion_Material_SetBaseColor(material, FluxionVec4{ 1.0f, 0.5f, 0.25f, 0.75f }));
                TEST_CHECK(ctx, Fluxion_Material_SetMetallic(material, 1.0f));
                TEST_CHECK(ctx, Fluxion_Material_SetRoughness(material, 0.4f));
                TEST_CHECK(ctx, Fluxion_Material_SetReflectance(material, 0.35f));
                TEST_CHECK(ctx, Fluxion_Material_SetEmissive(material, FluxionVec3{ 2.0f, 0.0f, 0.0f }));
                TEST_CHECK(ctx, Fluxion_Material_SetNormalScale(material, 1.0f));
                TEST_CHECK(ctx, Fluxion_Material_SetOcclusionStrength(material, 1.0f));
                TEST_CHECK(ctx, Fluxion_Material_SetAlphaCutoff(material, 0.5f));

                // Alpha mode is remembered and is not any of the shader's
                // business.
                TEST_CHECK(ctx, Fluxion_Material_GetAlphaMode(material) == FLUXION_MATERIAL_ALPHA_OPAQUE);
                Fluxion_Material_SetAlphaMode(material, FLUXION_MATERIAL_ALPHA_MASK);
                TEST_CHECK(ctx, Fluxion_Material_GetAlphaMode(material) == FLUXION_MATERIAL_ALPHA_MASK);

                // Every one of the engine's texture slots is there,
                // because the standard surface samples every one of them
                // unconditionally -- a map it did not sample would be
                // removed by the compiler and then missing from the bind
                // group the pipeline was built to expect.
                for (int i = 0; i < FLUXION_MATERIAL_TEXTURE_COUNT; ++i)
                    TEST_CHECK(ctx, Fluxion_Material_HasTextureSlot(material, (FluxionMaterialTextureSlot)i));

                Fluxion_Material_Destroy(material);
            }

            Fluxion_ShaderProgram_Destroy(program);
        }
    }

    {
        FluxionShaderProgramHandle program = CreateProgram(fixture.device, kSparseMaterial, "Test_MaterialParameters.Sparse");
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));

        if (FLUXION_HANDLE_IS_VALID(program))
        {
            FluxionMaterialHandle material = Fluxion_Material_Create(fixture.device, program);
            TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(material));

            if (FLUXION_HANDLE_IS_VALID(material))
            {
                TEST_CHECK(ctx, Fluxion_Material_HasParameter(material, FLUXION_MATERIAL_PARAM_BASE_COLOR));
                TEST_CHECK(ctx, Fluxion_Material_SetBaseColor(material, FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f }));

                // What it does not declare, it does not have -- and
                // setting it answers false rather than writing into
                // somebody else's bytes.
                TEST_CHECK(ctx, !Fluxion_Material_HasParameter(material, FLUXION_MATERIAL_PARAM_METALLIC));
                TEST_CHECK(ctx, !Fluxion_Material_SetMetallic(material, 1.0f));
                TEST_CHECK(ctx, !Fluxion_Material_SetEmissive(material, FluxionVec3{ 1.0f, 1.0f, 1.0f }));

                Fluxion_Material_Destroy(material);
            }

            Fluxion_ShaderProgram_Destroy(program);
        }
    }

    {
        // An engine parameter name used for something else. Refused when
        // the material is created, because the alternative is a metallic
        // value that can never be set and never says why.
        FluxionShaderProgramHandle program = CreateProgram(fixture.device, kWrongKindMaterial, "Test_MaterialParameters.WrongKind");
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));

        if (FLUXION_HANDLE_IS_VALID(program))
        {
            FluxionMaterialHandle material = Fluxion_Material_Create(fixture.device, program);
            TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(material));

            Fluxion_ShaderProgram_Destroy(program);
        }
    }

    DestroyNullRHIFixture(fixture);
}

} // namespace

extern "C" void Test_MaterialParameters_Run(TestContext* ctx)
{
    std::fprintf(stderr, "  Test_MaterialParameters\n");

    TheNamesAreASetOfDistinctStrings(ctx);
    EveryNameIsDeclaredByTheShaderLibrary(ctx);
    AlphaModeIsRememberedAndDefaultsToTheCheapest(ctx);
    TheEngineKnowsWhatAMaterialHas(ctx);
}
