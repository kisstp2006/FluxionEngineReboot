#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/Frontend/Lexer.hpp>

using namespace Fluxion::ShaderCompiler;

void Test_Lexer_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("float x = 1.5;", "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, tokens.size() == 6); // float, x, =, 1.5, ;, EOF
    }
    {
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("Vector3", "<test>", diagnostics);
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::KwVector3);
    }
    {
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("Texture2D Matrix4x4 Vector2", "<test>", diagnostics);
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::KwTexture2D);
        TEST_CHECK(ctx, tokens[1].kind == TokenKind::KwMatrix4x4);
        TEST_CHECK(ctx, tokens[2].kind == TokenKind::KwVector2);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
    }
    {
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("1.0e6 1e-3 2.5f", "<test>", diagnostics);
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::FloatLiteral && tokens[0].numberValue == 1.0e6);
        TEST_CHECK(ctx, tokens[1].kind == TokenKind::FloatLiteral);
        TEST_CHECK(ctx, tokens[2].kind == TokenKind::FloatLiteral && tokens[2].numberValue == 2.5);
    }
    {
        DiagnosticList diagnostics;
        Lex("@", "<test>", diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
}
