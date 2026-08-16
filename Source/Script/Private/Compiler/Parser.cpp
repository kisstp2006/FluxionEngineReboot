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

#include <Fluxion/Script/Compiler/Parser.hpp>

#include <utility>

namespace Fluxion::Script
{

namespace
{

bool IsBuiltInTypeToken(TokenKind kind)
{
    switch (kind)
    {
        case TokenKind::KwVoid:
        case TokenKind::KwBool:
        case TokenKind::KwInt:
        case TokenKind::KwFloat:
        case TokenKind::KwString:
            return true;
        default:
            return false;
    }
}

ValueType TypeFromToken(TokenKind kind)
{
    switch (kind)
    {
        case TokenKind::KwBool: return ValueType::Bool;
        case TokenKind::KwInt: return ValueType::Int;
        case TokenKind::KwFloat: return ValueType::Float;
        case TokenKind::KwString: return ValueType::String;
        case TokenKind::KwVoid: return ValueType::Void;
        default: return ValueType::Unknown;
    }
}

class ParserState
{
public:
    ParserState(const std::vector<Token>& tokens, DiagnosticList& diagnostics)
        : m_tokens(tokens), m_diagnostics(diagnostics)
    {
    }

    Program Run()
    {
        Program program;
        while (!AtEnd())
        {
            const size_t before = m_position;
            DeclPtr decl = ParseTypeDecl();
            if (decl)
            {
                program.declarations.push_back(std::move(decl));
                continue;
            }

            SynchronizeToNextDecl();
            // A failed declaration that consumed nothing and landed on
            // its own anchor would spin here forever.
            if (m_position == before) Advance();
        }
        return program;
    }

private:
    const std::vector<Token>& m_tokens;
    DiagnosticList& m_diagnostics;
    size_t m_position = 0;

    const Token& Current() const { return m_tokens[m_position]; }
    const Token& Previous() const { return m_tokens[m_position - 1]; }
    TokenKind PeekKind(size_t offset) const
    {
        size_t index = m_position + offset;
        return index < m_tokens.size() ? m_tokens[index].kind : TokenKind::EndOfFile;
    }

    bool Check(TokenKind kind) const { return Current().kind == kind; }
    bool AtEnd() const { return Check(TokenKind::EndOfFile); }

    const Token& Advance()
    {
        if (!AtEnd()) ++m_position;
        return Previous();
    }

    bool Match(TokenKind kind)
    {
        if (!Check(kind)) return false;
        Advance();
        return true;
    }

    const Token* Expect(TokenKind kind, const char* what)
    {
        if (Check(kind)) return &Advance();
        Error(Current().location, std::string("expected ") + what + ", found '" + Describe(Current()) + "'");
        return nullptr;
    }

    static std::string Describe(const Token& token)
    {
        if (token.kind == TokenKind::EndOfFile) return "end of file";
        if (token.kind == TokenKind::StringLiteral) return "a string literal";
        return token.text;
    }

    void Error(SourceLocation location, std::string message)
    {
        m_diagnostics.AddError(std::move(location), std::move(message));
    }

    // A type declaration always begins the same way, so those openers are
    // the only anchors worth trusting after an error.
    void SynchronizeToNextDecl()
    {
        while (!AtEnd())
        {
            const TokenKind kind = Current().kind;
            if (kind == TokenKind::KwClass || kind == TokenKind::KwInterface) return;
            if (kind == TokenKind::KwStruct || kind == TokenKind::KwEnum) return;
            if (kind == TokenKind::KwStatic && PeekKind(1) == TokenKind::KwClass) return;
            Advance();
        }
    }

    // Stops on whichever comes first at the current brace nesting: the
    // start of the next member, or the brace that closes the class.
    void SynchronizeToNextMember()
    {
        int depth = 0;
        while (!AtEnd())
        {
            TokenKind kind = Current().kind;
            if (depth == 0 && IsMemberAnchor(kind)) return;
            if (kind == TokenKind::LBrace) ++depth;
            else if (kind == TokenKind::RBrace) --depth;
            Advance();
        }
    }

    static bool IsMemberAnchor(TokenKind kind)
    {
        switch (kind)
        {
            case TokenKind::RBrace:
            case TokenKind::KwStatic:
            case TokenKind::KwVirtual:
            case TokenKind::KwOverride:
                return true;
            default:
                return IsBuiltInTypeToken(kind);
        }
    }

    // --- Types ------------------------------------------------------------

    // Never reports anything and never leaves the cursor moved when it
    // fails, so a caller may also use it to decide whether what follows
    // is a type at all. The caller reports the failure it cares about.
    bool ParseTypeRef(TypeRef& outType)
    {
        const size_t start = m_position;
        TypeRef parsed;

        if (IsBuiltInTypeToken(Current().kind))
        {
            parsed.type = TypeFromToken(Advance().kind);
        }
        else if (Check(TokenKind::Identifier))
        {
            parsed.type = ValueType::Object;
            parsed.name = Advance().text;

            if (Check(TokenKind::Less) && !ParseTypeArguments(parsed.typeArgs))
            {
                m_position = start;
                return false;
            }
        }
        else
        {
            return false;
        }

        parsed.arrayDepth = ParseArraySuffix();
        outType = std::move(parsed);
        return true;
    }

    // Consumes the `<A, B>` after a type name. The cursor is left wherever
    // it got to on failure; every caller rolls back for itself.
    bool ParseTypeArguments(std::vector<TypeRef>& outArgs)
    {
        Advance(); // '<'
        do
        {
            TypeRef argument;
            if (!ParseTypeRef(argument)) return false;
            outArgs.push_back(std::move(argument));
        } while (Match(TokenKind::Comma));

        // A nested argument list ends in two adjacent '>' characters,
        // which arrive as two separate tokens -- there is no shift
        // operator for them to have been lexed as instead.
        return Match(TokenKind::Greater);
    }

    // Counts the trailing `[]` pairs. A '[' that something other than ']'
    // follows belongs to an index expression, not to a type, and is left
    // alone.
    u32 ParseArraySuffix()
    {
        u32 depth = 0;
        while (Check(TokenKind::LBracket) && PeekKind(1) == TokenKind::RBracket)
        {
            Advance();
            Advance();
            ++depth;
        }
        return depth;
    }

    // --- Annotations ------------------------------------------------------

    // Reads every `[...]` group standing in front of a declaration. An
    // empty list is the ordinary case and costs nothing, so this is
    // called unconditionally wherever a declaration may begin.
    bool ParseAttributes(std::vector<AttributeNode>& outAttributes)
    {
        while (Check(TokenKind::LBracket))
        {
            // `[` also opens an index, but never at the point a
            // declaration starts, which is the only place this runs.
            Advance();
            do
            {
                AttributeNode attribute;
                attribute.location = Current().location;

                const Token* name = Expect(TokenKind::Identifier, "an annotation name");
                if (!name) return false;
                attribute.name = name->text;

                if (Match(TokenKind::LParen))
                {
                    if (!Check(TokenKind::RParen))
                    {
                        do
                        {
                            AttributeArgNode argument;
                            if (!ParseAttributeArgument(argument)) return false;
                            attribute.args.push_back(std::move(argument));
                        } while (Match(TokenKind::Comma));
                    }
                    if (!Expect(TokenKind::RParen, "')'")) return false;
                }

                outAttributes.push_back(std::move(attribute));
            } while (Match(TokenKind::Comma));

            if (!Expect(TokenKind::RBracket, "']'")) return false;
        }
        return true;
    }

    bool ParseAttributeArgument(AttributeArgNode& outArgument)
    {
        outArgument.location = Current().location;

        // `typeof` is not a keyword: it is spelled like any other name and
        // is only recognized here, where a type is the one other thing an
        // annotation can be given.
        if (Check(TokenKind::Identifier) && Current().text == "typeof" && PeekKind(1) == TokenKind::LParen)
        {
            Advance();
            Advance();
            if (!ParseTypeRef(outArgument.typeValue))
            {
                Error(Current().location, "expected a type name inside 'typeof', found '" + Describe(Current()) + "'");
                return false;
            }
            if (!Expect(TokenKind::RParen, "')'")) return false;
            outArgument.kind = AttributeArgumentKind::Class;
            return true;
        }

        const bool negated = Match(TokenKind::Minus);

        if (Match(TokenKind::IntLiteral))
        {
            outArgument.kind = AttributeArgumentKind::Int;
            outArgument.intValue = negated ? -Previous().intValue : Previous().intValue;
            return true;
        }
        if (Match(TokenKind::FloatLiteral))
        {
            outArgument.kind = AttributeArgumentKind::Float;
            outArgument.floatValue = negated ? -Previous().floatValue : Previous().floatValue;
            return true;
        }
        if (!negated && Match(TokenKind::StringLiteral))
        {
            outArgument.kind = AttributeArgumentKind::String;
            outArgument.stringValue = Previous().text;
            return true;
        }

        Error(outArgument.location, "an annotation takes a number, a piece of text or 'typeof(Name)', found '" + Describe(Current()) + "'");
        return false;
    }

    // --- Declarations ---------------------------------------------------

    DeclPtr ParseTypeDecl()
    {
        std::vector<AttributeNode> attributes;
        if (!ParseAttributes(attributes)) return nullptr;

        SourceLocation loc = Current().location;

        bool isStatic = false;
        bool isInterface = false;
        bool isStruct = false;

        if (Check(TokenKind::KwStatic))
        {
            Advance();
            isStatic = true;
            if (!Expect(TokenKind::KwClass, "'class'")) return nullptr;
        }
        else if (Check(TokenKind::KwClass))
        {
            Advance();
        }
        else if (Check(TokenKind::KwInterface))
        {
            Advance();
            isInterface = true;
        }
        else if (Check(TokenKind::KwStruct))
        {
            Advance();
            isStruct = true;
        }
        else if (Check(TokenKind::KwEnum))
        {
            Advance();
            return ParseEnumDecl(std::move(attributes), loc);
        }
        else
        {
            Error(loc, "expected a class, interface, struct or enum declaration, found '" + Describe(Current()) + "'");
            Advance();
            return nullptr;
        }

        const char* wanted = "a class name";
        if (isInterface) wanted = "an interface name";
        else if (isStruct) wanted = "a struct name";

        const Token* nameToken = Expect(TokenKind::Identifier, wanted);
        if (!nameToken) return nullptr;

        auto decl = std::make_unique<ClassDecl>();
        decl->location = loc;
        decl->name = nameToken->text;
        decl->isStatic = isStatic;
        decl->isInterface = isInterface;
        decl->isStruct = isStruct;
        decl->attributes = std::move(attributes);

        if (Check(TokenKind::Less) && !ParseTypeParameters(*decl)) return nullptr;

        if (Match(TokenKind::Colon))
        {
            do
            {
                const SourceLocation baseLoc = Current().location;
                TypeRef baseType;
                if (!ParseTypeRef(baseType))
                {
                    Error(baseLoc, "expected a base type name, found '" + Describe(Current()) + "'");
                    return nullptr;
                }
                decl->baseTypes.push_back(std::move(baseType));
                decl->baseLocations.push_back(baseLoc);
            } while (Match(TokenKind::Comma));
        }

        if (!Expect(TokenKind::LBrace, "'{'")) return nullptr;

        while (!Check(TokenKind::RBrace) && !AtEnd())
        {
            const size_t before = m_position;
            if (ParseMember(*decl)) continue;

            SynchronizeToNextMember();
            if (m_position == before) Advance();
        }

        if (!Expect(TokenKind::RBrace, "'}'")) return nullptr;
        return decl;
    }

    // The `<A, B>` after a declared name. Written out for every kind of
    // declaration that can carry one, including the kinds that go on to
    // be told they cannot -- reading it here is what lets the refusal
    // name the parameter and the place it was written.
    bool ParseTypeParameters(ClassDecl& decl)
    {
        Advance(); // '<'
        do
        {
            const SourceLocation paramLoc = Current().location;
            const Token* paramName = Expect(TokenKind::Identifier, "a type parameter name");
            if (!paramName) return false;
            decl.typeParams.push_back(paramName->text);
            decl.typeParamLocations.push_back(paramLoc);
        } while (Match(TokenKind::Comma));
        return Expect(TokenKind::Greater, "'>'") != nullptr;
    }

    // `enum Name { A, B = 10, C }`. A constant written without a number
    // continues from the one before it, so the whole set has its values
    // by the time parsing is done and nothing later has to work them out.
    DeclPtr ParseEnumDecl(std::vector<AttributeNode> attributes, SourceLocation loc)
    {
        const Token* nameToken = Expect(TokenKind::Identifier, "an enum name");
        if (!nameToken) return nullptr;

        auto decl = std::make_unique<ClassDecl>();
        decl->location = loc;
        decl->name = nameToken->text;
        decl->isEnum = true;
        decl->attributes = std::move(attributes);

        if (Check(TokenKind::Less) && !ParseTypeParameters(*decl)) return nullptr;

        if (Check(TokenKind::Colon))
        {
            Error(Current().location, "'" + decl->name + "' declares named constants, so it cannot declare a base list");
            return nullptr;
        }

        if (!Expect(TokenKind::LBrace, "'{'")) return nullptr;

        long long next = 0;
        while (!Check(TokenKind::RBrace) && !AtEnd())
        {
            EnumeratorDecl entry;
            entry.location = Current().location;

            const Token* constantName = Expect(TokenKind::Identifier, "a constant name");
            if (!constantName) return nullptr;
            entry.name = constantName->text;

            if (Match(TokenKind::Assign))
            {
                const bool negated = Match(TokenKind::Minus);
                const Token* literal = Expect(TokenKind::IntLiteral, "a whole number");
                if (!literal) return nullptr;
                entry.value = negated ? -literal->intValue : literal->intValue;
            }
            else
            {
                entry.value = next;
            }

            next = entry.value + 1;
            decl->enumerators.push_back(std::move(entry));

            // A trailing comma is allowed: the loop stops at the closing
            // brace whether or not one was written.
            if (!Match(TokenKind::Comma)) break;
        }

        if (!Expect(TokenKind::RBrace, "'}'")) return nullptr;
        return decl;
    }

    // Parses one member into `owner`, returning false when the member was
    // malformed and the caller has to recover.
    bool ParseMember(ClassDecl& owner)
    {
        std::vector<AttributeNode> attributes;
        if (!ParseAttributes(attributes)) return false;

        SourceLocation loc = Current().location;

        bool isStatic = Match(TokenKind::KwStatic);

        bool isVirtual = false;
        bool isOverride = false;
        if (Match(TokenKind::KwVirtual)) isVirtual = true;
        else if (Match(TokenKind::KwOverride)) isOverride = true;

        // A constructor is the one member with no return type: it is
        // named after its class and followed straight by a parameter
        // list.
        if (Check(TokenKind::Identifier) && Current().text == owner.name && PeekKind(1) == TokenKind::LParen)
        {
            if (!attributes.empty())
            {
                Error(loc, "a constructor cannot carry an annotation");
                return false;
            }
            return ParseConstructor(owner, loc, isStatic, isVirtual, isOverride);
        }

        TypeRef type;
        if (!ParseTypeRef(type))
        {
            Error(Current().location, "expected a member type, found '" + Describe(Current()) + "'");
            return false;
        }

        const Token* nameToken = Expect(TokenKind::Identifier, "a member name");
        if (!nameToken) return false;

        if (Check(TokenKind::LParen))
        {
            if (!attributes.empty())
            {
                Error(loc, "'" + nameToken->text + "' is a method, and only a class or a field may carry an annotation");
                return false;
            }
            return ParseMethod(owner, loc, std::move(type), nameToken->text, isStatic, isVirtual, isOverride);
        }

        // Anything that is not a parameter list makes this a field.
        if (isVirtual || isOverride)
        {
            Error(loc, "'" + nameToken->text + "' is a field, and a field cannot be declared 'virtual' or 'override'");
            return false;
        }
        if (!Expect(TokenKind::Semicolon, "';'")) return false;

        auto field = std::make_unique<FieldDecl>();
        field->location = loc;
        field->type = std::move(type);
        field->name = nameToken->text;
        field->attributes = std::move(attributes);
        if (isStatic)
        {
            Error(loc, "field '" + field->name + "' cannot be declared 'static'");
            return false;
        }
        owner.fields.push_back(std::move(field));
        return true;
    }

    bool ParseParameterList(MethodDecl& method)
    {
        if (!Expect(TokenKind::LParen, "'('")) return false;
        if (!Check(TokenKind::RParen))
        {
            do
            {
                SourceLocation paramLoc = Current().location;
                TypeRef paramType;
                if (!ParseTypeRef(paramType))
                {
                    Error(paramLoc, "expected a parameter type, found '" + Describe(Current()) + "'");
                    return false;
                }
                const Token* paramName = Expect(TokenKind::Identifier, "a parameter name");
                if (!paramName) return false;
                method.params.push_back(ParamDecl{ std::move(paramType), paramName->text, paramLoc });
            } while (Match(TokenKind::Comma));
        }
        return Expect(TokenKind::RParen, "')'") != nullptr;
    }

    bool ParseMethod(ClassDecl& owner, SourceLocation loc, TypeRef returnType, const std::string& name,
                     bool isStatic, bool isVirtual, bool isOverride)
    {
        auto decl = std::make_unique<MethodDecl>();
        decl->location = loc;
        decl->returnType = std::move(returnType);
        decl->name = name;
        decl->isStatic = isStatic;
        decl->isVirtual = isVirtual;
        decl->isOverride = isOverride;

        if (!ParseParameterList(*decl)) return false;

        // An interface names signatures and stops there; every other
        // method brings a body.
        if (owner.isInterface)
        {
            if (!Expect(TokenKind::Semicolon, "';'")) return false;
            owner.methods.push_back(std::move(decl));
            return true;
        }

        StmtPtr body = ParseBlock();
        if (!body) return false;
        decl->body = std::move(body);
        owner.methods.push_back(std::move(decl));
        return true;
    }

    bool ParseConstructor(ClassDecl& owner, SourceLocation loc, bool isStatic, bool isVirtual, bool isOverride)
    {
        auto decl = std::make_unique<MethodDecl>();
        decl->location = loc;
        decl->name = Advance().text; // the class name
        decl->isConstructor = true;
        decl->isStatic = isStatic;
        decl->returnType.type = ValueType::Void;

        if (isVirtual || isOverride)
        {
            Error(loc, "a constructor cannot be declared 'virtual' or 'override'");
            return false;
        }

        if (!ParseParameterList(*decl)) return false;

        if (Match(TokenKind::Colon))
        {
            decl->baseCallLocation = Previous().location;
            if (!Expect(TokenKind::KwBase, "'base'")) return false;
            if (!Expect(TokenKind::LParen, "'('")) return false;
            decl->hasBaseCall = true;

            if (!Check(TokenKind::RParen))
            {
                do
                {
                    ExprPtr arg = ParseExpression();
                    if (!arg) return false;
                    decl->baseArgs.push_back(std::move(arg));
                } while (Match(TokenKind::Comma));
            }
            if (!Expect(TokenKind::RParen, "')'")) return false;
        }

        StmtPtr body = ParseBlock();
        if (!body) return false;
        decl->body = std::move(body);
        owner.methods.push_back(std::move(decl));
        return true;
    }

    // --- Statements -------------------------------------------------------

    // A declared type is spelled the same way an expression can start, so
    // the only reliable test is to read a whole type and see whether a
    // variable name follows it. The attempt is rolled back either way.
    bool IsLocalDeclStart()
    {
        if (Check(TokenKind::KwVar)) return true;
        if (!IsBuiltInTypeToken(Current().kind) && !Check(TokenKind::Identifier)) return false;

        const size_t start = m_position;
        TypeRef probe;
        const bool looksLikeDecl = ParseTypeRef(probe) && Check(TokenKind::Identifier);
        m_position = start;
        return looksLikeDecl;
    }

    StmtPtr ParseStatement()
    {
        SourceLocation loc = Current().location;

        if (Check(TokenKind::LBrace)) return ParseBlock();
        if (Match(TokenKind::KwIf)) return ParseIf(loc);
        if (Match(TokenKind::KwWhile)) return ParseWhile(loc);
        if (Match(TokenKind::KwFor)) return ParseFor(loc);
        if (Match(TokenKind::KwForeach)) return ParseForEach(loc);
        if (Match(TokenKind::KwReturn)) return ParseReturn(loc);

        if (Match(TokenKind::KwBreak))
        {
            if (!Expect(TokenKind::Semicolon, "';'")) return nullptr;
            auto stmt = std::make_unique<BreakStmt>();
            stmt->location = loc;
            return stmt;
        }
        if (Match(TokenKind::KwContinue))
        {
            if (!Expect(TokenKind::Semicolon, "';'")) return nullptr;
            auto stmt = std::make_unique<ContinueStmt>();
            stmt->location = loc;
            return stmt;
        }

        if (IsLocalDeclStart()) return ParseLocalDecl(loc);

        auto stmt = std::make_unique<ExprStmt>();
        stmt->location = loc;
        stmt->expr = ParseExpression();
        if (!stmt->expr) return nullptr;
        if (!Expect(TokenKind::Semicolon, "';'")) return nullptr;
        return stmt;
    }

    StmtPtr ParseBlock()
    {
        SourceLocation loc = Current().location;
        if (!Expect(TokenKind::LBrace, "'{'")) return nullptr;

        auto block = std::make_unique<BlockStmt>();
        block->location = loc;

        while (!Check(TokenKind::RBrace) && !AtEnd())
        {
            StmtPtr stmt = ParseStatement();
            if (!stmt) return nullptr;
            block->statements.push_back(std::move(stmt));
        }

        if (!Expect(TokenKind::RBrace, "'}'")) return nullptr;
        return block;
    }

    StmtPtr ParseLocalDecl(SourceLocation loc)
    {
        auto stmt = std::make_unique<LocalDeclStmt>();
        stmt->location = loc;

        if (Match(TokenKind::KwVar))
        {
            stmt->inferred = true;
        }
        else if (!ParseTypeRef(stmt->declaredType))
        {
            Error(loc, "expected a variable type, found '" + Describe(Current()) + "'");
            return nullptr;
        }

        const Token* nameToken = Expect(TokenKind::Identifier, "a variable name");
        if (!nameToken) return nullptr;
        stmt->name = nameToken->text;

        if (Match(TokenKind::Assign))
        {
            stmt->initializer = ParseExpression();
            if (!stmt->initializer) return nullptr;
        }

        if (!Expect(TokenKind::Semicolon, "';'")) return nullptr;
        return stmt;
    }

    StmtPtr ParseIf(SourceLocation loc)
    {
        if (!Expect(TokenKind::LParen, "'('")) return nullptr;
        auto stmt = std::make_unique<IfStmt>();
        stmt->location = loc;

        stmt->condition = ParseExpression();
        if (!stmt->condition) return nullptr;
        if (!Expect(TokenKind::RParen, "')'")) return nullptr;

        stmt->thenBranch = ParseStatement();
        if (!stmt->thenBranch) return nullptr;

        if (Match(TokenKind::KwElse))
        {
            stmt->elseBranch = ParseStatement();
            if (!stmt->elseBranch) return nullptr;
        }
        return stmt;
    }

    StmtPtr ParseWhile(SourceLocation loc)
    {
        if (!Expect(TokenKind::LParen, "'('")) return nullptr;
        auto stmt = std::make_unique<WhileStmt>();
        stmt->location = loc;

        stmt->condition = ParseExpression();
        if (!stmt->condition) return nullptr;
        if (!Expect(TokenKind::RParen, "')'")) return nullptr;

        stmt->body = ParseStatement();
        if (!stmt->body) return nullptr;
        return stmt;
    }

    StmtPtr ParseFor(SourceLocation loc)
    {
        if (!Expect(TokenKind::LParen, "'('")) return nullptr;
        auto stmt = std::make_unique<ForStmt>();
        stmt->location = loc;

        if (Match(TokenKind::Semicolon))
        {
            // No initializer.
        }
        else if (IsLocalDeclStart())
        {
            SourceLocation initLoc = Current().location;
            stmt->init = ParseLocalDecl(initLoc);
            if (!stmt->init) return nullptr;
        }
        else
        {
            auto initStmt = std::make_unique<ExprStmt>();
            initStmt->location = Current().location;
            initStmt->expr = ParseExpression();
            if (!initStmt->expr) return nullptr;
            if (!Expect(TokenKind::Semicolon, "';'")) return nullptr;
            stmt->init = std::move(initStmt);
        }

        if (!Check(TokenKind::Semicolon))
        {
            stmt->condition = ParseExpression();
            if (!stmt->condition) return nullptr;
        }
        if (!Expect(TokenKind::Semicolon, "';'")) return nullptr;

        if (!Check(TokenKind::RParen))
        {
            stmt->step = ParseExpression();
            if (!stmt->step) return nullptr;
        }
        if (!Expect(TokenKind::RParen, "')'")) return nullptr;

        stmt->body = ParseStatement();
        if (!stmt->body) return nullptr;
        return stmt;
    }

    StmtPtr ParseForEach(SourceLocation loc)
    {
        if (!Expect(TokenKind::LParen, "'('")) return nullptr;

        auto stmt = std::make_unique<ForEachStmt>();
        stmt->location = loc;

        if (!ParseTypeRef(stmt->declaredType))
        {
            Error(Current().location, "expected a loop variable type, found '" + Describe(Current()) + "'");
            return nullptr;
        }

        const Token* nameToken = Expect(TokenKind::Identifier, "a loop variable name");
        if (!nameToken) return nullptr;
        stmt->name = nameToken->text;

        if (!Expect(TokenKind::KwIn, "'in'")) return nullptr;

        stmt->sequence = ParseExpression();
        if (!stmt->sequence) return nullptr;
        if (!Expect(TokenKind::RParen, "')'")) return nullptr;

        stmt->body = ParseStatement();
        if (!stmt->body) return nullptr;
        return stmt;
    }

    StmtPtr ParseReturn(SourceLocation loc)
    {
        auto stmt = std::make_unique<ReturnStmt>();
        stmt->location = loc;

        if (!Check(TokenKind::Semicolon))
        {
            stmt->value = ParseExpression();
            if (!stmt->value) return nullptr;
        }
        if (!Expect(TokenKind::Semicolon, "';'")) return nullptr;
        return stmt;
    }

    // --- Expressions ------------------------------------------------------

    ExprPtr ParseExpression() { return ParseAssignment(); }

    ExprPtr ParseAssignment()
    {
        ExprPtr lhs = ParseLogicalOr();
        if (!lhs) return nullptr;

        AssignOp op;
        switch (Current().kind)
        {
            case TokenKind::Assign: op = AssignOp::Assign; break;
            case TokenKind::PlusAssign: op = AssignOp::AddAssign; break;
            case TokenKind::MinusAssign: op = AssignOp::SubAssign; break;
            case TokenKind::StarAssign: op = AssignOp::MulAssign; break;
            case TokenKind::SlashAssign: op = AssignOp::DivAssign; break;
            default: return lhs;
        }

        SourceLocation loc = Current().location;
        Advance();

        // Recursing into the same level is what makes assignment
        // right-associative.
        ExprPtr rhs = ParseAssignment();
        if (!rhs) return nullptr;

        auto expr = std::make_unique<AssignExpr>();
        expr->location = loc;
        expr->op = op;
        expr->target = std::move(lhs);
        expr->value = std::move(rhs);
        return expr;
    }

    ExprPtr ParseBinaryLevel(ExprPtr (ParserState::*next)(), std::initializer_list<std::pair<TokenKind, BinaryOp>> operators)
    {
        ExprPtr lhs = (this->*next)();
        if (!lhs) return nullptr;

        for (;;)
        {
            bool matched = false;
            for (const auto& [tokenKind, binaryOp] : operators)
            {
                if (!Check(tokenKind)) continue;

                SourceLocation loc = Current().location;
                Advance();
                ExprPtr rhs = (this->*next)();
                if (!rhs) return nullptr;

                auto expr = std::make_unique<BinaryExpr>();
                expr->location = loc;
                expr->op = binaryOp;
                expr->lhs = std::move(lhs);
                expr->rhs = std::move(rhs);
                lhs = std::move(expr);
                matched = true;
                break;
            }
            if (!matched) return lhs;
        }
    }

    ExprPtr ParseLogicalOr()
    {
        return ParseBinaryLevel(&ParserState::ParseLogicalAnd, { { TokenKind::OrOr, BinaryOp::LogicalOr } });
    }

    ExprPtr ParseLogicalAnd()
    {
        return ParseBinaryLevel(&ParserState::ParseEquality, { { TokenKind::AndAnd, BinaryOp::LogicalAnd } });
    }

    ExprPtr ParseEquality()
    {
        return ParseBinaryLevel(&ParserState::ParseRelational, {
            { TokenKind::Equal, BinaryOp::Equal },
            { TokenKind::NotEqual, BinaryOp::NotEqual } });
    }

    ExprPtr ParseRelational()
    {
        return ParseBinaryLevel(&ParserState::ParseAdditive, {
            { TokenKind::Less, BinaryOp::Less },
            { TokenKind::Greater, BinaryOp::Greater },
            { TokenKind::LessEqual, BinaryOp::LessEqual },
            { TokenKind::GreaterEqual, BinaryOp::GreaterEqual } });
    }

    ExprPtr ParseAdditive()
    {
        return ParseBinaryLevel(&ParserState::ParseMultiplicative, {
            { TokenKind::Plus, BinaryOp::Add },
            { TokenKind::Minus, BinaryOp::Sub } });
    }

    ExprPtr ParseMultiplicative()
    {
        return ParseBinaryLevel(&ParserState::ParseUnary, {
            { TokenKind::Star, BinaryOp::Mul },
            { TokenKind::Slash, BinaryOp::Div },
            { TokenKind::Percent, BinaryOp::Mod } });
    }

    ExprPtr ParseUnary()
    {
        // `(int)` and `(float)` name a conversion; anything else after an
        // opening bracket is an expression to be grouped. Both spellings
        // are keywords, so the two can never be confused -- which is the
        // reason only these two types may be named here.
        if (Check(TokenKind::LParen) && PeekKind(2) == TokenKind::RParen &&
            (PeekKind(1) == TokenKind::KwInt || PeekKind(1) == TokenKind::KwFloat))
        {
            SourceLocation loc = Current().location;
            Advance();

            SourceLocation targetLoc = Current().location;
            const ValueType target = Check(TokenKind::KwInt) ? ValueType::Int : ValueType::Float;
            Advance();
            Advance();

            // The operand is whatever binds tightest after it, so the
            // conversion takes the value next to it and not the
            // arithmetic around it.
            ExprPtr operand = ParseUnary();
            if (!operand) return nullptr;

            auto expr = std::make_unique<ConvertExpr>();
            expr->location = loc;
            expr->target = target;
            expr->targetLocation = targetLoc;
            expr->operand = std::move(operand);
            return expr;
        }

        if (Check(TokenKind::Minus) || Check(TokenKind::Not))
        {
            SourceLocation loc = Current().location;
            UnaryOp op = Check(TokenKind::Minus) ? UnaryOp::Negate : UnaryOp::Not;
            Advance();

            ExprPtr operand = ParseUnary();
            if (!operand) return nullptr;

            auto expr = std::make_unique<UnaryExpr>();
            expr->location = loc;
            expr->op = op;
            expr->operand = std::move(operand);
            return expr;
        }
        return ParsePostfix();
    }

    ExprPtr ParsePostfix()
    {
        ExprPtr expr = ParsePrimary();
        if (!expr) return nullptr;

        for (;;)
        {
            if (Match(TokenKind::Dot))
            {
                const Token* member = Expect(TokenKind::Identifier, "a member name");
                if (!member) return nullptr;

                auto memberExpr = std::make_unique<MemberExpr>();
                // Anchored at the start of the whole reference rather
                // than at the dot, which is what a reader looks for.
                memberExpr->location = expr->location;
                memberExpr->base = std::move(expr);
                memberExpr->member = member->text;

                // `a.B<C>(...)` names a type at a call site; `a.B < c`
                // compares. Nothing short of reading the whole bracketed
                // list and seeing an argument list behind it tells the
                // two apart, so that is what is done, and the attempt is
                // rolled back whole when it does not fit.
                if (Check(TokenKind::Less))
                {
                    const size_t start = m_position;
                    const SourceLocation argsLocation = Current().location;

                    std::vector<TypeRef> typeArgs;
                    if (ParseTypeArguments(typeArgs) && Check(TokenKind::LParen))
                    {
                        memberExpr->typeArgs = std::move(typeArgs);
                        memberExpr->typeArgLocation = argsLocation;
                    }
                    else
                    {
                        m_position = start;
                    }
                }

                expr = std::move(memberExpr);
                continue;
            }
            if (Match(TokenKind::LBracket))
            {
                auto indexExpr = std::make_unique<IndexExpr>();
                indexExpr->location = expr->location;
                indexExpr->base = std::move(expr);
                indexExpr->index = ParseExpression();
                if (!indexExpr->index) return nullptr;
                if (!Expect(TokenKind::RBracket, "']'")) return nullptr;
                expr = std::move(indexExpr);
                continue;
            }
            if (Match(TokenKind::LParen))
            {
                auto call = std::make_unique<CallExpr>();
                call->location = expr->location;
                call->callee = std::move(expr);

                if (!Check(TokenKind::RParen))
                {
                    do
                    {
                        ExprPtr arg = ParseExpression();
                        if (!arg) return nullptr;
                        call->args.push_back(std::move(arg));
                    } while (Match(TokenKind::Comma));
                }
                if (!Expect(TokenKind::RParen, "')'")) return nullptr;
                expr = std::move(call);
                continue;
            }
            break;
        }
        return expr;
    }

    ExprPtr ParsePrimary()
    {
        SourceLocation loc = Current().location;

        if (Match(TokenKind::IntLiteral))
        {
            auto expr = std::make_unique<IntLiteralExpr>();
            expr->location = loc;
            expr->value = Previous().intValue;
            return expr;
        }
        if (Match(TokenKind::FloatLiteral))
        {
            auto expr = std::make_unique<FloatLiteralExpr>();
            expr->location = loc;
            expr->value = Previous().floatValue;
            return expr;
        }
        if (Match(TokenKind::StringLiteral))
        {
            auto expr = std::make_unique<StringLiteralExpr>();
            expr->location = loc;
            expr->value = Previous().text;
            return expr;
        }
        if (Match(TokenKind::KwTrue) || Match(TokenKind::KwFalse))
        {
            auto expr = std::make_unique<BoolLiteralExpr>();
            expr->location = loc;
            expr->value = Previous().kind == TokenKind::KwTrue;
            return expr;
        }
        if (Match(TokenKind::KwNull))
        {
            auto expr = std::make_unique<NullLiteralExpr>();
            expr->location = loc;
            return expr;
        }
        if (Match(TokenKind::KwThis))
        {
            auto expr = std::make_unique<ThisExpr>();
            expr->location = loc;
            return expr;
        }
        if (Match(TokenKind::KwNew)) return ParseNew(loc);
        if (Match(TokenKind::Identifier))
        {
            auto expr = std::make_unique<IdentifierExpr>();
            expr->location = loc;
            expr->name = Previous().text;
            return expr;
        }
        if (Match(TokenKind::LParen))
        {
            ExprPtr inner = ParseExpression();
            if (!inner) return nullptr;
            if (!Expect(TokenKind::RParen, "')'")) return nullptr;
            return inner;
        }

        Error(loc, "expected an expression, found '" + Describe(Current()) + "'");
        return nullptr;
    }

    ExprPtr ParseNew(SourceLocation loc)
    {
        // The type name is read without its `[]` suffixes, because the
        // first bracket pair after `new` is where the element count goes
        // and only the pairs after that belong to the element type.
        TypeRef type;
        if (IsBuiltInTypeToken(Current().kind))
        {
            type.type = TypeFromToken(Advance().kind);
        }
        else if (Check(TokenKind::Identifier))
        {
            type.type = ValueType::Object;
            type.name = Advance().text;
            if (Check(TokenKind::Less) && !ParseTypeArguments(type.typeArgs)) return nullptr;
        }
        else
        {
            Error(Current().location, "expected a type name after 'new', found '" + Describe(Current()) + "'");
            return nullptr;
        }

        if (Match(TokenKind::LBracket))
        {
            auto expr = std::make_unique<NewArrayExpr>();
            expr->location = loc;

            expr->length = ParseExpression();
            if (!expr->length) return nullptr;
            if (!Expect(TokenKind::RBracket, "']'")) return nullptr;

            // `new int[4][]` makes four empty `int[]`, so every pair after
            // the count deepens the element type rather than the result.
            type.arrayDepth = ParseArraySuffix();
            expr->elementType = std::move(type);
            return expr;
        }

        auto expr = std::make_unique<NewExpr>();
        expr->location = loc;
        expr->type = std::move(type);

        if (!Expect(TokenKind::LParen, "'('")) return nullptr;
        if (!Check(TokenKind::RParen))
        {
            do
            {
                ExprPtr arg = ParseExpression();
                if (!arg) return nullptr;
                expr->args.push_back(std::move(arg));
            } while (Match(TokenKind::Comma));
        }
        if (!Expect(TokenKind::RParen, "')'")) return nullptr;
        return expr;
    }
};

} // namespace

Program Parse(const std::vector<Token>& tokens, DiagnosticList& diagnostics)
{
    ParserState parser(tokens, diagnostics);
    return parser.Run();
}

} // namespace Fluxion::Script
