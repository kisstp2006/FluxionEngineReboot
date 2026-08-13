#include <Fluxion/Script/Compiler/Lexer.hpp>

#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace Fluxion::Script
{

namespace
{

const std::unordered_map<std::string, TokenKind>& Keywords()
{
    static const std::unordered_map<std::string, TokenKind> table = {
        { "void", TokenKind::KwVoid },
        { "bool", TokenKind::KwBool },
        { "int", TokenKind::KwInt },
        { "float", TokenKind::KwFloat },
        { "string", TokenKind::KwString },
        { "class", TokenKind::KwClass },
        { "interface", TokenKind::KwInterface },
        { "struct", TokenKind::KwStruct },
        { "enum", TokenKind::KwEnum },
        { "static", TokenKind::KwStatic },
        { "var", TokenKind::KwVar },
        { "virtual", TokenKind::KwVirtual },
        { "override", TokenKind::KwOverride },
        { "true", TokenKind::KwTrue },
        { "false", TokenKind::KwFalse },
        { "null", TokenKind::KwNull },
        { "this", TokenKind::KwThis },
        { "base", TokenKind::KwBase },
        { "new", TokenKind::KwNew },
        { "if", TokenKind::KwIf },
        { "else", TokenKind::KwElse },
        { "while", TokenKind::KwWhile },
        { "for", TokenKind::KwFor },
        { "foreach", TokenKind::KwForeach },
        { "in", TokenKind::KwIn },
        { "return", TokenKind::KwReturn },
        { "break", TokenKind::KwBreak },
        { "continue", TokenKind::KwContinue },
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
                tokens.push_back(MakeToken(TokenKind::EndOfFile, "", CurrentLocation()));
                break;
            }

            SourceLocation start = CurrentLocation();
            char c = Peek();

            if (std::isalpha((unsigned char)c) || c == '_')
            {
                tokens.push_back(LexIdentifierOrKeyword(start));
                continue;
            }
            if (std::isdigit((unsigned char)c))
            {
                tokens.push_back(LexNumber(start));
                continue;
            }
            if (c == '"')
            {
                tokens.push_back(LexString(start));
                continue;
            }

            Token punctuation;
            if (LexPunctuation(start, punctuation))
            {
                tokens.push_back(punctuation);
                continue;
            }

            // Nothing here is recoverable in the usual sense, but giving
            // up would hide every later problem in the file, so the
            // offending character is dropped and lexing carries on.
            m_diagnostics.AddError(start, std::string("unexpected character '") + c + "' in source");
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
        size_t index = m_cursor + offset;
        return index < m_source.size() ? m_source[index] : '\0';
    }

    char Advance()
    {
        char c = m_source[m_cursor++];
        if (c == '\n') { ++m_line; m_column = 1; }
        else { ++m_column; }
        return c;
    }

    SourceLocation CurrentLocation() const { return SourceLocation{ m_fileName, m_line, m_column }; }

    Token MakeToken(TokenKind kind, std::string text, SourceLocation location)
    {
        Token token;
        token.kind = kind;
        token.text = std::move(text);
        token.location = std::move(location);
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
                SourceLocation start = CurrentLocation();
                Advance();
                Advance();
                while (!AtEnd() && !(Peek() == '*' && Peek(1) == '/')) Advance();
                if (AtEnd())
                {
                    m_diagnostics.AddError(start, "block comment is never closed");
                    return;
                }
                Advance();
                Advance();
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
        return MakeToken(kind, std::move(text), std::move(start));
    }

    Token LexNumber(SourceLocation start)
    {
        std::string text;
        bool isFloat = false;

        while (!AtEnd() && std::isdigit((unsigned char)Peek())) text += Advance();

        // A dot only continues the number when a digit follows it, so
        // that a member access on a numeric-looking expression is still
        // lexed as a separate dot.
        if (Peek() == '.' && std::isdigit((unsigned char)Peek(1)))
        {
            isFloat = true;
            text += Advance();
            while (!AtEnd() && std::isdigit((unsigned char)Peek())) text += Advance();
        }

        // A trailing suffix marks the literal as a float; it is consumed
        // but kept out of the numeric text.
        if (Peek() == 'f' || Peek() == 'F')
        {
            isFloat = true;
            Advance();
        }

        Token token = MakeToken(isFloat ? TokenKind::FloatLiteral : TokenKind::IntLiteral, text, start);
        if (isFloat)
        {
            token.floatValue = std::strtod(text.c_str(), nullptr);
        }
        else
        {
            const long long parsed = std::strtoll(text.c_str(), nullptr, 10);
            if (parsed > 2147483647LL)
            {
                m_diagnostics.AddError(start, "integer literal '" + text + "' does not fit in an int");
                token.intValue = 2147483647LL;
            }
            else
            {
                token.intValue = parsed;
            }
        }
        return token;
    }

    Token LexString(SourceLocation start)
    {
        Advance(); // opening quote

        std::string value;
        for (;;)
        {
            if (AtEnd() || Peek() == '\n')
            {
                m_diagnostics.AddError(start, "string literal is never closed");
                break;
            }
            char c = Advance();
            if (c == '"') break;
            if (c != '\\')
            {
                value += c;
                continue;
            }

            if (AtEnd())
            {
                m_diagnostics.AddError(start, "string literal is never closed");
                break;
            }

            SourceLocation escapeLocation = CurrentLocation();
            char escaped = Advance();
            switch (escaped)
            {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '0': value += '\0'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                default:
                    m_diagnostics.AddError(escapeLocation, std::string("unknown escape sequence '\\") + escaped + "' in string literal");
                    value += escaped;
                    break;
            }
        }

        return MakeToken(TokenKind::StringLiteral, std::move(value), std::move(start));
    }

    bool LexPunctuation(SourceLocation start, Token& outToken)
    {
        char c = Peek();

        auto two = [&](char second, TokenKind kind, const char* text) -> bool
        {
            if (Peek(1) != second) return false;
            Advance();
            Advance();
            outToken = MakeToken(kind, text, start);
            return true;
        };

        auto one = [&](TokenKind kind, const char* text) -> bool
        {
            Advance();
            outToken = MakeToken(kind, text, start);
            return true;
        };

        switch (c)
        {
            case '(': return one(TokenKind::LParen, "(");
            case ')': return one(TokenKind::RParen, ")");
            case '{': return one(TokenKind::LBrace, "{");
            case '}': return one(TokenKind::RBrace, "}");
            case '[': return one(TokenKind::LBracket, "[");
            case ']': return one(TokenKind::RBracket, "]");
            case ',': return one(TokenKind::Comma, ",");
            case ';': return one(TokenKind::Semicolon, ";");
            case ':': return one(TokenKind::Colon, ":");
            case '.': return one(TokenKind::Dot, ".");
            case '+': if (two('=', TokenKind::PlusAssign, "+=")) return true; return one(TokenKind::Plus, "+");
            case '-': if (two('=', TokenKind::MinusAssign, "-=")) return true; return one(TokenKind::Minus, "-");
            case '*': if (two('=', TokenKind::StarAssign, "*=")) return true; return one(TokenKind::Star, "*");
            case '/': if (two('=', TokenKind::SlashAssign, "/=")) return true; return one(TokenKind::Slash, "/");
            case '%': return one(TokenKind::Percent, "%");
            case '=': if (two('=', TokenKind::Equal, "==")) return true; return one(TokenKind::Assign, "=");
            case '!': if (two('=', TokenKind::NotEqual, "!=")) return true; return one(TokenKind::Not, "!");
            case '<': if (two('=', TokenKind::LessEqual, "<=")) return true; return one(TokenKind::Less, "<");
            case '>': if (two('=', TokenKind::GreaterEqual, ">=")) return true; return one(TokenKind::Greater, ">");
            case '&': return two('&', TokenKind::AndAnd, "&&");
            case '|': return two('|', TokenKind::OrOr, "||");
            default: return false;
        }
    }
};

} // namespace

std::vector<Token> Lex(const std::string& source, const std::string& fileName, DiagnosticList& diagnostics)
{
    LexerState lexer(source, fileName, diagnostics);
    return lexer.Run();
}

} // namespace Fluxion::Script
