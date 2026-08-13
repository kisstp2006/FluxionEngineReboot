#include <Fluxion/Script/Compiler/Parser.hpp>

#include <utility>

namespace Fluxion::Script
{

namespace
{

bool IsTypeToken(TokenKind kind)
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
            DeclPtr decl = ParseClassDecl();
            if (decl) program.declarations.push_back(std::move(decl));
            else SynchronizeToNextDecl();
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

    // A class always begins with the same two tokens, so that pair is the
    // only anchor worth trusting after an error.
    void SynchronizeToNextDecl()
    {
        while (!AtEnd())
        {
            if (Check(TokenKind::KwStatic) && PeekKind(1) == TokenKind::KwClass) return;
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
            if (depth == 0 && (kind == TokenKind::RBrace || kind == TokenKind::KwStatic)) return;
            if (kind == TokenKind::LBrace) ++depth;
            else if (kind == TokenKind::RBrace) --depth;
            Advance();
        }
    }

    // --- Declarations ---------------------------------------------------

    DeclPtr ParseClassDecl()
    {
        SourceLocation loc = Current().location;

        if (!Check(TokenKind::KwStatic))
        {
            Error(loc, "expected a class declaration, found '" + Describe(Current()) + "'");
            Advance();
            return nullptr;
        }
        Advance();

        if (!Expect(TokenKind::KwClass, "'class'")) return nullptr;

        const Token* nameToken = Expect(TokenKind::Identifier, "a class name");
        if (!nameToken) return nullptr;

        auto decl = std::make_unique<ClassDecl>();
        decl->location = loc;
        decl->name = nameToken->text;

        if (!Expect(TokenKind::LBrace, "'{'")) return nullptr;

        while (!Check(TokenKind::RBrace) && !AtEnd())
        {
            DeclPtr method = ParseMethodDecl();
            if (method) decl->methods.push_back(std::move(method));
            else SynchronizeToNextMember();
        }

        if (!Expect(TokenKind::RBrace, "'}'")) return nullptr;
        return decl;
    }

    DeclPtr ParseMethodDecl()
    {
        SourceLocation loc = Current().location;

        if (!Expect(TokenKind::KwStatic, "'static'")) return nullptr;

        if (!IsTypeToken(Current().kind))
        {
            Error(Current().location, "expected a return type, found '" + Describe(Current()) + "'");
            return nullptr;
        }
        ValueType returnType = TypeFromToken(Advance().kind);

        const Token* nameToken = Expect(TokenKind::Identifier, "a method name");
        if (!nameToken) return nullptr;

        auto decl = std::make_unique<MethodDecl>();
        decl->location = loc;
        decl->returnType = returnType;
        decl->name = nameToken->text;

        if (!Expect(TokenKind::LParen, "'('")) return nullptr;
        if (!Check(TokenKind::RParen))
        {
            do
            {
                SourceLocation paramLoc = Current().location;
                if (!IsTypeToken(Current().kind))
                {
                    Error(paramLoc, "expected a parameter type, found '" + Describe(Current()) + "'");
                    return nullptr;
                }
                ValueType paramType = TypeFromToken(Advance().kind);
                const Token* paramName = Expect(TokenKind::Identifier, "a parameter name");
                if (!paramName) return nullptr;
                decl->params.push_back(ParamDecl{ paramType, paramName->text, paramLoc });
            } while (Match(TokenKind::Comma));
        }
        if (!Expect(TokenKind::RParen, "')'")) return nullptr;

        StmtPtr body = ParseBlock();
        if (!body) return nullptr;
        decl->body = std::move(body);
        return decl;
    }

    // --- Statements -------------------------------------------------------

    bool IsLocalDeclStart() const
    {
        return IsTypeToken(Current().kind) || Check(TokenKind::KwVar);
    }

    StmtPtr ParseStatement()
    {
        SourceLocation loc = Current().location;

        if (Check(TokenKind::LBrace)) return ParseBlock();
        if (Match(TokenKind::KwIf)) return ParseIf(loc);
        if (Match(TokenKind::KwWhile)) return ParseWhile(loc);
        if (Match(TokenKind::KwFor)) return ParseFor(loc);
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
        else
        {
            stmt->declaredType = TypeFromToken(Advance().kind);
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
                expr = std::move(memberExpr);
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
};

} // namespace

Program Parse(const std::vector<Token>& tokens, DiagnosticList& diagnostics)
{
    ParserState parser(tokens, diagnostics);
    return parser.Run();
}

} // namespace Fluxion::Script
