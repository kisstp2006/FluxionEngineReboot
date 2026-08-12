#pragma once

#include <Fluxion/ShaderCompiler/AST/Ast.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <string>

namespace Fluxion::ShaderCompiler
{

// Emits HLSL (Shader Model 6-style) source text from an already-Analyze()'d
// Program and its IR module. Vertex-stage output requires exactly one
// `out vec4 Position;` field, mapped to SV_Position -- the shader's clip-
// space output, in place of a GLSL-style implicit gl_Position.
std::string EmitHLSL(const Program& program, const ShaderIRModule& module, DiagnosticList& diagnostics);

} // namespace Fluxion::ShaderCompiler
