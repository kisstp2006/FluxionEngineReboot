#include "TestFramework.h"

#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
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

    FluxionRHIDeviceDesc deviceDesc = {};
    fixture.device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    return fixture;
}

void DestroyNullRHIFixture(const NullRHIFixture& fixture)
{
    Fluxion_RHI_DestroyDevice(fixture.device);
    Fluxion_RHI_DestroyInstance(fixture.instance);
}

const char* kVertex =
    "[Input] Vector3 position;\n"
    "[Output] Vector4 Position;\n"
    "void main() {\n"
    "  Position = Vector4(position, 1.0);\n"
    "}\n";

// The material side of these three is deliberately identical -- one
// Vector3 called `tint` -- so a reload between them is allowed. Only the
// arithmetic differs.
const char* kFragment =
    "[Uniform(Material)] Vector3 tint;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint, 1.0);\n"
    "}\n";

const char* kFragmentDifferentBody =
    "[Uniform(Material)] Vector3 tint;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint * 0.5, 1.0);\n"
    "}\n";

// A second uniform: every material built against the first holds a buffer
// sized for one Vector3 and an offset table that knows nothing about
// this. Reloading into it has to be refused.
const char* kFragmentExtraUniform =
    "[Uniform(Material)] Vector3 tint;\n"
    "[Uniform(Material)] float strength;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint * strength, 1.0);\n"
    "}\n";

const char* kFragmentBroken =
    "[Uniform(Material)] Vector3 tint;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint, ;\n"
    "}\n";

FluxionShaderProgramDesc MakeDesc(const char* fragment)
{
    FluxionShaderProgramDesc desc = {};
    desc.debugName = "Test_ShaderReload.Program";
    desc.vertexSource = kVertex;
    desc.fragmentSource = fragment;
    return desc;
}

} // namespace

extern "C" void Test_ShaderReload_Run(TestContext* ctx)
{
    // Same dependency and the same soft skip as the other RenderCore
    // checks that compile a shader.
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        std::fprintf(stderr, "  SKIP: dxc not found on this machine -- skipping shader reload checks\n");
        return;
    }

    NullRHIFixture fixture = CreateNullRHIFixture();

    FluxionShaderProgramDesc desc = MakeDesc(kFragment);
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(fixture.device, &desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));

    if (FLUXION_HANDLE_IS_VALID(program))
    {
        // The handle is the point. A reload that produced a new one would
        // leave every pipeline and material in the scene pointing at
        // nothing, and no amount of correct compiling would make up for
        // it -- so this is checked after every single reload below.
        const u32 index = program.index;
        const u32 generation = program.generation;

        FluxionShaderProgramDesc same = MakeDesc(kFragment);
        TEST_CHECK(ctx, Fluxion_ShaderProgram_Reload(fixture.device, program, &same) == FLUXION_SHADER_PROGRAM_RELOAD_OK);
        TEST_CHECK(ctx, program.index == index && program.generation == generation);

        FluxionShaderProgramDesc changed = MakeDesc(kFragmentDifferentBody);
        TEST_CHECK(ctx, Fluxion_ShaderProgram_Reload(fixture.device, program, &changed) == FLUXION_SHADER_PROGRAM_RELOAD_OK);
        TEST_CHECK(ctx, program.index == index && program.generation == generation);

        // A source that does not compile changes nothing. The program is
        // still the one that was working a moment ago, and it is still
        // usable -- which the next successful reload proves.
        FluxionShaderProgramDesc broken = MakeDesc(kFragmentBroken);
        TEST_CHECK(ctx, Fluxion_ShaderProgram_Reload(fixture.device, program, &broken) == FLUXION_SHADER_PROGRAM_RELOAD_COMPILE_FAILED);
        TEST_CHECK(ctx, program.index == index && program.generation == generation);

        // A source whose material parameters differ is refused, for the
        // same reason: what is running stays running.
        FluxionShaderProgramDesc extra = MakeDesc(kFragmentExtraUniform);
        TEST_CHECK(ctx, Fluxion_ShaderProgram_Reload(fixture.device, program, &extra) == FLUXION_SHADER_PROGRAM_RELOAD_LAYOUT_CHANGED);
        TEST_CHECK(ctx, program.index == index && program.generation == generation);

        // Still alive and still reloadable after two refusals -- a
        // refusal that quietly damaged the program would show up here.
        TEST_CHECK(ctx, Fluxion_ShaderProgram_Reload(fixture.device, program, &same) == FLUXION_SHADER_PROGRAM_RELOAD_OK);

        // A compute source is a different shape, not a new version of
        // this one.
        FluxionShaderProgramDesc computeShaped = {};
        computeShaped.debugName = "Test_ShaderReload.Compute";
        computeShaped.computeSource =
            "[Buffer(Object)] float brightness;\n"
            "void main() {\n"
            "  brightness[ThreadID] = 0.5;\n"
            "}\n";
        TEST_CHECK(ctx, Fluxion_ShaderProgram_Reload(fixture.device, program, &computeShaped) == FLUXION_SHADER_PROGRAM_RELOAD_INVALID_REQUEST);
    }

    if (FLUXION_HANDLE_IS_VALID(program))
    {
        // A pipeline naming this program, so the invalidation walk has a
        // matching record to find rather than iterating over nothing.
        FluxionRenderPipelineHandle pipeline = Fluxion_RenderPipeline_Create(fixture.device, program,
            FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, FLUXION_RHI_FORMAT_D32_FLOAT);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(pipeline));

        // Reloading over and over must leave nothing behind. Each reload
        // builds two new shaders and lets two go; if the old ones were
        // kept, the eighty rounds below would ask for a hundred and sixty
        // out of a pool of a hundred and twenty-eight and start failing
        // partway through. Nothing announces a leak -- running out is
        // what turns it into a visible failure, which is why the count is
        // chosen to exceed the pool rather than merely to be large.
        bool held = true;
        FluxionShaderProgramDesc same = MakeDesc(kFragment);
        for (int i = 0; i < 80 && held; ++i)
        {
            if (Fluxion_ShaderProgram_Reload(fixture.device, program, &same) != FLUXION_SHADER_PROGRAM_RELOAD_OK) held = false;
        }
        TEST_CHECK(ctx, held);

        if (FLUXION_HANDLE_IS_VALID(pipeline)) Fluxion_RenderPipeline_Destroy(pipeline);
    }

    if (FLUXION_HANDLE_IS_VALID(program))
    {
        // The same reload, taken off the frame. Run first with no job
        // system at all: a host that never started one must still get the
        // work done, and the only difference it may see is that the answer
        // is already waiting.
        FluxionShaderProgramDesc changed = MakeDesc(kFragmentDifferentBody);
        FluxionShaderProgramReloadJob* job = Fluxion_ShaderProgram_BeginReload(fixture.device, program, &changed);
        TEST_CHECK(ctx, job != nullptr);
        if (job != nullptr)
        {
            TEST_CHECK(ctx, Fluxion_ShaderProgram_IsReloadReady(job));
            TEST_CHECK(ctx, Fluxion_ShaderProgram_FinishReload(job) == FLUXION_SHADER_PROGRAM_RELOAD_OK);
        }

        // And again with workers, which is the arrangement this exists
        // for. Asking before the answer is ready has to be allowed and
        // has to not block -- that is the whole point of asking.
        Fluxion_JobSystem_Init(2, false);
        {
            FluxionShaderProgramDesc same = MakeDesc(kFragment);
            FluxionShaderProgramReloadJob* threaded = Fluxion_ShaderProgram_BeginReload(fixture.device, program, &same);
            TEST_CHECK(ctx, threaded != nullptr);
            if (threaded != nullptr)
            {
                // However this comes out, it must come out without
                // waiting, and Finish must cope with either answer.
                (void)Fluxion_ShaderProgram_IsReloadReady(threaded);
                TEST_CHECK(ctx, Fluxion_ShaderProgram_FinishReload(threaded) == FLUXION_SHADER_PROGRAM_RELOAD_OK);
            }

            // A refusal has to survive the trip too: the reason is worked
            // out when the result is applied, not when it is started, so
            // this is the path where that could have been lost.
            FluxionShaderProgramDesc extra = MakeDesc(kFragmentExtraUniform);
            FluxionShaderProgramReloadJob* refused = Fluxion_ShaderProgram_BeginReload(fixture.device, program, &extra);
            TEST_CHECK(ctx, refused != nullptr);
            if (refused != nullptr)
            {
                TEST_CHECK(ctx, Fluxion_ShaderProgram_FinishReload(refused) == FLUXION_SHADER_PROGRAM_RELOAD_LAYOUT_CHANGED);
            }

            // So does a source that does not compile.
            FluxionShaderProgramDesc broken = MakeDesc(kFragmentBroken);
            FluxionShaderProgramReloadJob* failing = Fluxion_ShaderProgram_BeginReload(fixture.device, program, &broken);
            TEST_CHECK(ctx, failing != nullptr);
            if (failing != nullptr)
            {
                TEST_CHECK(ctx, Fluxion_ShaderProgram_FinishReload(failing) == FLUXION_SHADER_PROGRAM_RELOAD_COMPILE_FAILED);
            }
        }
        Fluxion_JobSystem_Shutdown();

        // Starting a reload of something that is not a program is a
        // refusal, not a crash and not a job left running.
        FluxionShaderProgramDesc same = MakeDesc(kFragment);
        FluxionShaderProgramHandle bogus = { 0, 9999 };
        TEST_CHECK(ctx, Fluxion_ShaderProgram_BeginReload(fixture.device, bogus, &same) == nullptr);
    }

    if (FLUXION_HANDLE_IS_VALID(program)) Fluxion_ShaderProgram_Destroy(program);
    DestroyNullRHIFixture(fixture);
}
