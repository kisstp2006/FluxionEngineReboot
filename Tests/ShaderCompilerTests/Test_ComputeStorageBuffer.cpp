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

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <cstdio>

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

// Sampling at a level the shader names.
//
// This is what a compute shader has to use -- a dispatch has no
// neighbouring pixels to work a level out from -- and it is the one
// sampling call the two languages disagree about by more than a name.
// It went unchecked until something needed it, and what was there
// compiled on one backend and not the other: `textureLod` is real GLSL,
// so that side passed straight through, while HLSL has no such function
// at all and the emitted text was simply not a program.
void Test_TextureSampleLevel_Run(TestContext& ctx)
{
    const char* source =
        "[Texture(Frame)] TextureCube environmentMap;\n"
        "[Buffer(Object)] float sampled;\n"
        "void main() {\n"
        "  Vector3 direction = Vector3(0.0, 1.0, 0.0);\n"
        "  sampled[ThreadID] = textureLod(environmentMap, direction, 0.0).x;\n"
        "}\n";

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = ShaderStage::Compute;
    options.fileName = "<test>";

    auto result = Compile(source, options, diagnostics);
    TEST_CHECK(ctx, result.IsOk());
    if (!result.IsOk())
    {
        for (const auto& d : diagnostics.entries) std::fprintf(stderr, "  diag: %s\n", d.message.c_str());
        return;
    }

    // GLSL names it the same, so the call goes through unchanged.
    const std::string& glsl = result.Value().glslSource;
    TEST_CHECK(ctx, glsl.find("textureLod(environmentMap") != std::string::npos);

    // HLSL has no `textureLod`. It is a method on the texture, and the
    // sampler that belongs to it has to be handed over as an argument --
    // neither of which the general call path knows to do.
    const std::string& hlsl = result.Value().hlslSource;
    TEST_CHECK(ctx, hlsl.find("environmentMap.SampleLevel(environmentMap_sampler,") != std::string::npos);
    TEST_CHECK(ctx, hlsl.find("textureLod(") == std::string::npos);
}
