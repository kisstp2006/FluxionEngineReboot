#include <Fluxion/Script/Script.hpp>

#include <Compiler/Prelude.hpp>

#include <Fluxion/Script/Compiler/Ast.hpp>
#include <Fluxion/Script/Compiler/BytecodeEmitter.hpp>
#include <Fluxion/Script/Compiler/Lexer.hpp>
#include <Fluxion/Script/Compiler/Parser.hpp>
#include <Fluxion/Script/Compiler/Semantic.hpp>

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace Fluxion::Script
{

namespace
{

// The prelude is lexed on its own so its tokens keep their own file name:
// a message about it names the prelude and not the caller's source, which
// is what makes the two tellable apart afterwards.
std::vector<Token> LexWithPrelude(const std::string& source, const CompileOptions& options, DiagnosticList& diagnostics)
{
    std::vector<Token> tokens = Lex(PreludeSource(), PreludeSourceName(), diagnostics);

    // Only the last stream's end-of-file terminates the whole thing.
    if (!tokens.empty()) tokens.pop_back();

    if (!options.hostPrelude.empty())
    {
        std::vector<Token> hostTokens = Lex(options.hostPrelude, options.hostPreludeName, diagnostics);
        if (!hostTokens.empty()) hostTokens.pop_back();
        tokens.insert(tokens.end(), std::make_move_iterator(hostTokens.begin()), std::make_move_iterator(hostTokens.end()));
    }

    std::vector<Token> userTokens = Lex(source, options.fileName, diagnostics);
    tokens.insert(tokens.end(), std::make_move_iterator(userTokens.begin()), std::make_move_iterator(userTokens.end()));
    return tokens;
}

// True when anything went wrong inside the prelude itself. That is a
// defect in this module rather than a mistake in the source it was handed,
// so it is reported with a code of its own instead of being blamed on the
// caller.
bool BlamesThePrelude(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.severity != DiagnosticSeverity::Error) continue;
        if (entry.location.file == PreludeSourceName()) return true;
    }
    return false;
}

} // namespace

Fluxion::Foundation::Result<CompiledModule> Compile(const std::string& source, const CompileOptions& options, DiagnosticList& outDiagnostics)
{
    using ResultType = Fluxion::Foundation::Result<CompiledModule>;

    // Each step stops the pipeline with its own code, so a caller can
    // tell how far the source got without parsing the diagnostic text.
    // Code 9 is the odd one out: it says the failure was not the
    // caller's.
    std::vector<Token> tokens = LexWithPrelude(source, options, outDiagnostics);
    if (outDiagnostics.HasErrors())
    {
        assert(!BlamesThePrelude(outDiagnostics) && "Fluxion: the script prelude must always lex");
        if (BlamesThePrelude(outDiagnostics)) return ResultType::Error(9, "the built-in script prelude failed to compile");
        return ResultType::Error(1, "script lexing failed");
    }

    Program program = Parse(tokens, outDiagnostics);
    if (outDiagnostics.HasErrors())
    {
        assert(!BlamesThePrelude(outDiagnostics) && "Fluxion: the script prelude must always parse");
        if (BlamesThePrelude(outDiagnostics)) return ResultType::Error(9, "the built-in script prelude failed to compile");
        return ResultType::Error(2, "script parsing failed");
    }

    if (!Analyze(program, outDiagnostics, options.bindings))
    {
        assert(!BlamesThePrelude(outDiagnostics) && "Fluxion: the script prelude must always analyze");
        if (BlamesThePrelude(outDiagnostics)) return ResultType::Error(9, "the built-in script prelude failed to compile");
        return ResultType::Error(3, "script semantic analysis failed");
    }

    CompiledModule module = Emit(program, options.fileName, options.moduleVersion, outDiagnostics, options.bindings);
    if (outDiagnostics.HasErrors())
    {
        assert(!BlamesThePrelude(outDiagnostics) && "Fluxion: the script prelude must always be emittable");
        if (BlamesThePrelude(outDiagnostics)) return ResultType::Error(9, "the built-in script prelude failed to compile");
        return ResultType::Error(4, "script bytecode emission failed");
    }

    return ResultType::Ok(std::move(module));
}

} // namespace Fluxion::Script
