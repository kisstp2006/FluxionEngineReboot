#include <Fluxion/Script/Compiler/Semantic.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Fluxion::Script
{

namespace
{

bool IsNumeric(ValueType type) { return type == ValueType::Int || type == ValueType::Float; }

// True once an error has already been reported for the expression that
// produced this type, so further complaints about it would only be noise.
bool IsPoisoned(ValueType type) { return type == ValueType::Unknown; }

std::string Quoted(ValueType type) { return std::string("'") + ValueTypeName(type) + "'"; }

class Analyzer
{
public:
    Analyzer(Program& program, DiagnosticList& diagnostics)
        : m_program(program), m_diagnostics(diagnostics)
    {
    }

    bool Run()
    {
        const NativeFunctionSignature* natives = NativeFunctionTable();
        for (u32 i = 0; i < kNativeFunctionCount; ++i)
            m_natives[natives[i].qualifiedName].push_back((NativeFunctionId)i);

        CollectDeclarations();

        for (DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Class) continue;
            auto* classDecl = static_cast<ClassDecl*>(decl.get());
            m_currentClass = classDecl;
            for (DeclPtr& method : classDecl->methods)
                AnalyzeMethod(*static_cast<MethodDecl*>(method.get()));
            m_currentClass = nullptr;
        }

        return !m_diagnostics.HasErrors();
    }

private:
    struct LocalSymbol
    {
        ValueType type = ValueType::Unknown;
        int slot = -1;
    };

    Program& m_program;
    DiagnosticList& m_diagnostics;

    std::unordered_map<std::string, ClassDecl*> m_classes;
    std::unordered_map<std::string, MethodDecl*> m_methods; // keyed "Class.Method"
    std::unordered_map<std::string, std::vector<NativeFunctionId>> m_natives;

    ClassDecl* m_currentClass = nullptr;
    MethodDecl* m_currentMethod = nullptr;
    std::vector<std::unordered_map<std::string, LocalSymbol>> m_scopes;
    std::vector<int> m_scopeSlotMarks;
    int m_nextSlot = 0;
    int m_highWaterSlot = 0;
    int m_loopDepth = 0;

    void Error(const SourceLocation& location, std::string message)
    {
        m_diagnostics.AddError(location, std::move(message));
    }

    // Every method is numbered here, before any body is looked at, so a
    // call can be resolved against a method declared later in the file.
    void CollectDeclarations()
    {
        u32 nextFunctionIndex = 0;
        for (DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Class) continue;
            auto* classDecl = static_cast<ClassDecl*>(decl.get());

            if (!m_classes.emplace(classDecl->name, classDecl).second)
                Error(classDecl->location, "class '" + classDecl->name + "' is declared more than once");

            for (DeclPtr& methodDecl : classDecl->methods)
            {
                auto* method = static_cast<MethodDecl*>(methodDecl.get());
                method->qualifiedName = classDecl->name + "." + method->name;
                method->functionIndex = nextFunctionIndex++;

                if (!m_methods.emplace(method->qualifiedName, method).second)
                    Error(method->location, "method '" + method->qualifiedName + "' is declared more than once");
            }
        }
    }

    void AnalyzeMethod(MethodDecl& method)
    {
        m_currentMethod = &method;
        m_scopes.clear();
        m_scopeSlotMarks.clear();
        m_nextSlot = 0;
        m_highWaterSlot = 0;
        m_loopDepth = 0;

        PushScope();
        for (ParamDecl& param : method.params)
        {
            if (param.type == ValueType::Void)
                Error(param.location, "parameter '" + param.name + "' cannot have type 'void'");
            DeclareLocal(param.name, param.type, param.location);
        }

        if (method.body) AnalyzeStmt(*method.body);
        PopScope();

        method.localSlotCount = (u32)m_highWaterSlot;
        m_currentMethod = nullptr;
    }

    // --- Scopes ------------------------------------------------------------

    void PushScope()
    {
        m_scopes.emplace_back();
        m_scopeSlotMarks.push_back(m_nextSlot);
    }

    void PopScope()
    {
        m_scopes.pop_back();
        // Slots freed by leaving a scope are handed back to the next
        // sibling scope; the high-water mark is what sizes the frame.
        m_nextSlot = m_scopeSlotMarks.back();
        m_scopeSlotMarks.pop_back();
    }

    int DeclareLocal(const std::string& name, ValueType type, const SourceLocation& location)
    {
        auto& scope = m_scopes.back();
        if (scope.find(name) != scope.end())
        {
            Error(location, "'" + name + "' is already declared in this scope");
            return scope[name].slot;
        }

        int slot = m_nextSlot++;
        if (m_nextSlot > m_highWaterSlot) m_highWaterSlot = m_nextSlot;
        scope.emplace(name, LocalSymbol{ type, slot });
        return slot;
    }

    bool Lookup(const std::string& name, LocalSymbol& outSymbol) const
    {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope)
        {
            auto found = scope->find(name);
            if (found != scope->end()) { outSymbol = found->second; return true; }
        }
        return false;
    }

    // --- Statements ---------------------------------------------------------

    void AnalyzeStmt(Stmt& stmt)
    {
        switch (stmt.kind)
        {
            case StmtKind::LocalDecl: AnalyzeLocalDecl(static_cast<LocalDeclStmt&>(stmt)); break;

            case StmtKind::Expr:
                AnalyzeExpr(*static_cast<ExprStmt&>(stmt).expr);
                break;

            case StmtKind::Block: {
                auto& block = static_cast<BlockStmt&>(stmt);
                PushScope();
                for (StmtPtr& child : block.statements) AnalyzeStmt(*child);
                PopScope();
                break;
            }

            case StmtKind::If: {
                auto& node = static_cast<IfStmt&>(stmt);
                AnalyzeExpr(*node.condition);
                RequireBool(*node.condition, "if");
                AnalyzeStmt(*node.thenBranch);
                if (node.elseBranch) AnalyzeStmt(*node.elseBranch);
                break;
            }

            case StmtKind::While: {
                auto& node = static_cast<WhileStmt&>(stmt);
                AnalyzeExpr(*node.condition);
                RequireBool(*node.condition, "while");
                ++m_loopDepth;
                AnalyzeStmt(*node.body);
                --m_loopDepth;
                break;
            }

            case StmtKind::For: {
                auto& node = static_cast<ForStmt&>(stmt);
                PushScope();
                if (node.init) AnalyzeStmt(*node.init);
                if (node.condition)
                {
                    AnalyzeExpr(*node.condition);
                    RequireBool(*node.condition, "for");
                }
                if (node.step) AnalyzeExpr(*node.step);
                ++m_loopDepth;
                AnalyzeStmt(*node.body);
                --m_loopDepth;
                PopScope();
                break;
            }

            case StmtKind::Return: AnalyzeReturn(static_cast<ReturnStmt&>(stmt)); break;

            case StmtKind::Break:
                if (m_loopDepth == 0) Error(stmt.location, "'break' is only allowed inside a loop");
                break;

            case StmtKind::Continue:
                if (m_loopDepth == 0) Error(stmt.location, "'continue' is only allowed inside a loop");
                break;

            default: break;
        }
    }

    void AnalyzeLocalDecl(LocalDeclStmt& stmt)
    {
        if (stmt.initializer) AnalyzeExpr(*stmt.initializer);

        if (stmt.inferred)
        {
            if (!stmt.initializer)
            {
                Error(stmt.location, "'" + stmt.name + "' needs an initializer for its type to be inferred");
                stmt.declaredType = ValueType::Unknown;
            }
            else if (stmt.initializer->resolvedType == ValueType::Void)
            {
                Error(stmt.initializer->location, "cannot infer the type of '" + stmt.name + "' from an expression that produces no value");
                stmt.declaredType = ValueType::Unknown;
            }
            else
            {
                stmt.declaredType = stmt.initializer->resolvedType;
            }
        }
        else
        {
            if (stmt.declaredType == ValueType::Void)
            {
                Error(stmt.location, "variable '" + stmt.name + "' cannot have type 'void'");
                stmt.declaredType = ValueType::Unknown;
            }
            if (stmt.initializer)
                CheckAssignable(*stmt.initializer, stmt.declaredType, "in the initializer of '" + stmt.name + "'");
        }

        stmt.localSlot = DeclareLocal(stmt.name, stmt.declaredType, stmt.location);
    }

    void AnalyzeReturn(ReturnStmt& stmt)
    {
        const ValueType returnType = m_currentMethod ? m_currentMethod->returnType : ValueType::Void;

        if (!stmt.value)
        {
            if (returnType != ValueType::Void)
                Error(stmt.location, "a method returning " + Quoted(returnType) + " must return a value");
            return;
        }

        AnalyzeExpr(*stmt.value);
        if (returnType == ValueType::Void)
        {
            Error(stmt.location, "a method returning 'void' cannot return a value");
            return;
        }
        CheckAssignable(*stmt.value, returnType, "in a return statement");
    }

    void RequireBool(Expr& expr, const char* construct)
    {
        if (expr.resolvedType == ValueType::Bool || IsPoisoned(expr.resolvedType)) return;
        Error(expr.location, std::string("the condition of '") + construct + "' must be 'bool', found " + Quoted(expr.resolvedType));
    }

    // The one implicit conversion in the language, applied wherever a
    // value flows into a declared type.
    bool CheckAssignable(Expr& value, ValueType target, const std::string& context)
    {
        if (IsPoisoned(target) || IsPoisoned(value.resolvedType)) return true;
        if (value.resolvedType == target) return true;
        if (target == ValueType::Float && value.resolvedType == ValueType::Int)
        {
            value.conversion = ValueType::Float;
            return true;
        }
        Error(value.location, "cannot convert " + Quoted(value.resolvedType) + " to " + Quoted(target) + " " + context);
        return false;
    }

    // --- Expressions ---------------------------------------------------------

    void AnalyzeExpr(Expr& expr)
    {
        switch (expr.kind)
        {
            case ExprKind::IntLiteral: expr.resolvedType = ValueType::Int; break;
            case ExprKind::FloatLiteral: expr.resolvedType = ValueType::Float; break;
            case ExprKind::BoolLiteral: expr.resolvedType = ValueType::Bool; break;
            case ExprKind::StringLiteral: expr.resolvedType = ValueType::String; break;

            case ExprKind::Identifier: {
                auto& node = static_cast<IdentifierExpr&>(expr);
                LocalSymbol symbol;
                if (!Lookup(node.name, symbol))
                {
                    Error(node.location, "use of undeclared identifier '" + node.name + "'");
                    node.resolvedType = ValueType::Unknown;
                    break;
                }
                node.localSlot = symbol.slot;
                node.resolvedType = symbol.type;
                break;
            }

            case ExprKind::Member: {
                auto& node = static_cast<MemberExpr&>(expr);
                Error(node.location, "'" + DescribeCallee(node) + "' is not a value; only a method can be named this way");
                node.resolvedType = ValueType::Unknown;
                break;
            }

            case ExprKind::Call: AnalyzeCall(static_cast<CallExpr&>(expr)); break;
            case ExprKind::Unary: AnalyzeUnary(static_cast<UnaryExpr&>(expr)); break;
            case ExprKind::Binary: AnalyzeBinary(static_cast<BinaryExpr&>(expr)); break;
            case ExprKind::Assign: AnalyzeAssign(static_cast<AssignExpr&>(expr)); break;

            default: expr.resolvedType = ValueType::Unknown; break;
        }
    }

    void AnalyzeUnary(UnaryExpr& expr)
    {
        AnalyzeExpr(*expr.operand);
        const ValueType operandType = expr.operand->resolvedType;

        if (expr.op == UnaryOp::Not)
        {
            if (operandType != ValueType::Bool && !IsPoisoned(operandType))
                Error(expr.location, "'!' needs a 'bool' operand, found " + Quoted(operandType));
            expr.resolvedType = ValueType::Bool;
            return;
        }

        if (!IsNumeric(operandType))
        {
            if (!IsPoisoned(operandType))
                Error(expr.location, "unary '-' needs an 'int' or a 'float' operand, found " + Quoted(operandType));
            expr.resolvedType = ValueType::Unknown;
            return;
        }
        expr.resolvedType = operandType;
    }

    // Brings a numeric pair to a common type, widening the int side when
    // the other side is a float. Returns Unknown when the pair is not
    // numeric at all.
    ValueType UnifyNumeric(Expr& lhs, Expr& rhs)
    {
        if (!IsNumeric(lhs.resolvedType) || !IsNumeric(rhs.resolvedType)) return ValueType::Unknown;
        if (lhs.resolvedType == ValueType::Float || rhs.resolvedType == ValueType::Float)
        {
            if (lhs.resolvedType == ValueType::Int) lhs.conversion = ValueType::Float;
            if (rhs.resolvedType == ValueType::Int) rhs.conversion = ValueType::Float;
            return ValueType::Float;
        }
        return ValueType::Int;
    }

    void AnalyzeBinary(BinaryExpr& expr)
    {
        AnalyzeExpr(*expr.lhs);
        AnalyzeExpr(*expr.rhs);

        const ValueType lhsType = expr.lhs->resolvedType;
        const ValueType rhsType = expr.rhs->resolvedType;
        const bool poisoned = IsPoisoned(lhsType) || IsPoisoned(rhsType);

        switch (expr.op)
        {
            case BinaryOp::LogicalAnd:
            case BinaryOp::LogicalOr: {
                if (!poisoned && (lhsType != ValueType::Bool || rhsType != ValueType::Bool))
                    Error(expr.location, std::string("'") + OperatorText(expr.op) + "' needs 'bool' operands, found " +
                                             Quoted(lhsType) + " and " + Quoted(rhsType));
                expr.operandType = ValueType::Bool;
                expr.resolvedType = ValueType::Bool;
                return;
            }

            case BinaryOp::Equal:
            case BinaryOp::NotEqual: {
                if (lhsType == ValueType::Bool && rhsType == ValueType::Bool) expr.operandType = ValueType::Bool;
                else if (lhsType == ValueType::String && rhsType == ValueType::String) expr.operandType = ValueType::String;
                else expr.operandType = UnifyNumeric(*expr.lhs, *expr.rhs);

                if (IsPoisoned(expr.operandType) && !poisoned)
                    Error(expr.location, std::string("'") + OperatorText(expr.op) + "' cannot compare " +
                                             Quoted(lhsType) + " with " + Quoted(rhsType));
                expr.resolvedType = ValueType::Bool;
                return;
            }

            case BinaryOp::Less:
            case BinaryOp::Greater:
            case BinaryOp::LessEqual:
            case BinaryOp::GreaterEqual: {
                expr.operandType = UnifyNumeric(*expr.lhs, *expr.rhs);
                if (IsPoisoned(expr.operandType) && !poisoned)
                    Error(expr.location, std::string("'") + OperatorText(expr.op) + "' needs numeric operands, found " +
                                             Quoted(lhsType) + " and " + Quoted(rhsType));
                expr.resolvedType = ValueType::Bool;
                return;
            }

            case BinaryOp::Add: {
                // A string on the left absorbs whatever is on the right,
                // turning it into text.
                if (lhsType == ValueType::String)
                {
                    if (rhsType != ValueType::String && !CanStringify(rhsType))
                    {
                        if (!poisoned)
                            Error(expr.location, "'+' cannot join a 'string' with " + Quoted(rhsType));
                    }
                    else if (rhsType != ValueType::String)
                    {
                        expr.rhs->conversion = ValueType::String;
                    }
                    expr.operandType = ValueType::String;
                    expr.resolvedType = ValueType::String;
                    return;
                }
                AnalyzeArithmetic(expr, poisoned);
                return;
            }

            case BinaryOp::Sub:
            case BinaryOp::Mul:
            case BinaryOp::Div:
            case BinaryOp::Mod:
                AnalyzeArithmetic(expr, poisoned);
                return;

            default:
                expr.resolvedType = ValueType::Unknown;
                return;
        }
    }

    void AnalyzeArithmetic(BinaryExpr& expr, bool poisoned)
    {
        expr.operandType = UnifyNumeric(*expr.lhs, *expr.rhs);
        if (IsPoisoned(expr.operandType))
        {
            if (!poisoned)
                Error(expr.location, std::string("'") + OperatorText(expr.op) + "' needs numeric operands, found " +
                                         Quoted(expr.lhs->resolvedType) + " and " + Quoted(expr.rhs->resolvedType));
            expr.resolvedType = ValueType::Unknown;
            return;
        }
        expr.resolvedType = expr.operandType;
    }

    static bool CanStringify(ValueType type)
    {
        return type == ValueType::Int || type == ValueType::Float || type == ValueType::Bool || type == ValueType::String;
    }

    static const char* OperatorText(BinaryOp op)
    {
        switch (op)
        {
            case BinaryOp::Add: return "+";
            case BinaryOp::Sub: return "-";
            case BinaryOp::Mul: return "*";
            case BinaryOp::Div: return "/";
            case BinaryOp::Mod: return "%";
            case BinaryOp::Equal: return "==";
            case BinaryOp::NotEqual: return "!=";
            case BinaryOp::Less: return "<";
            case BinaryOp::Greater: return ">";
            case BinaryOp::LessEqual: return "<=";
            case BinaryOp::GreaterEqual: return ">=";
            case BinaryOp::LogicalAnd: return "&&";
            case BinaryOp::LogicalOr: return "||";
            default: return "?";
        }
    }

    void AnalyzeAssign(AssignExpr& expr)
    {
        if (expr.target->kind != ExprKind::Identifier)
        {
            Error(expr.location, "the left-hand side of an assignment must be a variable");
            AnalyzeExpr(*expr.value);
            expr.resolvedType = ValueType::Unknown;
            return;
        }

        AnalyzeExpr(*expr.target);
        AnalyzeExpr(*expr.value);

        const ValueType targetType = expr.target->resolvedType;
        const ValueType valueType = expr.value->resolvedType;
        expr.resolvedType = targetType;

        if (expr.op == AssignOp::Assign)
        {
            CheckAssignable(*expr.value, targetType, "in an assignment");
            return;
        }

        const bool poisoned = IsPoisoned(targetType) || IsPoisoned(valueType);

        // `s += x` appends text; every other compound form is arithmetic.
        if (expr.op == AssignOp::AddAssign && targetType == ValueType::String)
        {
            if (valueType != ValueType::String && !CanStringify(valueType))
            {
                if (!poisoned) Error(expr.location, "'+=' cannot append " + Quoted(valueType) + " to a 'string'");
            }
            else if (valueType != ValueType::String)
            {
                expr.value->conversion = ValueType::String;
            }
            expr.operandType = ValueType::String;
            return;
        }

        if (!IsNumeric(targetType) || !IsNumeric(valueType))
        {
            if (!poisoned)
                Error(expr.location, std::string("'") + CompoundOperatorText(expr.op) + "' needs numeric operands, found " +
                                         Quoted(targetType) + " and " + Quoted(valueType));
            expr.operandType = ValueType::Unknown;
            return;
        }

        if (targetType == ValueType::Float)
        {
            if (valueType == ValueType::Int) expr.value->conversion = ValueType::Float;
            expr.operandType = ValueType::Float;
            return;
        }

        // An int target cannot absorb a float without losing information.
        if (valueType == ValueType::Float)
        {
            Error(expr.location, "cannot convert 'float' to 'int' in a compound assignment");
            expr.operandType = ValueType::Unknown;
            return;
        }
        expr.operandType = ValueType::Int;
    }

    static const char* CompoundOperatorText(AssignOp op)
    {
        switch (op)
        {
            case AssignOp::AddAssign: return "+=";
            case AssignOp::SubAssign: return "-=";
            case AssignOp::MulAssign: return "*=";
            case AssignOp::DivAssign: return "/=";
            default: return "=";
        }
    }

    // --- Calls ----------------------------------------------------------------

    static std::string DescribeCallee(const MemberExpr& member)
    {
        if (member.base && member.base->kind == ExprKind::Identifier)
            return static_cast<const IdentifierExpr*>(member.base.get())->name + "." + member.member;
        return "." + member.member;
    }

    void AnalyzeCall(CallExpr& call)
    {
        for (ExprPtr& arg : call.args) AnalyzeExpr(*arg);

        if (!call.callee)
        {
            call.resolvedType = ValueType::Unknown;
            return;
        }

        if (call.callee->kind == ExprKind::Identifier)
        {
            const auto* callee = static_cast<const IdentifierExpr*>(call.callee.get());
            const std::string qualified = (m_currentClass ? m_currentClass->name : std::string()) + "." + callee->name;

            auto found = m_methods.find(qualified);
            if (found == m_methods.end())
            {
                Error(call.location, "no method named '" + callee->name + "' exists here");
                call.resolvedType = ValueType::Unknown;
                return;
            }
            BindScriptMethod(call, *found->second, callee->name);
            return;
        }

        if (call.callee->kind == ExprKind::Member)
        {
            const auto* member = static_cast<const MemberExpr*>(call.callee.get());
            const std::string qualified = DescribeCallee(*member);

            auto native = m_natives.find(qualified);
            if (native != m_natives.end())
            {
                BindNative(call, native->second, qualified);
                return;
            }

            auto found = m_methods.find(qualified);
            if (found != m_methods.end())
            {
                BindScriptMethod(call, *found->second, qualified);
                return;
            }

            Error(call.location, "no method named '" + qualified + "' exists");
            call.resolvedType = ValueType::Unknown;
            return;
        }

        Error(call.location, "this expression cannot be called");
        call.resolvedType = ValueType::Unknown;
    }

    void BindScriptMethod(CallExpr& call, MethodDecl& method, const std::string& displayName)
    {
        call.target = CallTarget::ScriptMethod;
        call.targetIndex = method.functionIndex;
        call.resolvedType = method.returnType;

        if (call.args.size() != method.params.size())
        {
            Error(call.location, "method '" + displayName + "' takes " + std::to_string(method.params.size()) +
                                     " argument(s) but " + std::to_string(call.args.size()) + " were given");
            return;
        }

        for (size_t i = 0; i < call.args.size(); ++i)
            CheckAssignable(*call.args[i], method.params[i].type, "in argument " + std::to_string(i + 1) + " of '" + displayName + "'");
    }

    void BindNative(CallExpr& call, const std::vector<NativeFunctionId>& candidates, const std::string& displayName)
    {
        // Every built-in takes exactly one argument and produces no
        // value, so selecting one is purely a question of the argument's
        // type.
        call.target = CallTarget::Native;
        call.resolvedType = ValueType::Void;

        if (call.args.size() != 1)
        {
            Error(call.location, "'" + displayName + "' takes 1 argument but " + std::to_string(call.args.size()) + " were given");
            return;
        }

        const NativeFunctionSignature* natives = NativeFunctionTable();
        const ValueType argType = call.args[0]->resolvedType;

        for (NativeFunctionId candidate : candidates)
        {
            if (natives[(u32)candidate].parameterType == argType)
            {
                call.targetIndex = (u32)candidate;
                return;
            }
        }

        // No exact form; fall back to the single implicit widening.
        if (argType == ValueType::Int)
        {
            for (NativeFunctionId candidate : candidates)
            {
                if (natives[(u32)candidate].parameterType == ValueType::Float)
                {
                    call.targetIndex = (u32)candidate;
                    call.args[0]->conversion = ValueType::Float;
                    return;
                }
            }
        }

        if (!IsPoisoned(argType))
            Error(call.args[0]->location, "'" + displayName + "' does not accept an argument of type " + Quoted(argType));
    }
};

} // namespace

bool Analyze(Program& program, DiagnosticList& diagnostics)
{
    Analyzer analyzer(program, diagnostics);
    return analyzer.Run();
}

} // namespace Fluxion::Script
