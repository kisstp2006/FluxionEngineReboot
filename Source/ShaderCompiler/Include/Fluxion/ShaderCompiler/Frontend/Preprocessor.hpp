#pragma once

#include <Fluxion/ShaderCompiler/Diagnostics.hpp>

#include <functional>
#include <string>

namespace Fluxion::ShaderCompiler
{

// Resolves an #include target name to its raw text (true) or reports it
// couldn't be found (false) -- the caller supplies this so the compiler
// itself never hardcodes a filesystem layout or search path.
using IncludeResolver = std::function<bool(const std::string& name, std::string& outContent)>;

// A small, line-oriented preprocessor: expands `#include "name"`
// (recursively, via resolver), and evaluates `#define NAME value` /
// `#ifdef` / `#ifndef` / `#else` / `#endif` conditional compilation.
// Every other directive is silently dropped -- this language's own
// declarations (`[Input]`, `[Target(N)]`, ...) are real syntax the
// parser understands directly, never a preprocessor macro.
std::string Preprocess(const std::string& source, const std::string& fileName, const IncludeResolver& resolver, DiagnosticList& diagnostics);

} // namespace Fluxion::ShaderCompiler
