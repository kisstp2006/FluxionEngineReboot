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

#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/ShaderCompiler/Backends/GLSL/GLSLBackend.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Preprocessor.hpp>
#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <Fluxion/Foundation/Types.h>

#include <string>
#include <vector>

namespace Fluxion::ShaderCompiler
{

// What this build of the compiler understands, and what it produces.
//
// Anything kept beside a source so it need not be compiled again has to
// be able to tell whether it was produced by this compiler or an older
// one, and there is no way to work that out from the output itself. So it
// is written down. Raise the language version when what a source is
// allowed to say changes; raise the compiler version when the same source
// starts producing different output. Failing to raise either is not a
// build error -- it is a stale artifact served silently, which is worse.
inline constexpr u32 kShaderLanguageVersion = 1;
inline constexpr u32 kShaderCompilerVersion = 1;

struct CompileOptions
{
    ShaderStage stage = ShaderStage::Fragment;
    std::string entryPoint = "main";
    std::string fileName = "<source>";
    IncludeResolver includeResolver; // optional; unset means #include always fails to resolve

    // Nothing target-specific is fixed inside the compiler itself --
    // both of these carry sane defaults for the current Vulkan backend,
    // but a caller targeting a different pipeline layout or GLSL profile
    // overrides them here rather than the compiler guessing.
    IRBuildOptions irOptions;
    GLSLOptions glslOptions;
};

struct CompiledShader
{
    ShaderIRModule reflection;
    std::string hlslSource;
    std::string glslSource;

    // Every `#include` that was actually resolved while compiling this,
    // in the order they were read. Reported so a caller keeping the
    // result can tell later whether any of them has changed since --
    // which the source text alone cannot say.
    std::vector<ResolvedInclude> includes;
};

// Runs the full pipeline: preprocess -> lex -> parse -> analyze -> build
// IR -> emit HLSL and GLSL text. `outDiagnostics` always receives every
// error/warning produced along the way (even on success, e.g. warnings);
// the returned Result only reports the pass/fail outcome, since
// Fluxion::Foundation::Result<T>'s error message must be a static string
// -- the actual per-error detail always lives in `outDiagnostics`.
Fluxion::Foundation::Result<CompiledShader> Compile(const std::string& source, const CompileOptions& options, DiagnosticList& outDiagnostics);

} // namespace Fluxion::ShaderCompiler
