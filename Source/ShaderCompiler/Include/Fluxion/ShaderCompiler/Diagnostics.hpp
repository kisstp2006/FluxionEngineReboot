#pragma once

#include <string>
#include <vector>

namespace Fluxion::ShaderCompiler
{

struct SourceLocation
{
    std::string file;
    unsigned int line = 0;
    unsigned int column = 0;
};

enum class DiagnosticSeverity
{
    Error,
    Warning,
};

// A compile error/warning carries its own formatted message and location
// (unlike the C-facing FluxionResult, whose message field must be a
// static string) -- a shader source can produce many distinct errors in
// one pass, each needing its own dynamic text.
struct Diagnostic
{
    SourceLocation location;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
};

struct DiagnosticList
{
    std::vector<Diagnostic> entries;

    bool HasErrors() const
    {
        for (const Diagnostic& d : entries)
        {
            if (d.severity == DiagnosticSeverity::Error) return true;
        }
        return false;
    }

    void AddError(SourceLocation location, std::string message)
    {
        entries.push_back(Diagnostic{ std::move(location), DiagnosticSeverity::Error, std::move(message) });
    }
};

} // namespace Fluxion::ShaderCompiler
