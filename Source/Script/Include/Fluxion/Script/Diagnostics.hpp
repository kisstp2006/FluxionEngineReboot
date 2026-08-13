#pragma once

#include <string>
#include <vector>

namespace Fluxion::Script
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

// A compile/load error or warning, carrying its own formatted message and
// the exact place in the source it came from. The C-facing FluxionResult
// can only hold a static string, so it is unusable for this: one source
// file routinely produces many distinct errors in a single pass, each
// needing its own dynamically built text.
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

    void AddWarning(SourceLocation location, std::string message)
    {
        entries.push_back(Diagnostic{ std::move(location), DiagnosticSeverity::Warning, std::move(message) });
    }
};

} // namespace Fluxion::Script
