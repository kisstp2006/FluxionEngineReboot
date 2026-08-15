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
    {
        // A name one of the target languages has taken.
        //
        // This is not pedantry about a word nobody would use: `packed`
        // describes exactly what a normal map holds, it reads perfectly
        // in this language, HLSL accepted it, and the shader failed only
        // on OpenGL -- as a syntax error against generated text, at a
        // line the author could not look at. Refused here, where the
        // name is, and where the message can name it.
        DiagnosticList diagnostics;
        bool ok = AnalyzeSource("void main() { Vector3 packed = Vector3(1.0, 1.0, 1.0); }", diagnostics);
        TEST_CHECK(ctx, !ok);

        bool namedIt = false;
        for (const Diagnostic& entry : diagnostics.entries)
        {
            if (entry.message.find("'packed'") != std::string::npos) namedIt = true;
        }
        TEST_CHECK(ctx, namedIt);
    }
    {
        // Everywhere a name can be introduced, not only local variables:
        // a uniform, a function, a parameter and a struct field all end
        // up in the generated text too.
        DiagnosticList uniformDiagnostics;
        TEST_CHECK(ctx, !AnalyzeSource("[Uniform] float sample;\nvoid main() { }\n", uniformDiagnostics));

        DiagnosticList functionDiagnostics;
        TEST_CHECK(ctx, !AnalyzeSource("float filter(float x) { return x; }\nvoid main() { }\n", functionDiagnostics));

        DiagnosticList parameterDiagnostics;
        TEST_CHECK(ctx, !AnalyzeSource("float twice(float input) { return input; }\nvoid main() { }\n", parameterDiagnostics));

        DiagnosticList fieldDiagnostics;
        TEST_CHECK(ctx, !AnalyzeSource("struct Thing { float shared; }\nvoid main() { }\n", fieldDiagnostics));
    }
    {
        // And a name that merely CONTAINS one is fine -- the rule is
        // about whole words, or it would refuse half the sensible names
        // there are.
        DiagnosticList diagnostics;
        TEST_CHECK(ctx, AnalyzeSource("void main() { Vector3 packedNormal = Vector3(1.0, 1.0, 1.0); float sampleCount = 4.0; }", diagnostics));
    }
}
