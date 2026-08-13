#include <Fluxion/Script/Script.hpp>

#include <Fluxion/Script/Compiler/Ast.hpp>
#include <Fluxion/Script/Compiler/BytecodeEmitter.hpp>
#include <Fluxion/Script/Compiler/Lexer.hpp>
#include <Fluxion/Script/Compiler/Parser.hpp>
#include <Fluxion/Script/Compiler/Semantic.hpp>

#include <utility>
#include <vector>

namespace Fluxion::Script
{

Fluxion::Foundation::Result<CompiledModule> Compile(const std::string& source, const CompileOptions& options, DiagnosticList& outDiagnostics)
{
    using ResultType = Fluxion::Foundation::Result<CompiledModule>;

    // Each step stops the pipeline with its own code, so a caller can
    // tell how far the source got without parsing the diagnostic text.
    std::vector<Token> tokens = Lex(source, options.fileName, outDiagnostics);
    if (outDiagnostics.HasErrors())
        return ResultType::Error(1, "script lexing failed");

    Program program = Parse(tokens, outDiagnostics);
    if (outDiagnostics.HasErrors())
        return ResultType::Error(2, "script parsing failed");

    if (!Analyze(program, outDiagnostics))
        return ResultType::Error(3, "script semantic analysis failed");

    CompiledModule module = Emit(program, options.fileName, options.moduleVersion, outDiagnostics);
    if (outDiagnostics.HasErrors())
        return ResultType::Error(4, "script bytecode emission failed");

    return ResultType::Ok(std::move(module));
}

} // namespace Fluxion::Script
