#pragma once

#include <Fluxion/Script/Compiler/Ast.hpp>
#include <Fluxion/Script/Diagnostics.hpp>

namespace Fluxion::Script
{

// Type-checks a parsed program and annotates it in place with everything
// code generation needs: every expression's resolved type, the frame slot
// each name resolves to, the target of every call, the frame size of
// every method, and each place an int has to be widened to a float or a
// value turned into text.
//
// The only implicit conversion is int to float, applied in assignment, in
// a mixed arithmetic or comparison operand pair, and in argument passing.
// A condition must already be a bool; a number is never treated as one.
//
// Returns true when no errors were added to `diagnostics`.
bool Analyze(Program& program, DiagnosticList& diagnostics);

} // namespace Fluxion::Script
