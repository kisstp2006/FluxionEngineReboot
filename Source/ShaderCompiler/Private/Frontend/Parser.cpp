#include <Fluxion/ShaderCompiler/Frontend/Parser.hpp>

#include <unordered_set>

namespace Fluxion::ShaderCompiler
{

namespace
{

bool IsTypeToken(TokenKind kind)
{
    switch (kind)
    {
        case TokenKind::KwVoid: case TokenKind::KwBool: case TokenKind::KwInt: case TokenKind::KwUint:
        case TokenKind::KwFloat: case TokenKind::KwVector2: case TokenKind::KwVector3: case TokenKind::KwVector4:
        case TokenKind::KwMatrix3x3: case TokenKind::KwMatrix4x4: case TokenKind::KwTexture2D: case TokenKind::KwTextureCube:
            return true;
        default:
            return false;
    }
}

bool BindingGroupFromName(const std::string& name, BindingGroup* outGroup)
{
    if (name == "Global") { *outGroup = BindingGroup::Global; return true; }
    if (name == "Frame") { *outGroup = BindingGroup::Frame; return true; }
    if (name == "Material") { *outGroup = BindingGroup::Material; return true; }
    if (name == "Object") { *outGroup = BindingGroup::Object; return true; }
    return false;
}

ShaderType TypeFromToken(TokenKind kind)
{
    switch (kind)
    {
        case TokenKind::KwVoid: return { TypeKind::Void };
        case TokenKind::KwBool: return { TypeKind::Bool };
        case TokenKind::KwInt: return { TypeKind::Int };
        case TokenKind::KwUint: return { TypeKind::Uint };
        case TokenKind::KwFloat: return { TypeKind::Float };
        case TokenKind::KwVector2: return { TypeKind::Vec2 };
        case TokenKind::KwVector3: return { TypeKind::Vec3 };
        case TokenKind::KwVector4: return { TypeKind::Vec4 };
        case TokenKind::KwMatrix3x3: return { TypeKind::Mat3 };
        case TokenKind::KwMatrix4x4: return { TypeKind::Mat4 };
        case TokenKind::KwTexture2D: return { TypeKind::Sampler2D };
        case TokenKind::KwTextureCube: return { TypeKind::SamplerCube };
        default: return { TypeKind::Void };
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
        while (!Check(TokenKind::EndOfFile))
        {
            DeclPtr decl = ParseTopLevelDecl();
            if (decl) program.declarations.push_back(std::move(decl));
            else SynchronizeToNextDecl();
        }
        return program;
    }

private:
    const std::vector<Token>& m_tokens;
    DiagnosticList& m_diagnostics;
    size_t m_pos = 0;

    // Which names are types because a `struct` said so.
    //
    // A built-in type is a keyword and the lexer settles it; a declared
    // one arrives as an ordinary identifier, and only this tells the two
    // apart. Filled as each struct is parsed, which is what makes a type
    // usable after its declaration and not before -- read in order, the
    // same way the file is.
    std::unordered_set<std::string> m_structNames;

    // Whether what comes next begins a type: a built-in keyword, or the
    // name of a struct already declared.
    bool IsTypeStart() const
    {
        if (IsTypeToken(Current().kind)) return true;
        return Check(TokenKind::Identifier) && m_structNames.count(Current().text) != 0;
    }

    // Consumes one type and returns it. Only call where IsTypeStart() has
    // already said yes.
    ShaderType ParseTypeName()
    {
        if (Check(TokenKind::Identifier)) return ShaderType{ TypeKind::Unresolved, Advance().text };
        return TypeFromToken(Advance().kind);
    }

    const Token& Current() const { return m_tokens[m_pos]; }
    const Token& Previous() const { return m_tokens[m_pos - 1]; }
    bool Check(TokenKind kind) const { return Current().kind == kind; }
    bool AtEnd() const { return Check(TokenKind::EndOfFile); }

    const Token& Advance()
    {
        if (!AtEnd()) ++m_pos;
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
        Error(Current().location, std::string("expected ") + what + ", got '" + Current().text + "'");
        return nullptr;
    }

    void Error(SourceLocation location, std::string message)
    {
        m_diagnostics.AddError(std::move(location), std::move(message));
    }

    void SynchronizeToNextDecl()
    {
        while (!AtEnd())
        {
            if (IsTypeStart() || Check(TokenKind::KwConst) ||
                Check(TokenKind::KwStruct) || Check(TokenKind::LBracket))
                return;
            Advance();
        }
    }

    // --- Top-level declarations -----------------------------------------

    DeclPtr ParseTopLevelDecl()
    {
        SourceLocation loc = Current().location;

        if (Match(TokenKind::LBracket))
        {
            return ParseAttributeDecl(loc);
        }
        if (Match(TokenKind::KwStruct))
        {
            return ParseStructDecl(loc);
        }
        if (Match(TokenKind::KwConst))
        {
            return ParseGlobalConstDecl(loc);
        }
        if (IsTypeStart())
        {
            return ParseFunctionDecl(loc);
        }

        Error(loc, "expected a declaration (an attribute like '[Input]'/'[Uniform]'/'[Target(N)]', 'struct', 'const', or a function)");
        Advance();
        return nullptr;
    }

    // Bracketed declaration attributes: `[Input] T name;`,
    // `[Output] T name;`, `[Target(N)] T name;`,
    // `[Uniform] T name;`/`[Uniform(Group)] T name;`,
    // `[Texture] T name;`/`[Texture(Group)] T name;`,
    // `[Buffer] T name;`/`[Buffer(Group)] T name;` -- the only way to
    // declare stage IO, render targets, uniforms, and storage buffers in
    // this language. The parenthesized argument means different things by
    // attribute: an integer slot for Target, an optional BindingGroup name
    // (Global/Frame/Material/Object, default Material) for
    // Uniform/Texture/Buffer.
    DeclPtr ParseAttributeDecl(SourceLocation loc)
    {
        const Token* attrName = Expect(TokenKind::Identifier, "an attribute name (Input/Output/Target/Uniform/Texture/Buffer)");
        if (!attrName) return nullptr;
        std::string attribute = attrName->text;

        int explicitSlot = -1;
        BindingGroup group = BindingGroup::Material;
        if (Match(TokenKind::LParen))
        {
            if (attribute == "Uniform" || attribute == "Texture" || attribute == "Buffer")
            {
                const Token* groupToken = Expect(TokenKind::Identifier, "a binding group name (Global/Frame/Material/Object)");
                if (groupToken && !BindingGroupFromName(groupToken->text, &group))
                    Error(groupToken->location, "unknown binding group '" + groupToken->text + "' (expected Global/Frame/Material/Object)");
            }
            else
            {
                const Token* slotToken = Expect(TokenKind::IntLiteral, "a slot index");
                if (slotToken) explicitSlot = (int)slotToken->numberValue;
            }
            Expect(TokenKind::RParen, "')'");
        }
        Expect(TokenKind::RBracket, "']'");

        // A struct this shader declared is a type here too, not only a
        // builtin one. A buffer of light descriptions has no other way to
        // say what it holds -- the alternative is several parallel
        // buffers of vectors, which is the same data with the grouping
        // thrown away.
        if (!IsTypeStart()) { Error(Current().location, "expected a type after attribute"); return nullptr; }
        ShaderType type = ParseTypeName();
        const Token* nameToken = Expect(TokenKind::Identifier, "a declaration name");
        Expect(TokenKind::Semicolon, "';'");
        if (!nameToken) return nullptr;

        if (attribute == "Target")
        {
            if (explicitSlot < 0) { Error(loc, "[Target] requires a slot index, e.g. [Target(0)]"); explicitSlot = 0; }
            auto decl = std::make_unique<OutputSlotDecl>();
            decl->location = loc;
            decl->slot = explicitSlot;
            decl->type = type;
            decl->name = nameToken->text;
            return decl;
        }
        if (attribute == "Uniform" || attribute == "Texture" || attribute == "Buffer")
        {
            auto decl = std::make_unique<UniformDecl>();
            decl->location = loc;
            decl->type = type;
            decl->name = nameToken->text;
            decl->group = group;
            decl->isStorageBuffer = (attribute == "Buffer");
            return decl;
        }
        if (attribute == "Input" || attribute == "Output")
        {
            auto decl = std::make_unique<StageIODecl>();
            decl->location = loc;
            decl->direction = (attribute == "Input") ? StageIODirection::In : StageIODirection::Out;
            decl->type = type;
            decl->name = nameToken->text;
            return decl;
        }

        Error(loc, "unknown attribute '" + attribute + "' (expected Input/Output/Target/Uniform/Texture/Buffer)");
        return nullptr;
    }

    DeclPtr ParseGlobalConstDecl(SourceLocation loc)
    {
        if (!IsTypeToken(Current().kind)) { Error(Current().location, "expected a type after 'const'"); return nullptr; }
        ShaderType type = TypeFromToken(Advance().kind);
        const Token* nameToken = Expect(TokenKind::Identifier, "a constant name");
        Expect(TokenKind::Assign, "'='");
        ExprPtr init = ParseExpression();
        Expect(TokenKind::Semicolon, "';'");
        if (!nameToken) return nullptr;

        auto decl = std::make_unique<GlobalConstDecl>();
        decl->location = loc;
        decl->type = type;
        decl->name = nameToken->text;
        decl->initializer = std::move(init);
        return decl;
    }

    DeclPtr ParseStructDecl(SourceLocation loc)
    {
        const Token* nameToken = Expect(TokenKind::Identifier, "a struct name");
        Expect(TokenKind::LBrace, "'{'");
        auto decl = std::make_unique<StructDecl>();
        decl->location = loc;
        if (nameToken)
        {
            decl->name = nameToken->text;

            // Registered before the fields are read, so a struct may name
            // itself in its own body. Nothing useful can be built that
            // way yet, but the alternative is an error message about an
            // unknown type that names the type being declared.
            m_structNames.insert(decl->name);
        }

        while (!Check(TokenKind::RBrace) && !AtEnd())
        {
            if (!IsTypeStart()) { Error(Current().location, "expected a field type"); break; }
            ShaderType fieldType = ParseTypeName();
            const Token* fieldName = Expect(TokenKind::Identifier, "a field name");
            Expect(TokenKind::Semicolon, "';'");
            if (fieldName) decl->fields.push_back(StructField{ fieldType, fieldName->text });
        }
        Expect(TokenKind::RBrace, "'}'");

        // The semicolon after the closing brace is optional, and accepted
        // either way. Every language this one resembles requires it, so
        // an author writes it without thinking -- and without this it
        // would be left over as a stray token, reported as a broken
        // declaration on the line AFTER the struct, which names the wrong
        // thing entirely.
        Match(TokenKind::Semicolon);

        return decl;
    }

    DeclPtr ParseFunctionDecl(SourceLocation loc)
    {
        ShaderType returnType = ParseTypeName();
        const Token* nameToken = Expect(TokenKind::Identifier, "a function name");
        Expect(TokenKind::LParen, "'('");

        auto decl = std::make_unique<FunctionDecl>();
        decl->location = loc;
        decl->returnType = returnType;
        if (nameToken) decl->name = nameToken->text;

        if (!Check(TokenKind::RParen))
        {
            do
            {
                if (!IsTypeStart()) { Error(Current().location, "expected a parameter type"); break; }
                ShaderType paramType = ParseTypeName();
                const Token* paramName = Expect(TokenKind::Identifier, "a parameter name");
                decl->params.push_back(Param{ paramType, paramName ? paramName->text : "" });
            } while (Match(TokenKind::Comma));
        }
        Expect(TokenKind::RParen, "')'");
        decl->body = ParseBlock();
        return decl;
    }

    // --- Statements --------------------------------------------------------

    StmtPtr ParseStatement()
    {
        SourceLocation loc = Current().location;

        if (Check(TokenKind::LBrace)) return ParseBlock();
        if (Match(TokenKind::KwIf)) return ParseIf(loc);
        if (Match(TokenKind::KwFor)) return ParseFor(loc);
        if (Match(TokenKind::KwWhile)) return ParseWhile(loc);
        if (Match(TokenKind::KwReturn)) return ParseReturn(loc);
        if (Match(TokenKind::KwDiscard)) return ParseDiscard(loc);
        if (IsTypeStart()) return ParseVarDeclStatement(loc);

        auto stmt = std::make_unique<ExprStmt>();
        stmt->location = loc;
        stmt->expr = ParseExpression();
        Expect(TokenKind::Semicolon, "';'");
        return stmt;
    }

    StmtPtr ParseDiscard(SourceLocation loc)
    {
        auto stmt = std::make_unique<DiscardStmt>();
        stmt->location = loc;
        Expect(TokenKind::Semicolon, "';'");
        return stmt;
    }

    StmtPtr ParseBlock()
    {
        SourceLocation loc = Current().location;
        Expect(TokenKind::LBrace, "'{'");
        auto block = std::make_unique<BlockStmt>();
        block->location = loc;
        while (!Check(TokenKind::RBrace) && !AtEnd())
            block->statements.push_back(ParseStatement());
        Expect(TokenKind::RBrace, "'}'");
        return block;
    }

    StmtPtr ParseVarDeclStatement(SourceLocation loc)
    {
        ShaderType type = ParseTypeName();
        const Token* nameToken = Expect(TokenKind::Identifier, "a variable name");
        auto stmt = std::make_unique<VarDeclStmt>();
        stmt->location = loc;
        stmt->type = type;
        if (nameToken) stmt->name = nameToken->text;
        if (Match(TokenKind::Assign))
            stmt->initializer = ParseExpression();
        Expect(TokenKind::Semicolon, "';'");
        return stmt;
    }

    StmtPtr ParseIf(SourceLocation loc)
    {
        Expect(TokenKind::LParen, "'('");
        auto stmt = std::make_unique<IfStmt>();
        stmt->location = loc;
        stmt->condition = ParseExpression();
        Expect(TokenKind::RParen, "')'");
        stmt->thenBranch = ParseStatement();
        if (Match(TokenKind::KwElse))
            stmt->elseBranch = ParseStatement();
        return stmt;
    }

    StmtPtr ParseFor(SourceLocation loc)
    {
        Expect(TokenKind::LParen, "'('");
        auto stmt = std::make_unique<ForStmt>();
        stmt->location = loc;

        if (!Check(TokenKind::Semicolon))
        {
            SourceLocation initLoc = Current().location;
            if (IsTypeStart())
            {
                stmt->init = ParseVarDeclStatement(initLoc);
            }
            else
            {
                auto initStmt = std::make_unique<ExprStmt>();
                initStmt->location = initLoc;
                initStmt->expr = ParseExpression();
                Expect(TokenKind::Semicolon, "';'");
                stmt->init = std::move(initStmt);
            }
        }
        else
        {
            Advance();
        }

        if (!Check(TokenKind::Semicolon)) stmt->condition = ParseExpression();
        Expect(TokenKind::Semicolon, "';'");
        if (!Check(TokenKind::RParen)) stmt->iteration = ParseExpression();
        Expect(TokenKind::RParen, "')'");
        stmt->body = ParseStatement();
        return stmt;
    }

    StmtPtr ParseWhile(SourceLocation loc)
    {
        Expect(TokenKind::LParen, "'('");
        auto stmt = std::make_unique<WhileStmt>();
        stmt->location = loc;
        stmt->condition = ParseExpression();
        Expect(TokenKind::RParen, "')'");
        stmt->body = ParseStatement();
        return stmt;
    }

    StmtPtr ParseReturn(SourceLocation loc)
    {
        auto stmt = std::make_unique<ReturnStmt>();
        stmt->location = loc;
        if (!Check(TokenKind::Semicolon))
            stmt->value = ParseExpression();
        Expect(TokenKind::Semicolon, "';'");
        return stmt;
    }

    // --- Expressions (standard C-family precedence, loosest to tightest) --

    ExprPtr ParseExpression() { return ParseAssignment(); }

    ExprPtr ParseAssignment()
    {
        ExprPtr lhs = ParseTernary();

        AssignOp op;
        if (Check(TokenKind::Assign)) op = AssignOp::Assign;
        else if (Check(TokenKind::PlusAssign)) op = AssignOp::AddAssign;
        else if (Check(TokenKind::MinusAssign)) op = AssignOp::SubAssign;
        else if (Check(TokenKind::StarAssign)) op = AssignOp::MulAssign;
        else if (Check(TokenKind::SlashAssign)) op = AssignOp::DivAssign;
        else return lhs;

        SourceLocation loc = Current().location;
        Advance();
        ExprPtr rhs = ParseAssignment();

        auto expr = std::make_unique<AssignExpr>();
        expr->location = loc;
        expr->op = op;
        expr->target = std::move(lhs);
        expr->value = std::move(rhs);
        return expr;
    }

    ExprPtr ParseTernary()
    {
        ExprPtr condition = ParseLogicalOr();
        if (!Match(TokenKind::Question)) return condition;

        SourceLocation loc = Previous().location;
        auto expr = std::make_unique<TernaryExpr>();
        expr->location = loc;
        expr->condition = std::move(condition);
        expr->thenExpr = ParseExpression();
        Expect(TokenKind::Colon, "':'");
        expr->elseExpr = ParseAssignment();
        return expr;
    }

    ExprPtr ParseBinaryLevel(ExprPtr (ParserState::*next)(), std::initializer_list<std::pair<TokenKind, BinaryOp>> ops)
    {
        ExprPtr lhs = (this->*next)();
        for (;;)
        {
            bool matched = false;
            for (auto& [tokenKind, binOp] : ops)
            {
                if (Check(tokenKind))
                {
                    SourceLocation loc = Current().location;
                    Advance();
                    ExprPtr rhs = (this->*next)();
                    auto expr = std::make_unique<BinaryExpr>();
                    expr->location = loc;
                    expr->op = binOp;
                    expr->lhs = std::move(lhs);
                    expr->rhs = std::move(rhs);
                    lhs = std::move(expr);
                    matched = true;
                    break;
                }
            }
            if (!matched) return lhs;
        }
    }

    ExprPtr ParseLogicalOr() { return ParseBinaryLevel(&ParserState::ParseLogicalAnd, { { TokenKind::OrOr, BinaryOp::Or } }); }
    ExprPtr ParseLogicalAnd() { return ParseBinaryLevel(&ParserState::ParseEquality, { { TokenKind::AndAnd, BinaryOp::And } }); }
    ExprPtr ParseEquality() { return ParseBinaryLevel(&ParserState::ParseRelational, { { TokenKind::Equal, BinaryOp::Eq }, { TokenKind::NotEqual, BinaryOp::NotEq } }); }
    ExprPtr ParseRelational()
    {
        return ParseBinaryLevel(&ParserState::ParseAdditive, {
            { TokenKind::Less, BinaryOp::Less }, { TokenKind::Greater, BinaryOp::Greater },
            { TokenKind::LessEqual, BinaryOp::LessEq }, { TokenKind::GreaterEqual, BinaryOp::GreaterEq } });
    }
    ExprPtr ParseAdditive() { return ParseBinaryLevel(&ParserState::ParseMultiplicative, { { TokenKind::Plus, BinaryOp::Add }, { TokenKind::Minus, BinaryOp::Sub } }); }
    ExprPtr ParseMultiplicative()
    {
        return ParseBinaryLevel(&ParserState::ParseUnary, {
            { TokenKind::Star, BinaryOp::Mul }, { TokenKind::Slash, BinaryOp::Div }, { TokenKind::Percent, BinaryOp::Mod } });
    }

    ExprPtr ParseUnary()
    {
        if (Check(TokenKind::Minus) || Check(TokenKind::Not))
        {
            SourceLocation loc = Current().location;
            UnaryOp op = Check(TokenKind::Minus) ? UnaryOp::Negate : UnaryOp::Not;
            Advance();
            auto expr = std::make_unique<UnaryExpr>();
            expr->location = loc;
            expr->op = op;
            expr->operand = ParseUnary();
            return expr;
        }
        return ParsePostfix();
    }

    ExprPtr ParsePostfix()
    {
        ExprPtr expr = ParsePrimary();
        for (;;)
        {
            if (Match(TokenKind::Dot))
            {
                SourceLocation loc = Previous().location;
                const Token* member = Expect(TokenKind::Identifier, "a member/swizzle name");
                auto memberExpr = std::make_unique<MemberExpr>();
                memberExpr->location = loc;
                memberExpr->base = std::move(expr);
                if (member) memberExpr->member = member->text;
                expr = std::move(memberExpr);
                continue;
            }
            if (Match(TokenKind::LBracket))
            {
                SourceLocation loc = Previous().location;
                auto indexExpr = std::make_unique<IndexExpr>();
                indexExpr->location = loc;
                indexExpr->base = std::move(expr);
                indexExpr->index = ParseExpression();
                Expect(TokenKind::RBracket, "']'");
                expr = std::move(indexExpr);
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
            expr->value = (long long)Previous().numberValue;
            return expr;
        }
        if (Match(TokenKind::FloatLiteral))
        {
            auto expr = std::make_unique<FloatLiteralExpr>();
            expr->location = loc;
            expr->value = Previous().numberValue;
            return expr;
        }
        if (Match(TokenKind::KwTrue) || Match(TokenKind::KwFalse))
        {
            auto expr = std::make_unique<BoolLiteralExpr>();
            expr->location = loc;
            expr->value = Previous().kind == TokenKind::KwTrue;
            return expr;
        }
        if (IsTypeToken(Current().kind))
        {
            ShaderType type = TypeFromToken(Advance().kind);
            Expect(TokenKind::LParen, "'(' after type (constructor call)");
            auto expr = std::make_unique<ConstructorExpr>();
            expr->location = loc;
            expr->targetType = type;
            if (!Check(TokenKind::RParen))
            {
                do { expr->args.push_back(ParseExpression()); } while (Match(TokenKind::Comma));
            }
            Expect(TokenKind::RParen, "')'");
            return expr;
        }
        if (Match(TokenKind::Identifier))
        {
            std::string name = Previous().text;
            if (Match(TokenKind::LParen))
            {
                auto expr = std::make_unique<CallExpr>();
                expr->location = loc;
                expr->callee = name;
                if (!Check(TokenKind::RParen))
                {
                    do { expr->args.push_back(ParseExpression()); } while (Match(TokenKind::Comma));
                }
                Expect(TokenKind::RParen, "')'");
                return expr;
            }
            auto expr = std::make_unique<VarRefExpr>();
            expr->location = loc;
            expr->name = name;
            return expr;
        }
        if (Match(TokenKind::LParen))
        {
            ExprPtr inner = ParseExpression();
            Expect(TokenKind::RParen, "')'");
            return inner;
        }

        Error(loc, "expected an expression, got '" + Current().text + "'");
        Advance();
        auto errExpr = std::make_unique<IntLiteralExpr>();
        errExpr->location = loc;
        return errExpr;
    }
};

} // namespace

Program Parse(const std::vector<Token>& tokens, DiagnosticList& diagnostics)
{
    ParserState parser(tokens, diagnostics);
    return parser.Run();
}

} // namespace Fluxion::ShaderCompiler
