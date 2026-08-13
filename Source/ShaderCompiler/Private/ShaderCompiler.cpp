#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <Fluxion/ShaderCompiler/AST/Ast.hpp>
#include <Fluxion/ShaderCompiler/Backends/GLSL/GLSLBackend.hpp>
#include <Fluxion/ShaderCompiler/Backends/HLSL/HLSLBackend.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Lexer.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Parser.hpp>
#include <Fluxion/ShaderCompiler/Semantic/SemanticAnalyzer.hpp>

namespace Fluxion::ShaderCompiler
{

Fluxion::Foundation::Result<CompiledShader> Compile(const std::string& source, const CompileOptions& options, DiagnosticList& outDiagnostics)
{
    std::vector<ResolvedInclude> includes;
    std::string preprocessed = Preprocess(source, options.fileName, options.includeResolver, outDiagnostics, &includes);
    if (outDiagnostics.HasErrors())
        return Fluxion::Foundation::Result<CompiledShader>::Error(1, "shader preprocessing failed");

    std::vector<Token> tokens = Lex(preprocessed, options.fileName, outDiagnostics);
    if (outDiagnostics.HasErrors())
        return Fluxion::Foundation::Result<CompiledShader>::Error(2, "shader lexing failed");

    Program program = Parse(tokens, outDiagnostics);
    if (outDiagnostics.HasErrors())
        return Fluxion::Foundation::Result<CompiledShader>::Error(3, "shader parsing failed");

    if (!Analyze(program, outDiagnostics))
        return Fluxion::Foundation::Result<CompiledShader>::Error(4, "shader semantic analysis failed");

    CompiledShader result;
    result.reflection = BuildIR(program, options.stage, outDiagnostics, options.irOptions);
    result.reflection.entryPoint = options.entryPoint;
    if (outDiagnostics.HasErrors())
        return Fluxion::Foundation::Result<CompiledShader>::Error(5, "shader IR construction failed");

    result.hlslSource = EmitHLSL(program, result.reflection, outDiagnostics);
    result.glslSource = EmitGLSL(program, result.reflection, outDiagnostics, options.glslOptions);
    if (outDiagnostics.HasErrors())
        return Fluxion::Foundation::Result<CompiledShader>::Error(6, "shader backend emission failed");

    result.includes = std::move(includes);
    return Fluxion::Foundation::Result<CompiledShader>::Ok(std::move(result));
}

} // namespace Fluxion::ShaderCompiler
