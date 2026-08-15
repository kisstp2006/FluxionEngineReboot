#include "TestFramework.h"

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/MaterialShader.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>
#include <Fluxion/ShaderCompiler/ShaderCache.hpp>
#include <filesystem>
#include <system_error>

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

// Reaches the engine's own shader library. Test_ShaderLibrary.cpp checks
// the library and its resolver directly; what is checked here is only
// that THIS path hands the resolver to the compiler. Without it the
// library could be perfect and no shader created through this interface
// would be able to include a line of it.
const char* kFragmentUsingLibrary =
    "#include \"Fluxion/Math.jsl\"\n"
    "[Uniform(Material)] Vector3 tint;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint * Saturate(FLUXION_INV_PI), 1.0);\n"
    "}\n";

// The same shader with an include nothing provides. It must fail --
// otherwise the check above would pass on a build where every include
// silently resolved to nothing.
const char* kFragmentUsingMissingInclude =
    "#include \"Fluxion/NotThere.jsl\"\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// A material as one is actually written: it describes the surface and
// stops. The pass entry point is appended by
// Fluxion_MaterialShader_BuildFragmentSource.
const char* kMaterialSource =
    "#include \"Fluxion/Surface.jsl\"\n"
    "[Input] Vector2 vUV;\n"
    "[Uniform(Material)] Vector3 baseColorFactor;\n"
    "SurfaceData EvaluateSurface() {\n"
    "  SurfaceData surface = DefaultSurface();\n"
    "  surface.baseColor = baseColorFactor * vUV.x;\n"
    "  surface.perceptualRoughness = vUV.y;\n"
    "  return surface;\n"
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

    FluxionShaderProgramDesc graphicsDesc = {};
    graphicsDesc.debugName = "Test_ShaderProgram.Graphics";
    graphicsDesc.vertexSource = kVertexSource;
    graphicsDesc.fragmentSource = kFragmentSource;
    FluxionShaderProgramHandle graphicsProgram = Fluxion_ShaderProgram_Create(fixture.device, &graphicsDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(graphicsProgram));

    FluxionShaderProgramDesc computeDesc = {};
    computeDesc.debugName = "Test_ShaderProgram.Compute";
    computeDesc.computeSource = kComputeSource;
    FluxionShaderProgramHandle computeProgram = Fluxion_ShaderProgram_Create(fixture.device, &computeDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(computeProgram));

    // Handles from two independent Create calls must never alias.
    TEST_CHECK(ctx, graphicsProgram.index != computeProgram.index || graphicsProgram.generation != computeProgram.generation);

    Fluxion_ShaderProgram_Destroy(graphicsProgram);
    Fluxion_ShaderProgram_Destroy(computeProgram);

    {
        FluxionShaderProgramDesc libraryDesc = {};
        libraryDesc.debugName = "Test_ShaderProgram.Library";
        libraryDesc.vertexSource = kVertexSource;
        libraryDesc.fragmentSource = kFragmentUsingLibrary;
        FluxionShaderProgramHandle libraryProgram = Fluxion_ShaderProgram_Create(fixture.device, &libraryDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(libraryProgram));
        if (FLUXION_HANDLE_IS_VALID(libraryProgram)) Fluxion_ShaderProgram_Destroy(libraryProgram);

        // The same material text, made into two different shaders, and
        // both handed to the real compiler. Checking only that the
        // emitted text looks right would miss a struct this language
        // accepts and the target's own compiler does not.
        for (int pass = 0; pass < FLUXION_MATERIAL_PASS_COUNT; ++pass)
        {
            char* fragment = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, (FluxionMaterialPass)pass);
            TEST_CHECK(ctx, fragment != nullptr);
            if (!fragment) continue;

            FluxionShaderProgramDesc passDesc = {};
            passDesc.debugName = "Test_ShaderProgram.MaterialPass";
            passDesc.vertexSource = kVertexSource;
            passDesc.fragmentSource = fragment;

            FluxionShaderProgramHandle passProgram = Fluxion_ShaderProgram_Create(fixture.device, &passDesc);
            TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(passProgram));
            if (FLUXION_HANDLE_IS_VALID(passProgram)) Fluxion_ShaderProgram_Destroy(passProgram);

            Fluxion_MaterialShader_FreeSource(fragment);
        }

        FluxionShaderProgramDesc missingDesc = {};
        missingDesc.debugName = "Test_ShaderProgram.MissingInclude";
        missingDesc.vertexSource = kVertexSource;
        missingDesc.fragmentSource = kFragmentUsingMissingInclude;
        FluxionShaderProgramHandle missingProgram = Fluxion_ShaderProgram_Create(fixture.device, &missingDesc);
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(missingProgram));
    }

    {
        // The cache has its own tests; what is checked here is only that
        // this path reaches it. Without this, the cache could be perfect
        // and the engine could still be building every shader from
        // scratch on every run, and every test would still pass.
        std::error_code error;
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path(error) / "FluxionShaderProgramCacheTest";
        std::filesystem::remove_all(directory, error);

        Fluxion_ShaderProgram_SetCacheDirectory(directory.string().c_str());

        FluxionShaderProgramDesc cachedDesc = {};
        cachedDesc.debugName = "Test_ShaderProgram.Cached";
        cachedDesc.vertexSource = kVertexSource;
        cachedDesc.fragmentSource = kFragmentSource;

        const Fluxion::ShaderCompiler::ShaderCacheCounters before = Fluxion::ShaderCompiler::GetShaderCacheCounters();
        FluxionShaderProgramHandle first = Fluxion_ShaderProgram_Create(fixture.device, &cachedDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(first));

        // Two stages, so two artifacts built and nothing read.
        const Fluxion::ShaderCompiler::ShaderCacheCounters afterFirst = Fluxion::ShaderCompiler::GetShaderCacheCounters();
        TEST_CHECK(ctx, afterFirst.compiled == before.compiled + 2);
        TEST_CHECK(ctx, afterFirst.loaded == before.loaded);

        FluxionShaderProgramHandle second = Fluxion_ShaderProgram_Create(fixture.device, &cachedDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(second));

        // The same two stages again, both read rather than built.
        const Fluxion::ShaderCompiler::ShaderCacheCounters afterSecond = Fluxion::ShaderCompiler::GetShaderCacheCounters();
        TEST_CHECK(ctx, afterSecond.compiled == afterFirst.compiled);
        TEST_CHECK(ctx, afterSecond.loaded == afterFirst.loaded + 2);

        Fluxion_ShaderProgram_Destroy(first);
        Fluxion_ShaderProgram_Destroy(second);

        // Switched off again, so nothing after this test is affected by
        // what this test did -- and switching off has to work as well.
        Fluxion_ShaderProgram_SetCacheDirectory(nullptr);

        FluxionShaderProgramHandle uncached = Fluxion_ShaderProgram_Create(fixture.device, &cachedDesc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(uncached));
        const Fluxion::ShaderCompiler::ShaderCacheCounters afterOff = Fluxion::ShaderCompiler::GetShaderCacheCounters();
        TEST_CHECK(ctx, afterOff.loaded == afterSecond.loaded);
        TEST_CHECK(ctx, afterOff.compiled == afterSecond.compiled + 2);
        Fluxion_ShaderProgram_Destroy(uncached);

        std::filesystem::remove_all(directory, error);
    }

    DestroyNullRHIFixture(fixture);
}
