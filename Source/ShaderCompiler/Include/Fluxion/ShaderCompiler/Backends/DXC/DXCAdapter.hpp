#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Fluxion::ShaderCompiler
{

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
    DiagnosticList& outDiagnostics);

} // namespace Fluxion::ShaderCompiler
