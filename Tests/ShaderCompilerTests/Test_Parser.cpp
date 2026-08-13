#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/Frontend/Lexer.hpp>
#include <Fluxion/ShaderCompiler/Frontend/Parser.hpp>

using namespace Fluxion::ShaderCompiler;

namespace
{

Program ParseSource(const char* source, DiagnosticList& diagnostics)
{
    std::vector<Token> tokens = Lex(source, "<test>", diagnostics);
    return Parse(tokens, diagnostics);
}

} // namespace

void Test_Parser_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        Program program = ParseSource("void main() { float x = 1.0; }", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 1);
        TEST_CHECK(ctx, program.declarations[0]->kind == DeclKind::Function);
    }
    {
        DiagnosticList diagnostics;
        ParseSource("void main() { float x = ; }", diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
    {
        // Swizzle, function call, and if/for control flow.
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "float f(Vector3 v) {\n"
            "  float total = 0.0;\n"
            "  for (int i = 0; i < 3; i += 1) { total += v.x; }\n"
            "  if (total > 0.5) return v.y; else return v.z;\n"
            "}\n",
            diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 1);
    }
    {
        // The attribute declarations + spelled-out type names this
        // language exclusively uses.
        DiagnosticList diagnostics;
        Program program = ParseSource(
            "[Input] Vector3 vColor;\n"
            "[Target(0)] Vector4 fragColor;\n"
            "[Uniform] Vector3 tint;\n"
            "[Texture] Texture2D albedoMap;\n"
            "void main() { return Vector4(vColor, 1.0); }\n",
            diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, program.declarations.size() == 5);
        TEST_CHECK(ctx, program.declarations[0]->kind == DeclKind::StageIO);
        TEST_CHECK(ctx, program.declarations[1]->kind == DeclKind::OutputSlot);
        TEST_CHECK(ctx, program.declarations[2]->kind == DeclKind::Uniform);
        TEST_CHECK(ctx, program.declarations[3]->kind == DeclKind::Uniform);
        TEST_CHECK(ctx, program.declarations[4]->kind == DeclKind::Function);
    }
    {
        // [Target] with no explicit slot index is a parse error.
        DiagnosticList diagnostics;
        ParseSource("[Target] Vector4 fragColor;\n", diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
}
