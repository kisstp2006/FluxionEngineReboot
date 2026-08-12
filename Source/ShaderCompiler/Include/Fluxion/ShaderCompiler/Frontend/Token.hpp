#pragma once

#include <Fluxion/ShaderCompiler/Diagnostics.hpp>

#include <string>

namespace Fluxion::ShaderCompiler
{

enum class TokenKind
{
    EndOfFile,
    Identifier,
    IntLiteral,
    FloatLiteral,

    // Type keywords -- .NET/C#-flavored spelling only (Vector2/3/4,
    // Matrix3x3/4x4, Texture2D/Cube); float/int/uint/bool already match
    // C#'s own spelling verbatim.
    KwVoid, KwBool, KwInt, KwUint, KwFloat,
    KwVector2, KwVector3, KwVector4, KwMatrix3x3, KwMatrix4x4,
    KwTexture2D, KwTextureCube,

    // Value/control-flow keywords.
    KwTrue, KwFalse,
    KwIf, KwElse, KwFor, KwWhile, KwReturn,
    KwConst, KwStruct,

    // Punctuation/operators.
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Semicolon, Colon, Question, Dot,
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
    Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual,
    AndAnd, OrOr, Not,
    Hash,
};

struct Token
{
    TokenKind kind = TokenKind::EndOfFile;
    std::string text;
    double numberValue = 0.0;
    SourceLocation location;
};

} // namespace Fluxion::ShaderCompiler
