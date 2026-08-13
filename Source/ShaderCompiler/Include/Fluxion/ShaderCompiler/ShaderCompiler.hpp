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
