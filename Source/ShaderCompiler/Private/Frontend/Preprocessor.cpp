#include <Fluxion/ShaderCompiler/Frontend/Preprocessor.hpp>

#include <Fluxion/Foundation/Hashing.h>

#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Fluxion::ShaderCompiler
{

namespace
{

std::string Trim(const std::string& s)
{
    size_t begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

// Extracts the bare name out of `"name"` or `<name>` (or a bare token,
// for the legacy `#include name` spelling this language's fixture corpus
// also uses).
std::string ExtractIncludeName(const std::string& rest)
{
    std::string trimmed = Trim(rest);
    if (trimmed.size() >= 2 && (trimmed.front() == '"' || trimmed.front() == '<'))
        return trimmed.substr(1, trimmed.size() - 2);
    return trimmed;
}

// Names the frontend already gives real meaning to as language keywords
// (see Lexer.cpp's keyword table) -- a legacy `#define VERT_IN attribute`
// style alias is superseded by that native understanding rather than
// applied, so it can never shadow the keyword with plain text the parser
// wouldn't recognize.
bool IsReservedMacroName(const std::string& name)
{
    static const std::unordered_set<std::string> reserved = {
        "VERT_IN", "VERT_OUT", "FRAG_IN", "RETURN", "in", "out",
    };
    return reserved.count(name) != 0;
}

// Classic backslash-newline line continuation: joins any line ending in
// '\' (optionally trailing whitespace) with the next physical line,
// before any directive/macro processing happens -- needed for multi-line
// `#define NAME ... \` bodies to be seen as a single logical line.
std::string JoinContinuations(const std::string& source)
{
    std::string result;
    result.reserve(source.size());
    std::istringstream in(source);
    std::string line;
    bool first = true;
    while (std::getline(in, line))
    {
        std::string trimmedEnd = line;
        while (!trimmedEnd.empty() && (trimmedEnd.back() == '\r' || trimmedEnd.back() == ' ' || trimmedEnd.back() == '\t'))
            trimmedEnd.pop_back();

        if (!first) { } // newline already appended after previous line unless continuing
        if (!trimmedEnd.empty() && trimmedEnd.back() == '\\')
        {
            result += trimmedEnd.substr(0, trimmedEnd.size() - 1);
            result += ' ';
            continue; // do not end the logical line yet
        }
        result += line;
        result += '\n';
        first = false;
    }
    return result;
}

class PreprocessorState
{
public:
    PreprocessorState(const IncludeResolver& resolver, DiagnosticList& diagnostics, std::vector<ResolvedInclude>* outIncludes)
        : m_resolver(resolver), m_diagnostics(diagnostics), m_includes(outIncludes)
    {
    }

    std::string Run(const std::string& source, const std::string& fileName, int depth)
    {
        if (depth > 32)
        {
            m_diagnostics.AddError(SourceLocation{ fileName, 0, 0 }, "include depth exceeded (32) -- possible include cycle");
            return "";
        }

        std::string joined = JoinContinuations(source);

        // A UTF-8 BOM here is worse than one reaching the lexer: it sits
        // in front of the '#' of a first-line directive, so the line
        // stops being recognised as one at all and is copied into the
        // output verbatim. An #include on line 1 then silently does
        // nothing, and the failure surfaces much later as a missing
        // definition. Every included file goes through here too, so one
        // check covers the whole tree.
        if (joined.size() >= 3 &&
            (unsigned char)joined[0] == 0xEFu &&
            (unsigned char)joined[1] == 0xBBu &&
            (unsigned char)joined[2] == 0xBFu)
        {
            joined.erase(0, 3);
        }

        std::ostringstream out;
        std::istringstream in(joined);
        std::string line;
        unsigned int lineNumber = 0;

        // One bool per open #if/#ifdef/#ifndef nesting level: whether
        // this level's branch is currently emitting text.
        std::vector<bool> emitStack;
        auto emitting = [&]() -> bool
        {
            for (bool b : emitStack) if (!b) return false;
            return true;
        };

        while (std::getline(in, line))
        {
            ++lineNumber;
            std::string trimmed = Trim(line);

            if (!trimmed.empty() && trimmed[0] == '#')
            {
                std::string directive = trimmed.substr(1);
                size_t space = directive.find_first_of(" \t");
                std::string word = (space == std::string::npos) ? directive : directive.substr(0, space);
                std::string rest = (space == std::string::npos) ? "" : Trim(directive.substr(space + 1));

                if (word == "include")
                {
                    if (!emitting()) continue;
                    std::string name = ExtractIncludeName(rest);
                    std::string content;
                    if (!m_resolver || !m_resolver(name, content))
                    {
                        m_diagnostics.AddError(SourceLocation{ fileName, lineNumber, 1 }, "cannot resolve #include \"" + name + "\"");
                        continue;
                    }
                    // Recorded before it is expanded, so the order is the
                    // order they were read, and recorded as the text that
                    // came back rather than the name that asked for it --
                    // two builds can resolve the same name to different
                    // files, and it is the text that decides the answer.
                    if (m_includes != nullptr)
                        m_includes->push_back(ResolvedInclude{ name, Fluxion_HashBytes64(content.data(), content.size()) });

                    out << Run(content, name, depth + 1) << "\n";
                    continue;
                }
                if (word == "define")
                {
                    if (!emitting()) continue;
                    size_t nameEnd = rest.find_first_of(" \t");
                    std::string macroName = (nameEnd == std::string::npos) ? rest : rest.substr(0, nameEnd);
                    std::string macroValue = (nameEnd == std::string::npos) ? "" : Trim(rest.substr(nameEnd + 1));
                    if (!IsReservedMacroName(macroName))
                        m_macros[macroName] = macroValue;
                    continue;
                }
                if (word == "ifdef")
                {
                    emitStack.push_back(m_macros.count(rest) != 0);
                    continue;
                }
                if (word == "ifndef")
                {
                    emitStack.push_back(m_macros.count(rest) == 0);
                    continue;
                }
                if (word == "if")
                {
                    // Minimal `#if <expr>` support: only literal 0/1 and
                    // bare macro-defined-ness are understood (matching
                    // this language's own fixture corpus, which only
                    // ever writes `#if 0`/`#if 1`) -- a full constant-
                    // expression evaluator isn't needed for that usage.
                    bool truthy = (rest == "1") || (rest != "0" && m_macros.count(rest) != 0);
                    emitStack.push_back(truthy);
                    continue;
                }
                if (word == "else")
                {
                    if (!emitStack.empty()) emitStack.back() = !emitStack.back();
                    continue;
                }
                if (word == "endif")
                {
                    if (!emitStack.empty()) emitStack.pop_back();
                    continue;
                }
                // Any other directive (`#extension`, `#version`,
                // `#pragma`, ...) has no portable meaning for this
                // language's own frontend/backends, so it's consumed
                // here rather than leaked through as text the parser
                // would choke on. This language's own declarations are
                // real syntax (`[Input]`, `[Target(N)]`, ...), never a
                // preprocessor directive.
                continue;
            }

            if (!emitting()) continue;
            out << ExpandMacros(line) << "\n";
        }

        return out.str();
    }

private:
    const IncludeResolver& m_resolver;
    DiagnosticList& m_diagnostics;
    std::vector<ResolvedInclude>* m_includes;
    std::unordered_map<std::string, std::string> m_macros;

    std::string ExpandMacros(const std::string& line) const
    {
        if (m_macros.empty()) return line;

        std::string result;
        result.reserve(line.size());
        size_t i = 0;
        while (i < line.size())
        {
            char c = line[i];
            if (std::isalpha((unsigned char)c) || c == '_')
            {
                size_t start = i;
                while (i < line.size() && (std::isalnum((unsigned char)line[i]) || line[i] == '_')) ++i;
                std::string word = line.substr(start, i - start);
                auto it = m_macros.find(word);
                result += (it != m_macros.end()) ? it->second : word;
            }
            else
            {
                result += c;
                ++i;
            }
        }
        return result;
    }
};

} // namespace

std::string Preprocess(const std::string& source, const std::string& fileName, const IncludeResolver& resolver, DiagnosticList& diagnostics,
    std::vector<ResolvedInclude>* outIncludes)
{
    if (outIncludes != nullptr) outIncludes->clear();
    PreprocessorState state(resolver, diagnostics, outIncludes);
    return state.Run(source, fileName, 0);
}

} // namespace Fluxion::ShaderCompiler
