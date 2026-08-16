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
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Fluxion::ShaderCompiler
{

// Nothing about the target shader model or SPIR-V environment is fixed
// inside the adapter itself -- both are caller-supplied, defaulting to
// values that work with the Vulkan 1.2-minimum backend this compiler
// currently targets, but callers building for a newer/older Vulkan
// version (or a different SPIR-V consumer entirely) can override either
// without touching this module.
struct DXCOptions
{
    std::string shaderModel = "6_0";       // dxc -T <stage>_<shaderModel>
    std::string spirvTargetEnv = "vulkan1.2"; // dxc -fspv-target-env=<...>
};

// Separate from DXCOptions (not reused) since a DXIL target has no
// SPIR-V-environment concept at all -- keeping the two option structs
// distinct means a caller can't accidentally pass a meaningless
// spirvTargetEnv to a DXIL compile or vice versa.
struct DXILOptions
{
    std::string shaderModel = "6_0"; // dxc -T <stage>_<shaderModel>, no -spirv
};

// The only file in this module that ever shells out to an external
// tool: hands HLSL text to the `dxc` command-line compiler (from the
// Vulkan/DirectX Shader Compiler project) and gets SPIR-V bytes back.
// dxc is invoked as a plain external process (not linked against) so
// this module never depends on its library or license terms -- see
// DXCAdapter.cpp for why a swappable external tool is used here instead
// of an in-process SPIR-V backend.
bool IsDXCAvailable();

// How the external tool identifies itself, verbatim. Anything that keeps
// a result so it need not be produced again has to include this: the tool
// lives outside this build entirely, and a different one turns the same
// text into different bytes without one line here changing. Asked once
// per process and remembered. Empty when it could not be asked, which is
// a distinct answer and has to be treated as one rather than as "the same
// as last time".
const std::string& DXCIdentity();

Fluxion::Foundation::Result<std::vector<uint8_t>> CompileToSpirv(
    const std::string& hlslSource,
    ShaderStage stage,
    const std::string& entryPoint,
    DiagnosticList& outDiagnostics,
    const DXCOptions& options = {});

// Same HLSL-text-in, bytes-out shape as CompileToSpirv, targeting D3D12's
// native bytecode format instead -- the D3D12 RHI backend's
// FluxionRHIShaderDesc::bytecode expects this, the same way the Vulkan
// backend expects CompileToSpirv's output.
Fluxion::Foundation::Result<std::vector<uint8_t>> CompileToDxil(
    const std::string& hlslSource,
    ShaderStage stage,
    const std::string& entryPoint,
    DiagnosticList& outDiagnostics,
    const DXILOptions& options = {});

} // namespace Fluxion::ShaderCompiler
