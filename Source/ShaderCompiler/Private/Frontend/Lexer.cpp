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

#include <Fluxion/ShaderCompiler/Frontend/Lexer.hpp>

#include <cctype>
#include <unordered_map>

namespace Fluxion::ShaderCompiler
{

namespace
{

const std::unordered_map<std::string, TokenKind>& Keywords()
{
    static const std::unordered_map<std::string, TokenKind> table = {
        { "void", TokenKind::KwVoid }, { "bool", TokenKind::KwBool },
        { "int", TokenKind::KwInt }, { "uint", TokenKind::KwUint },
        { "float", TokenKind::KwFloat },
        { "Vector2", TokenKind::KwVector2 }, { "Vector3", TokenKind::KwVector3 }, { "Vector4", TokenKind::KwVector4 },
        { "Matrix3x3", TokenKind::KwMatrix3x3 }, { "Matrix4x4", TokenKind::KwMatrix4x4 },
        { "Texture2D", TokenKind::KwTexture2D }, { "TextureCube", TokenKind::KwTextureCube },
        { "true", TokenKind::KwTrue }, { "false", TokenKind::KwFalse },
        { "if", TokenKind::KwIf }, { "else", TokenKind::KwElse },
        { "for", TokenKind::KwFor }, { "while", TokenKind::KwWhile },
        { "return", TokenKind::KwReturn },
        { "discard", TokenKind::KwDiscard },
        { "const", TokenKind::KwConst }, { "struct", TokenKind::KwStruct },
    };
    return table;
}

class LexerState
{
public:
    LexerState(const std::string& source, std::string fileName, DiagnosticList& diagnostics)
        : m_source(source), m_fileName(std::move(fileName)), m_diagnostics(diagnostics)
    {
    }

    std::vector<Token> Run()
    {
        std::vector<Token> tokens;
        for (;;)
        {
            SkipWhitespaceAndComments();
            if (AtEnd())
            {
                tokens.push_back(MakeToken(TokenKind::EndOfFile, ""));
                break;
            }

            SourceLocation start = CurrentLocation();
            char c = Peek();

            if (std::isalpha((unsigned char)c) || c == '_')
            {
                tokens.push_back(LexIdentifierOrKeyword(start));
                continue;
            }
            if (std::isdigit((unsigned char)c) || (c == '.' && std::isdigit((unsigned char)Peek(1))))
            {
                tokens.push_back(LexNumber(start));
                continue;
            }

            Token punct;
            if (LexPunctuation(start, punct))
            {
                tokens.push_back(punct);
                continue;
            }

            m_diagnostics.AddError(start, std::string("unexpected character '") + c + "'");
            Advance();
        }
        return tokens;
    }

private:
    const std::string& m_source;
    std::string m_fileName;
    DiagnosticList& m_diagnostics;
    size_t m_cursor = 0;
    unsigned int m_line = 1;
    unsigned int m_column = 1;

    bool AtEnd() const { return m_cursor >= m_source.size(); }
    char Peek(size_t offset = 0) const
    {
        size_t i = m_cursor + offset;
        return i < m_source.size() ? m_source[i] : '\0';
    }

    char Advance()
    {
        char c = m_source[m_cursor++];
        if (c == '\n') { ++m_line; m_column = 1; }
        else { ++m_column; }
        return c;
    }

    SourceLocation CurrentLocation() const { return SourceLocation{ m_fileName, m_line, m_column }; }

    Token MakeToken(TokenKind kind, std::string text, SourceLocation location = {})
    {
        Token token;
        token.kind = kind;
        token.text = std::move(text);
        token.location = location.file.empty() ? CurrentLocation() : location;
        return token;
    }

    void SkipWhitespaceAndComments()
    {
        for (;;)
        {
            if (AtEnd()) return;
            char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { Advance(); continue; }
            if (c == '/' && Peek(1) == '/')
            {
                while (!AtEnd() && Peek() != '\n') Advance();
                continue;
            }
            if (c == '/' && Peek(1) == '*')
            {
                Advance(); Advance();
                while (!AtEnd() && !(Peek() == '*' && Peek(1) == '/')) Advance();
                if (!AtEnd()) { Advance(); Advance(); }
                continue;
            }
            return;
        }
    }

    Token LexIdentifierOrKeyword(SourceLocation start)
    {
        std::string text;
        while (!AtEnd() && (std::isalnum((unsigned char)Peek()) || Peek() == '_'))
            text += Advance();

        const auto& keywords = Keywords();
        auto it = keywords.find(text);
        TokenKind kind = (it != keywords.end()) ? it->second : TokenKind::Identifier;
        return MakeToken(kind, text, start);
    }

    Token LexNumber(SourceLocation start)
    {
        std::string text;
        bool isFloat = false;
        while (!AtEnd() && std::isdigit((unsigned char)Peek())) text += Advance();
        // Both "2." (no digits after the point) and ".5"/"1.5" (digits
        // before and/or after) are valid float literals in this
        // language, matching common GLSL source style.
        if (!AtEnd() && Peek() == '.')
        {
            isFloat = true;
            text += Advance();
            while (!AtEnd() && std::isdigit((unsigned char)Peek())) text += Advance();
        }
        // Scientific notation (e.g. "1.0e6", "1e-6").
        if (!AtEnd() && (Peek() == 'e' || Peek() == 'E') &&
            (std::isdigit((unsigned char)Peek(1)) || ((Peek(1) == '+' || Peek(1) == '-') && std::isdigit((unsigned char)Peek(2)))))
        {
            isFloat = true;
            text += Advance();
            if (Peek() == '+' || Peek() == '-') text += Advance();
            while (!AtEnd() && std::isdigit((unsigned char)Peek())) text += Advance();
        }
        // Trailing 'f'/'F' float suffix (e.g. "1.0f"), consumed but not
        // kept in the numeric text -- the token kind already says Float.
        if (!AtEnd() && (Peek() == 'f' || Peek() == 'F'))
        {
            isFloat = true;
            Advance();
        }

        Token token = MakeToken(isFloat ? TokenKind::FloatLiteral : TokenKind::IntLiteral, text, start);
        token.numberValue = std::strtod(text.c_str(), nullptr);
        return token;
    }

    bool LexPunctuation(SourceLocation start, Token& outToken)
    {
        char c = Peek();
        auto two = [&](char second, TokenKind kind, const char* text) -> bool
        {
            if (Peek(1) == second)
            {
                Advance(); Advance();
                outToken = MakeToken(kind, text, start);
                return true;
            }
            return false;
        };

        switch (c)
        {
            case '(': Advance(); outToken = MakeToken(TokenKind::LParen, "(", start); return true;
            case ')': Advance(); outToken = MakeToken(TokenKind::RParen, ")", start); return true;
            case '{': Advance(); outToken = MakeToken(TokenKind::LBrace, "{", start); return true;
            case '}': Advance(); outToken = MakeToken(TokenKind::RBrace, "}", start); return true;
            case '[': Advance(); outToken = MakeToken(TokenKind::LBracket, "[", start); return true;
            case ']': Advance(); outToken = MakeToken(TokenKind::RBracket, "]", start); return true;
            case ',': Advance(); outToken = MakeToken(TokenKind::Comma, ",", start); return true;
            case ';': Advance(); outToken = MakeToken(TokenKind::Semicolon, ";", start); return true;
            case ':': Advance(); outToken = MakeToken(TokenKind::Colon, ":", start); return true;
            case '?': Advance(); outToken = MakeToken(TokenKind::Question, "?", start); return true;
            case '.': Advance(); outToken = MakeToken(TokenKind::Dot, ".", start); return true;
            case '#': Advance(); outToken = MakeToken(TokenKind::Hash, "#", start); return true;
            case '+': if (two('=', TokenKind::PlusAssign, "+=")) return true; Advance(); outToken = MakeToken(TokenKind::Plus, "+", start); return true;
            case '-': if (two('=', TokenKind::MinusAssign, "-=")) return true; Advance(); outToken = MakeToken(TokenKind::Minus, "-", start); return true;
            case '*': if (two('=', TokenKind::StarAssign, "*=")) return true; Advance(); outToken = MakeToken(TokenKind::Star, "*", start); return true;
            case '/': if (two('=', TokenKind::SlashAssign, "/=")) return true; Advance(); outToken = MakeToken(TokenKind::Slash, "/", start); return true;
            case '%': Advance(); outToken = MakeToken(TokenKind::Percent, "%", start); return true;
            case '=': if (two('=', TokenKind::Equal, "==")) return true; Advance(); outToken = MakeToken(TokenKind::Assign, "=", start); return true;
            case '!': if (two('=', TokenKind::NotEqual, "!=")) return true; Advance(); outToken = MakeToken(TokenKind::Not, "!", start); return true;
            case '<': if (two('=', TokenKind::LessEqual, "<=")) return true; Advance(); outToken = MakeToken(TokenKind::Less, "<", start); return true;
            case '>': if (two('=', TokenKind::GreaterEqual, ">=")) return true; Advance(); outToken = MakeToken(TokenKind::Greater, ">", start); return true;
            case '&': if (two('&', TokenKind::AndAnd, "&&")) return true; break;
            case '|': if (two('|', TokenKind::OrOr, "||")) return true; break;
            default: break;
        }
        return false;
    }
};

} // namespace

// A UTF-8 BOM is an encoding marker rather than source text, and the
// editors that write one do not display it. Lexing it as a character
// produces "unexpected character" on line 1 column 1 of a file that
// looks, to whoever wrote it, completely ordinary.
//
// The preprocessor strips one too, so text arriving through it has none
// left. This is here because Lex is its own entry point and a caller
// that already has preprocessed text is free to call it directly.
bool StartsWithUtf8Bom(const std::string& source)
{
    return source.size() >= 3 &&
           (unsigned char)source[0] == 0xEFu &&
           (unsigned char)source[1] == 0xBBu &&
           (unsigned char)source[2] == 0xBFu;
}

std::vector<Token> Lex(const std::string& source, const std::string& fileName, DiagnosticList& diagnostics)
{
    if (!StartsWithUtf8Bom(source))
    {
        LexerState lexer(source, fileName, diagnostics);
        return lexer.Run();
    }

    // Dropped rather than skipped over, so that reported columns match
    // what an editor shows -- it does not count the marker either.
    const std::string withoutBom = source.substr(3);
    LexerState lexer(withoutBom, fileName, diagnostics);
    return lexer.Run();
}

} // namespace Fluxion::ShaderCompiler
