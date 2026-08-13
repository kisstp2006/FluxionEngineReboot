#pragma once

#include <Fluxion/Script/Compiler/Ast.hpp>
#include <Fluxion/Script/Compiler/Token.hpp>
#include <Fluxion/Script/Diagnostics.hpp>

#include <vector>

namespace Fluxion::Script
{

// Recursive descent over an already-lexed token stream. Precedence runs
// from assignment (loosest, and right-associative) through `||`, `&&`,
// equality, relational, additive, multiplicative and unary, to postfix
// call/member access (tightest).
//
// A token that does not match what the grammar expects is recorded as a
// diagnostic and abandons the construct being parsed; the parser then
// skips ahead to the next plausible declaration boundary so that a single
// mistake does not hide every problem after it.
Program Parse(const std::vector<Token>& tokens, DiagnosticList& diagnostics);

} // namespace Fluxion::Script
