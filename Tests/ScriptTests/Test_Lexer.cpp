// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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
