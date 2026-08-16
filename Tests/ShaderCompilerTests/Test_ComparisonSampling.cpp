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

// Reading a depth map through a comparison, in both target languages.
//
// BOTH outputs are checked in one place, and that is the point of this
// file rather than a nicety: the two languages spell this so differently
// -- a method on the texture against a coordinate with an extra
// component -- that an implementation of one says nothing about the
// other. A sampling call that existed on one backend only has already
// shipped here once.

namespace
{

const char* const kShadowSource =
    "[Texture(Frame)] Texture2DShadow shadowMap;\n"
    "[Input] Vector3 shadowCoord;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  float lit = textureCompare(shadowMap, shadowCoord.xy, shadowCoord.z);\n"
    "  return Vector4(lit, lit, lit, 1.0);\n"
    "}\n";

CompileOptions FragmentOptions()
{
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = "<test>";
    return options;
}

} // namespace

void Test_ComparisonSampling_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        auto result = Compile(kShadowSource, FragmentOptions(), diagnostics);
        TEST_CHECK(ctx, result.IsOk());
        if (!result.IsOk())
        {
            for (const auto& d : diagnostics.entries) std::fprintf(stderr, "  diag: %s\n", d.message.c_str());
            return;
        }

        // GLSL: one combined object, and the depth to compare against
        // rides in the coordinate's last component -- three arguments
        // become two.
        const std::string& glsl = result.Value().glslSource;
        TEST_CHECK(ctx, glsl.find("uniform sampler2DShadow shadowMap") != std::string::npos);
        TEST_CHECK(ctx, glsl.find("texture(shadowMap, vec3(") != std::string::npos);
        TEST_CHECK(ctx, glsl.find("textureCompare(") == std::string::npos);

        // HLSL: a one-channel texture, a sampler of a DIFFERENT TYPE
        // that carries the comparison, and a method rather than a
        // function. An ordinary SamplerState here would compile and
        // return the depth instead of the answer.
        const std::string& hlsl = result.Value().hlslSource;
        TEST_CHECK(ctx, hlsl.find("Texture2D<float> shadowMap") != std::string::npos);
        TEST_CHECK(ctx, hlsl.find("SamplerComparisonState shadowMap_sampler") != std::string::npos);
        TEST_CHECK(ctx, hlsl.find("shadowMap.SampleCmpLevelZero(shadowMap_sampler,") != std::string::npos);
        TEST_CHECK(ctx, hlsl.find("textureCompare(") == std::string::npos);
    }

    {
        // An ordinary texture holds depths rather than the answer to a
        // comparison, and averaging depths means nothing. Refused here,
        // where the name is -- not by a target language, in text nobody
        // wrote.
        const char* wrongTexture =
            "[Texture(Frame)] Texture2D colorMap;\n"
            "[Input] Vector3 shadowCoord;\n"
            "[Target(0)] Vector4 fragColor;\n"
            "void main() {\n"
            "  float lit = textureCompare(colorMap, shadowCoord.xy, shadowCoord.z);\n"
            "  return Vector4(lit, lit, lit, 1.0);\n"
            "}\n";

        DiagnosticList diagnostics;
        auto result = Compile(wrongTexture, FragmentOptions(), diagnostics);
        TEST_CHECK(ctx, !result.IsOk());

        bool namedIt = false;
        for (const Diagnostic& entry : diagnostics.entries)
        {
            if (entry.message.find("Texture2DShadow") != std::string::npos) namedIt = true;
        }
        TEST_CHECK(ctx, namedIt);
    }

    {
        // And the shape of the call itself: a shadow map and a
        // coordinate say nothing without a depth to compare against.
        const char* missingDepth =
            "[Texture(Frame)] Texture2DShadow shadowMap;\n"
            "[Input] Vector3 shadowCoord;\n"
            "[Target(0)] Vector4 fragColor;\n"
            "void main() {\n"
            "  float lit = textureCompare(shadowMap, shadowCoord.xy);\n"
            "  return Vector4(lit, lit, lit, 1.0);\n"
            "}\n";

        DiagnosticList diagnostics;
        auto result = Compile(missingDepth, FragmentOptions(), diagnostics);
        TEST_CHECK(ctx, !result.IsOk());
    }
}
