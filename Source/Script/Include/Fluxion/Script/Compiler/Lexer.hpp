#pragma once

#include <Fluxion/Script/Compiler/Token.hpp>
#include <Fluxion/Script/Diagnostics.hpp>

#include <string>
#include <vector>

namespace Fluxion::Script
{

// Turns source text into a flat token stream. Lexing never gives up
// partway: an unrecognized character is recorded as a diagnostic and
// skipped, so the caller always receives a complete, terminated token
// stream and the parser can keep reporting further problems in the same
// run. The returned vector always ends with an EndOfFile token.
std::vector<Token> Lex(const std::string& source, const std::string& fileName, DiagnosticList& diagnostics);

} // namespace Fluxion::Script
