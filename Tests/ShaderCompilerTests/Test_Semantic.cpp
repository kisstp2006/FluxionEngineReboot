#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/Frontend/Lexer.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Parser.hpp>
#include <Fluxion/ShaderCompiler/Semantic/SemanticAnalyzer.hpp>

using namespace Fluxion::ShaderCompiler;

namespace
{

bool AnalyzeSource(const char* source, DiagnosticList& diagnostics)
{
    std::vector<Token> tokens = Lex(source, "<test>", diagnostics);
    Program program = Parse(tokens, diagnostics);
    if (diagnostics.HasErrors()) return false;
    return Analyze(program, diagnostics);
}

} // namespace

void Test_Semantic_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource("void main() { Vector3 v = Vector3(1.0, 2.0, 3.0); float x = v.x + v.y; }", diagnostics);
        TEST_CHECK(ctx, ok);
    }
    {
        // Swizzle out of range for a Vector2.
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource("void main() { Vector2 v = Vector2(1.0, 2.0); float x = v.z; }", diagnostics);
        TEST_CHECK(ctx, !ok);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
    {
        // Overload resolution by argument type/count.
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource(
            "float avg(Vector2 x) { return (x.x + x.y) / 2.0; }\n"
            "float avg(Vector3 x) { return (x.x + x.y + x.z) / 3.0; }\n"
            "void main() { Vector2 a = Vector2(1.0, 2.0); float r = avg(a); }\n",
            diagnostics);
        TEST_CHECK(ctx, ok);
    }
    {
        // Use of an undeclared identifier.
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource("void main() { float x = undeclaredThing; }", diagnostics);
        TEST_CHECK(ctx, !ok);
    }
    {
        // Attribute declarations + a real `return` routed to [Target(0)].
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource(
            "[Input] Vector3 vColor;\n"
            "[Target(0)] Vector4 fragColor;\n"
            "void main() { return Vector4(vColor, 1.0); }\n",
            diagnostics);
        TEST_CHECK(ctx, ok);
    }
}
