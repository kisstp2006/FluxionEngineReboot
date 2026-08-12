#pragma once

#include <Fluxion/ShaderCompiler/AST/Ast.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/IR/ShaderIR.hpp>

#include <string>

namespace Fluxion::ShaderCompiler
{

struct GLSLOptions
{
    // "450 core", "460 core", ... -- caller-selected, since the right
    // GLSL version depends on the target driver/API the caller is
    // actually compiling for, not something this module can assume.
    std::string versionDirective = "450 core";
};

// Emits GLSL source text. Unlike the HLSL backend, GLSL's own
// `in`/`out`/`uniform` globals already match this language's own
// declaration model 1:1, so no static-mirror/wrapper-main indirection is
// needed here -- functions (including the entry point) are emitted
// close to verbatim, with type names rewritten and a `return` inside the
// entry function routed to its declared target.
std::string EmitGLSL(const Program& program, const ShaderIRModule& module, DiagnosticList& diagnostics, const GLSLOptions& options = {});

} // namespace Fluxion::ShaderCompiler
