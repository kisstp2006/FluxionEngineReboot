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
    KwClass, KwInterface, KwStruct, KwEnum, KwStatic, KwVar, KwVirtual, KwOverride,

    // Value and control-flow keywords.
    KwTrue, KwFalse, KwNull, KwThis, KwBase, KwNew,
    KwIf, KwElse, KwWhile, KwFor, KwForeach, KwIn, KwReturn, KwBreak, KwContinue,

    // Punctuation and operators.
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Semicolon, Colon, Dot,
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
