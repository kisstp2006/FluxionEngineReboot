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

FluxionRHIVertexLayout MakePositionOnlyLayout()
{
    FluxionRHIVertexLayout layout = {};
    layout.attributeCount = 1;
    layout.attributes[0].location = 0;
    layout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    layout.attributes[0].offset = 0;
    layout.stride = 12;
    return layout;
}

bool SameHandle(FluxionRHIPipelineHandle a, FluxionRHIPipelineHandle b)
{
    return a.index == b.index && a.generation == b.generation;
}

} // namespace

// The lazily-built pipeline cache has no public reader -- resolving is
// something a draw does, not something a caller asks for. Declared here
// rather than published in a header, because nothing outside RenderCore
// should be building pipelines out of band; the checks below only need to
// observe which object a resolve hands back.
extern "C" FluxionRHIPipelineHandle FluxionRendererInternal_RenderPipeline_Resolve(FluxionRenderPipelineHandle pipeline, FluxionRHIDeviceHandle device, const FluxionRHIVertexLayout* vertexLayout);

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

        // A pipeline object bakes in the shaders it was built from and
        // never consults them again, so one that outlives a reload is
        // drawing with shaders that have been destroyed. Nothing about
        // that is visible from the outside -- the handle stays valid and
        // the draw still submits -- so the only way to catch it is to
        // watch which object a resolve returns.
        const FluxionRHIVertexLayout layout = MakePositionOnlyLayout();
        const FluxionRHIPipelineHandle built = FluxionRendererInternal_RenderPipeline_Resolve(pipeline, fixture.device, &layout);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(built));

        // Same layout, no reload in between: the cache is doing its job
        // and hands back the object it already built. Without this the
        // check below would pass just as well against a cache that
        // rebuilds on every single draw.
        TEST_CHECK(ctx, SameHandle(FluxionRendererInternal_RenderPipeline_Resolve(pipeline, fixture.device, &layout), built));

        FluxionShaderProgramDesc afterReload = MakeDesc(kFragmentDifferentBody);
        TEST_CHECK(ctx, Fluxion_ShaderProgram_Reload(fixture.device, program, &afterReload) == FLUXION_SHADER_PROGRAM_RELOAD_OK);

        const FluxionRHIPipelineHandle rebuilt = FluxionRendererInternal_RenderPipeline_Resolve(pipeline, fixture.device, &layout);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(rebuilt));
        TEST_CHECK(ctx, !SameHandle(rebuilt, built));

        // The other half of the contract: a reload that was refused must
        // not cost anything either. Throwing the variants away on a
        // refusal would be silently wasteful rather than wrong, which is
        // exactly the kind of thing that survives unnoticed.
        FluxionShaderProgramDesc refused = MakeDesc(kFragmentExtraUniform);
        TEST_CHECK(ctx, Fluxion_ShaderProgram_Reload(fixture.device, program, &refused) == FLUXION_SHADER_PROGRAM_RELOAD_LAYOUT_CHANGED);
        TEST_CHECK(ctx, SameHandle(FluxionRendererInternal_RenderPipeline_Resolve(pipeline, fixture.device, &layout), rebuilt));

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
