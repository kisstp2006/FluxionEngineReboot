#include "TestFramework.h"

#include <Fluxion/Script/Compiler/Lexer.hpp>

#include <string>

using namespace Fluxion::Script;

void Test_Lexer_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("int x = 42;", "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, tokens.size() == 6);
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::KwInt);
        TEST_CHECK(ctx, tokens[1].kind == TokenKind::Identifier && tokens[1].text == "x");
        TEST_CHECK(ctx, tokens[2].kind == TokenKind::Assign);
        TEST_CHECK(ctx, tokens[3].kind == TokenKind::IntLiteral && tokens[3].intValue == 42);
        TEST_CHECK(ctx, tokens[4].kind == TokenKind::Semicolon);
        TEST_CHECK(ctx, tokens[5].kind == TokenKind::EndOfFile);
    }
    {
        // Declaration keywords are recognized as keywords, not names.
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("static class var bool string void float return break continue", "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::KwStatic);
        TEST_CHECK(ctx, tokens[1].kind == TokenKind::KwClass);
        TEST_CHECK(ctx, tokens[2].kind == TokenKind::KwVar);
        TEST_CHECK(ctx, tokens[3].kind == TokenKind::KwBool);
        TEST_CHECK(ctx, tokens[4].kind == TokenKind::KwString);
        TEST_CHECK(ctx, tokens[5].kind == TokenKind::KwVoid);
        TEST_CHECK(ctx, tokens[6].kind == TokenKind::KwFloat);
        TEST_CHECK(ctx, tokens[7].kind == TokenKind::KwReturn);
        TEST_CHECK(ctx, tokens[8].kind == TokenKind::KwBreak);
        TEST_CHECK(ctx, tokens[9].kind == TokenKind::KwContinue);
    }
    {
        // Line and column are tracked across newlines and leading
        // indentation, since every diagnostic is only as useful as the
        // position it points at.
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex(
            "static class Program\n"
            "{\n"
            "    static void Main() { }\n"
            "}\n",
            "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());

        TEST_CHECK(ctx, tokens[0].kind == TokenKind::KwStatic && tokens[0].location.line == 1 && tokens[0].location.column == 1);
        TEST_CHECK(ctx, tokens[1].kind == TokenKind::KwClass && tokens[1].location.line == 1 && tokens[1].location.column == 8);
        TEST_CHECK(ctx, tokens[2].kind == TokenKind::Identifier && tokens[2].location.line == 1 && tokens[2].location.column == 14);
        TEST_CHECK(ctx, tokens[3].kind == TokenKind::LBrace && tokens[3].location.line == 2 && tokens[3].location.column == 1);
        TEST_CHECK(ctx, tokens[4].kind == TokenKind::KwStatic && tokens[4].location.line == 3 && tokens[4].location.column == 5);
        TEST_CHECK(ctx, tokens[5].kind == TokenKind::KwVoid && tokens[5].location.line == 3 && tokens[5].location.column == 12);
        TEST_CHECK(ctx, tokens[6].kind == TokenKind::Identifier && tokens[6].location.line == 3 && tokens[6].location.column == 17);
        TEST_CHECK(ctx, tokens[7].kind == TokenKind::LParen && tokens[7].location.line == 3 && tokens[7].location.column == 21);
        TEST_CHECK(ctx, tokens[10].kind == TokenKind::RBrace && tokens[10].location.line == 3 && tokens[10].location.column == 26);
        TEST_CHECK(ctx, tokens[11].kind == TokenKind::RBrace && tokens[11].location.line == 4 && tokens[11].location.column == 1);
        TEST_CHECK(ctx, tokens[0].location.file == "<test>");
    }
    {
        // Both comment forms are skipped, and the tokens after them keep
        // the right position.
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex(
            "// leading remark\n"
            "int /* inline */ x;\n",
            "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, tokens.size() == 4);
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::KwInt && tokens[0].location.line == 2);
        TEST_CHECK(ctx, tokens[1].kind == TokenKind::Identifier && tokens[1].location.column == 18);
    }
    {
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("2.5f 2.5 3 10f", "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::FloatLiteral && tokens[0].floatValue == 2.5);
        TEST_CHECK(ctx, tokens[1].kind == TokenKind::FloatLiteral && tokens[1].floatValue == 2.5);
        TEST_CHECK(ctx, tokens[2].kind == TokenKind::IntLiteral && tokens[2].intValue == 3);
        TEST_CHECK(ctx, tokens[3].kind == TokenKind::FloatLiteral && tokens[3].floatValue == 10.0);
    }
    {
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex(R"("a\nb\t\\\"c")", "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::StringLiteral);
        TEST_CHECK(ctx, tokens[0].text == std::string("a\nb\t\\\"c"));
    }
    {
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("+= -= *= /= == != <= >= && || ! % .", "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, tokens[0].kind == TokenKind::PlusAssign);
        TEST_CHECK(ctx, tokens[1].kind == TokenKind::MinusAssign);
        TEST_CHECK(ctx, tokens[2].kind == TokenKind::StarAssign);
        TEST_CHECK(ctx, tokens[3].kind == TokenKind::SlashAssign);
        TEST_CHECK(ctx, tokens[4].kind == TokenKind::Equal);
        TEST_CHECK(ctx, tokens[5].kind == TokenKind::NotEqual);
        TEST_CHECK(ctx, tokens[6].kind == TokenKind::LessEqual);
        TEST_CHECK(ctx, tokens[7].kind == TokenKind::GreaterEqual);
        TEST_CHECK(ctx, tokens[8].kind == TokenKind::AndAnd);
        TEST_CHECK(ctx, tokens[9].kind == TokenKind::OrOr);
        TEST_CHECK(ctx, tokens[10].kind == TokenKind::Not);
        TEST_CHECK(ctx, tokens[11].kind == TokenKind::Percent);
        TEST_CHECK(ctx, tokens[12].kind == TokenKind::Dot);
    }
    {
        // An unusable character is reported and dropped; everything after
        // it is still lexed, so one stray symbol does not blind the rest
        // of the pass.
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("int x\n= $ 7;", "<test>", diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
        TEST_CHECK(ctx, diagnostics.entries.size() == 1);
        TEST_CHECK(ctx, diagnostics.entries[0].location.line == 2 && diagnostics.entries[0].location.column == 3);
        TEST_CHECK(ctx, tokens.size() == 6);
        TEST_CHECK(ctx, tokens[3].kind == TokenKind::IntLiteral && tokens[3].intValue == 7);
        TEST_CHECK(ctx, tokens[4].kind == TokenKind::Semicolon);
        TEST_CHECK(ctx, tokens[5].kind == TokenKind::EndOfFile);
    }
    {
        DiagnosticList diagnostics;
        Lex("string s = \"unfinished;", "<test>", diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
    {
        // A file saved with a UTF-8 byte order mark. The marker is not
        // source text and no editor shows it, so lexing it as a character
        // would report "unexpected character" on line 1 of a file that
        // looks entirely ordinary to whoever wrote it.
        DiagnosticList diagnostics;
        std::vector<Token> tokens = Lex("\xEF\xBB\xBF" "int x = 7;", "<test>", diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());

        // And the columns still match what an editor shows -- counting
        // the marker would silently shift every position on line 1.
        TEST_CHECK(ctx, tokens[0].location.line == 1 && tokens[0].location.column == 1);
    }
}
