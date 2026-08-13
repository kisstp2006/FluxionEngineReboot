#pragma once

#include <Fluxion/ShaderCompiler/Diagnostics.hpp>

#include <Fluxion/Foundation/Types.h>

#include <functional>
#include <string>
#include <vector>

namespace Fluxion::ShaderCompiler
{

// Resolves an #include target name to its raw text (true) or reports it
// couldn't be found (false) -- the caller supplies this so the compiler
// itself never hardcodes a filesystem layout or search path.
using IncludeResolver = std::function<bool(const std::string& name, std::string& outContent)>;

// One include, as it was actually read. The name is what the source asked
// for; the hash is of what came back. Two compilations of the same source
// only agree if every include agreed too, and an include that changed
// underneath is exactly what a check on the source text alone misses.
struct ResolvedInclude
{
    std::string name;
    u64 contentHash = 0;
};

// A small, line-oriented preprocessor: expands `#include "name"`
// (recursively, via resolver), and evaluates `#define NAME value` /
// `#ifdef` / `#ifndef` / `#else` / `#endif` conditional compilation.
// Every other directive is silently dropped -- this language's own
// declarations (`[Input]`, `[Target(N)]`, ...) are real syntax the
// parser understands directly, never a preprocessor macro.
// `outIncludes`, when given, receives every include actually resolved,
// in the order they were read.
std::string Preprocess(const std::string& source, const std::string& fileName, const IncludeResolver& resolver, DiagnosticList& diagnostics,
    std::vector<ResolvedInclude>* outIncludes = nullptr);

} // namespace Fluxion::ShaderCompiler
