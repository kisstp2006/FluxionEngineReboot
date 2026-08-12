#pragma once

#include <Fluxion/ShaderCompiler/Diagnostics.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Token.hpp>

#include <string>
#include <vector>

namespace Fluxion::ShaderCompiler
{

// Turns already-preprocessed source text (see Preprocessor.h -- #include
// has already been expanded by the time this runs) into a flat token
// stream. Lexing never fails outright: an unrecognized character is
// recorded as a diagnostic and skipped, so the caller always gets a best-
// effort token stream to keep parsing (and reporting further errors) on.
std::vector<Token> Lex(const std::string& source, const std::string& fileName, DiagnosticList& diagnostics);

} // namespace Fluxion::ShaderCompiler
