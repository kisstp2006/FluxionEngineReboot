#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/ShaderCompiler/Backends/GLSL/GLSLBackend.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Preprocessor.hpp>
#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <string>

namespace Fluxion::ShaderCompiler
{

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
};

// Runs the full pipeline: preprocess -> lex -> parse -> analyze -> build
// IR -> emit HLSL and GLSL text. `outDiagnostics` always receives every
// error/warning produced along the way (even on success, e.g. warnings);
// the returned Result only reports the pass/fail outcome, since
// Fluxion::Foundation::Result<T>'s error message must be a static string
// -- the actual per-error detail always lives in `outDiagnostics`.
Fluxion::Foundation::Result<CompiledShader> Compile(const std::string& source, const CompileOptions& options, DiagnosticList& outDiagnostics);

} // namespace Fluxion::ShaderCompiler
