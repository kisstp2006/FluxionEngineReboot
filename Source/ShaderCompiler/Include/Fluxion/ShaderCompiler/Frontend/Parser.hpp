#pragma once

#include <Fluxion/ShaderCompiler/AST/Ast.hpp>
#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Token.hpp>

#include <vector>

namespace Fluxion::ShaderCompiler
{

// Recursive-descent parser over an already-lexed token stream, standard
// C-family operator precedence (assignment binds loosest, postfix binds
// tightest). Parse errors are recorded as diagnostics; the parser
// resynchronizes at the next top-level declaration so one bad
// declaration doesn't hide errors in the rest of the file.
Program Parse(const std::vector<Token>& tokens, DiagnosticList& diagnostics);

} // namespace Fluxion::ShaderCompiler
