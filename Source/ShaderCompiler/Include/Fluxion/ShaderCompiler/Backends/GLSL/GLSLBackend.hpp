#pragma once

#include <Fluxion/ShaderCompiler/AST/Ast.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <string>

namespace Fluxion::ShaderCompiler
{

// Emits GLSL 450 core source text. Unlike the HLSL backend, GLSL's own
// `in`/`out`/`uniform` globals already match this language's own
// declaration model 1:1, so no static-mirror/wrapper-main indirection is
// needed here -- functions (including the entry point) are emitted
// close to verbatim, with type names and RETURN(expr) rewritten.
std::string EmitGLSL(const Program& program, const ShaderIRModule& module, DiagnosticList& diagnostics);

} // namespace Fluxion::ShaderCompiler
