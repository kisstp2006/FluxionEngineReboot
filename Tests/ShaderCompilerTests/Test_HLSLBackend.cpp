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
