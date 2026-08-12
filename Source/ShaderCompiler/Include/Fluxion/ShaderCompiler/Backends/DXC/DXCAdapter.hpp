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
