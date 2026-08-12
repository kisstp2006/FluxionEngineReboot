#pragma once

#include <Fluxion/ShaderCompiler/AST/Ast.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>

namespace Fluxion::ShaderCompiler
{

// Type-checks and annotates a parsed Program in place: every Expr's
// resolvedType is filled in, overloaded function calls are validated
// against the declared overload set, and swizzle member access is
// checked against the base vector's component count. Routing a plain
// `return expr;` to the enclosing entry function's [Target(N)]/[Output]
// happens later, in the backends' own text emission, once they know
// which function is the entry point. Returns true if no errors were
// added to `diagnostics`.
bool Analyze(Program& program, DiagnosticList& diagnostics);

} // namespace Fluxion::ShaderCompiler
