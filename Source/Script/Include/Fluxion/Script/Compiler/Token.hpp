#pragma once

#include <Fluxion/Script/Diagnostics.hpp>

#include <string>

namespace Fluxion::Script
{

enum class TokenKind
{
    EndOfFile,
    Identifier,

    IntLiteral,
    FloatLiteral,
    StringLiteral,

    // Type keywords.
    KwVoid, KwBool, KwInt, KwFloat, KwString,

    // Declaration keywords.
    KwClass, KwStatic, KwVar,

    // Value and control-flow keywords.
    KwTrue, KwFalse,
    KwIf, KwElse, KwWhile, KwFor, KwReturn, KwBreak, KwContinue,

    // Punctuation and operators.
    LParen, RParen, LBrace, RBrace,
    Comma, Semicolon, Dot,
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
    Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual,
    AndAnd, OrOr, Not,
};

struct Token
{
    TokenKind kind = TokenKind::EndOfFile;

    // For an identifier or a keyword this is the spelling; for a string
    // literal it is the decoded text, with escapes already resolved and
    // the surrounding quotes removed.
    std::string text;

    long long intValue = 0;
    double floatValue = 0.0;

    SourceLocation location;
};

} // namespace Fluxion::Script
