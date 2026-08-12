#include <Fluxion/ShaderCompiler/Semantic/SemanticAnalyzer.hpp>

#include <unordered_map>
#include <vector>

namespace Fluxion::ShaderCompiler
{

namespace
{

int VectorComponentCount(TypeKind kind)
{
    switch (kind)
    {
        case TypeKind::Vec2: return 2;
        case TypeKind::Vec3: return 3;
        case TypeKind::Vec4: return 4;
        default: return 0;
    }
}

ShaderType VectorOfSize(int n)
{
    switch (n)
    {
        case 1: return { TypeKind::Float };
        case 2: return { TypeKind::Vec2 };
        case 3: return { TypeKind::Vec3 };
        case 4: return { TypeKind::Vec4 };
        default: return { TypeKind::Void };
    }
}

bool IsScalar(TypeKind kind) { return kind == TypeKind::Float || kind == TypeKind::Int || kind == TypeKind::Uint || kind == TypeKind::Bool; }
bool IsVector(TypeKind kind) { return VectorComponentCount(kind) > 0; }

int SwizzleChannelIndex(char c)
{
    switch (c)
    {
        case 'x': case 'r': return 0;
        case 'y': case 'g': return 1;
        case 'z': case 'b': return 2;
        case 'w': case 'a': return 3;
        default: return -1;
    }
}

class Analyzer
{
public:
    Analyzer(Program& program, DiagnosticList& diagnostics) : m_program(program), m_diagnostics(diagnostics) {}

    bool Run()
    {
        // A compute-only built-in: the X component of the global
        // invocation ID. Seeded unconditionally (same permissive style as
        // every other global here) -- harmless if a vertex/fragment
        // shader never references it.
        m_globals["ThreadID"] = ShaderType{ TypeKind::Uint };

        // Pass 1: collect every global name (uniforms, stage IO, consts,
        // output slots, function overload sets) before checking any
        // function body, so forward references between declarations work.
        for (DeclPtr& decl : m_program.declarations)
        {
            switch (decl->kind)
            {
                case DeclKind::Uniform: {
                    auto* d = static_cast<UniformDecl*>(decl.get());
                    m_globals[d->name] = d->type;
                    break;
                }
                case DeclKind::StageIO: {
                    auto* d = static_cast<StageIODecl*>(decl.get());
                    m_globals[d->name] = d->type;
                    break;
                }
                case DeclKind::GlobalConst: {
                    auto* d = static_cast<GlobalConstDecl*>(decl.get());
                    m_globals[d->name] = d->type;
                    break;
                }
                case DeclKind::OutputSlot: {
                    auto* d = static_cast<OutputSlotDecl*>(decl.get());
                    m_globals[d->name] = d->type;
                    break;
                }
                case DeclKind::Function: {
                    auto* d = static_cast<FunctionDecl*>(decl.get());
                    m_functions[d->name].push_back(d);
                    break;
                }
                default: break;
            }
        }

        for (DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind == DeclKind::GlobalConst)
                AnalyzeExpr(*static_cast<GlobalConstDecl*>(decl.get())->initializer);
            if (decl->kind == DeclKind::Function)
                AnalyzeFunction(*static_cast<FunctionDecl*>(decl.get()));
        }

        return !m_diagnostics.HasErrors();
    }

private:
    Program& m_program;
    DiagnosticList& m_diagnostics;
    std::unordered_map<std::string, ShaderType> m_globals;
    std::unordered_map<std::string, std::vector<FunctionDecl*>> m_functions;
    std::vector<std::unordered_map<std::string, ShaderType>> m_scopes;

    void Error(const SourceLocation& loc, const std::string& msg) { m_diagnostics.AddError(loc, msg); }

    void PushScope() { m_scopes.emplace_back(); }
    void PopScope() { m_scopes.pop_back(); }
    void Declare(const std::string& name, ShaderType type) { m_scopes.back()[name] = type; }

    bool Lookup(const std::string& name, ShaderType& outType) const
    {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it)
        {
            auto found = it->find(name);
            if (found != it->end()) { outType = found->second; return true; }
        }
        auto found = m_globals.find(name);
        if (found != m_globals.end()) { outType = found->second; return true; }
        return false;
    }

    void AnalyzeFunction(FunctionDecl& fn)
    {
        PushScope();
        for (Param& p : fn.params) Declare(p.name, p.type);
        AnalyzeStmt(*fn.body, fn);
        PopScope();
    }

    void AnalyzeStmt(Stmt& stmt, FunctionDecl& fn)
    {
        switch (stmt.kind)
        {
            case StmtKind::Expr:
                AnalyzeExpr(*static_cast<ExprStmt&>(stmt).expr);
                break;
            case StmtKind::VarDecl: {
                auto& s = static_cast<VarDeclStmt&>(stmt);
                if (s.initializer) AnalyzeExpr(*s.initializer);
                Declare(s.name, s.type);
                break;
            }
            case StmtKind::Block: {
                auto& s = static_cast<BlockStmt&>(stmt);
                PushScope();
                for (StmtPtr& child : s.statements) AnalyzeStmt(*child, fn);
                PopScope();
                break;
            }
            case StmtKind::If: {
                auto& s = static_cast<IfStmt&>(stmt);
                AnalyzeExpr(*s.condition);
                AnalyzeStmt(*s.thenBranch, fn);
                if (s.elseBranch) AnalyzeStmt(*s.elseBranch, fn);
                break;
            }
            case StmtKind::For: {
                auto& s = static_cast<ForStmt&>(stmt);
                PushScope();
                if (s.init) AnalyzeStmt(*s.init, fn);
                if (s.condition) AnalyzeExpr(*s.condition);
                if (s.iteration) AnalyzeExpr(*s.iteration);
                AnalyzeStmt(*s.body, fn);
                PopScope();
                break;
            }
            case StmtKind::While: {
                auto& s = static_cast<WhileStmt&>(stmt);
                AnalyzeExpr(*s.condition);
                AnalyzeStmt(*s.body, fn);
                break;
            }
            case StmtKind::Return: {
                auto& s = static_cast<ReturnStmt&>(stmt);
                if (s.value) AnalyzeExpr(*s.value);
                break;
            }
        }
    }

    void AnalyzeExpr(Expr& expr)
    {
        switch (expr.kind)
        {
            case ExprKind::IntLiteral: expr.resolvedType = { TypeKind::Int }; break;
            case ExprKind::FloatLiteral: expr.resolvedType = { TypeKind::Float }; break;
            case ExprKind::BoolLiteral: expr.resolvedType = { TypeKind::Bool }; break;

            case ExprKind::VarRef: {
                auto& e = static_cast<VarRefExpr&>(expr);
                ShaderType type;
                if (!Lookup(e.name, type))
                {
                    Error(e.location, "use of undeclared identifier '" + e.name + "'");
                    type = { TypeKind::Float };
                }
                expr.resolvedType = type;
                break;
            }

            case ExprKind::Unary: {
                auto& e = static_cast<UnaryExpr&>(expr);
                AnalyzeExpr(*e.operand);
                expr.resolvedType = (e.op == UnaryOp::Not) ? ShaderType{ TypeKind::Bool } : e.operand->resolvedType;
                break;
            }

            case ExprKind::Binary: {
                auto& e = static_cast<BinaryExpr&>(expr);
                AnalyzeExpr(*e.lhs);
                AnalyzeExpr(*e.rhs);
                if (e.op == BinaryOp::Eq || e.op == BinaryOp::NotEq || e.op == BinaryOp::Less ||
                    e.op == BinaryOp::Greater || e.op == BinaryOp::LessEq || e.op == BinaryOp::GreaterEq ||
                    e.op == BinaryOp::And || e.op == BinaryOp::Or)
                {
                    expr.resolvedType = { TypeKind::Bool };
                }
                else
                {
                    expr.resolvedType = ResolveArithmetic(e.lhs->resolvedType, e.rhs->resolvedType, e.location);
                }
                break;
            }

            case ExprKind::Member: {
                auto& e = static_cast<MemberExpr&>(expr);
                AnalyzeExpr(*e.base);
                int baseComponents = VectorComponentCount(e.base->resolvedType.kind);
                if (baseComponents == 0)
                {
                    Error(e.location, "swizzle '." + e.member + "' used on a non-vector type");
                    expr.resolvedType = { TypeKind::Float };
                    break;
                }
                if (e.member.size() < 1 || e.member.size() > 4)
                {
                    Error(e.location, "invalid swizzle '." + e.member + "'");
                    expr.resolvedType = { TypeKind::Float };
                    break;
                }
                for (char c : e.member)
                {
                    int idx = SwizzleChannelIndex(c);
                    if (idx < 0 || idx >= baseComponents)
                    {
                        Error(e.location, "swizzle channel '" + std::string(1, c) + "' is out of range for this vector");
                        break;
                    }
                }
                expr.resolvedType = VectorOfSize((int)e.member.size());
                break;
            }

            case ExprKind::Index: {
                auto& e = static_cast<IndexExpr&>(expr);
                AnalyzeExpr(*e.base);
                AnalyzeExpr(*e.index);
                expr.resolvedType = IsVector(e.base->resolvedType.kind) ? ShaderType{ TypeKind::Float } : e.base->resolvedType;
                break;
            }

            case ExprKind::Constructor: {
                auto& e = static_cast<ConstructorExpr&>(expr);
                for (ExprPtr& arg : e.args) AnalyzeExpr(*arg);
                expr.resolvedType = e.targetType;
                break;
            }

            case ExprKind::Ternary: {
                auto& e = static_cast<TernaryExpr&>(expr);
                AnalyzeExpr(*e.condition);
                AnalyzeExpr(*e.thenExpr);
                AnalyzeExpr(*e.elseExpr);
                expr.resolvedType = e.thenExpr->resolvedType;
                break;
            }

            case ExprKind::Assign: {
                auto& e = static_cast<AssignExpr&>(expr);
                if (e.target->kind != ExprKind::VarRef && e.target->kind != ExprKind::Member && e.target->kind != ExprKind::Index)
                    Error(e.location, "left-hand side of assignment is not assignable");
                AnalyzeExpr(*e.target);
                AnalyzeExpr(*e.value);
                expr.resolvedType = e.target->resolvedType;
                break;
            }

            case ExprKind::Call: {
                auto& e = static_cast<CallExpr&>(expr);
                for (ExprPtr& arg : e.args) AnalyzeExpr(*arg);
                expr.resolvedType = ResolveCall(e);
                break;
            }
        }
    }

    ShaderType ResolveArithmetic(const ShaderType& a, const ShaderType& b, const SourceLocation& loc)
    {
        if (a == b) return a;
        if (IsVector(a.kind) && IsScalar(b.kind)) return a;
        if (IsScalar(a.kind) && IsVector(b.kind)) return b;
        if ((a.kind == TypeKind::Mat3 && b.kind == TypeKind::Vec3)) return b;
        if ((a.kind == TypeKind::Mat4 && b.kind == TypeKind::Vec4)) return b;
        if (IsScalar(a.kind) && IsScalar(b.kind)) return (a.kind == TypeKind::Float || b.kind == TypeKind::Float) ? ShaderType{ TypeKind::Float } : a;
        Error(loc, "operand types are incompatible in this expression");
        return a;
    }

    ShaderType ResolveCall(CallExpr& call)
    {
        // Built-in functions get a small structural heuristic (result is
        // a vector if any argument is a vector, otherwise float/bool as
        // appropriate) rather than an exhaustive per-function signature
        // table -- this covers the common GLSL-style math/sampling
        // builtins without hand-listing every overload.
        static const std::unordered_map<std::string, TypeKind> kFixedResultBuiltins = {
            { "dot", TypeKind::Float }, { "length", TypeKind::Float }, { "distance", TypeKind::Float },
            { "texture2D", TypeKind::Vec4 }, { "texture", TypeKind::Vec4 }, { "textureCube", TypeKind::Vec4 },
        };
        auto fixedIt = kFixedResultBuiltins.find(call.callee);
        if (fixedIt != kFixedResultBuiltins.end())
            return { fixedIt->second };

        auto userFn = m_functions.find(call.callee);
        if (userFn != m_functions.end())
        {
            FunctionDecl* best = SelectOverload(userFn->second, call);
            if (best) return best->returnType;
        }

        // Unknown callee, or a builtin outside the fixed-result table
        // (normalize/mix/clamp/pow/abs/min/max/sin/cos/...): the result
        // follows the widest argument type.
        ShaderType widest = { TypeKind::Float };
        for (ExprPtr& arg : call.args)
            if (IsVector(arg->resolvedType.kind)) widest = arg->resolvedType;
        if (userFn == m_functions.end() && kFixedResultBuiltins.find(call.callee) == kFixedResultBuiltins.end() &&
            !call.args.empty())
        {
            widest = call.args[0]->resolvedType;
            for (ExprPtr& arg : call.args)
                if (IsVector(arg->resolvedType.kind)) { widest = arg->resolvedType; break; }
        }
        return widest;
    }

    FunctionDecl* SelectOverload(const std::vector<FunctionDecl*>& candidates, const CallExpr& call)
    {
        FunctionDecl* arityMatch = nullptr;
        for (FunctionDecl* candidate : candidates)
        {
            if (candidate->params.size() != call.args.size()) continue;
            if (!arityMatch) arityMatch = candidate;

            bool exact = true;
            for (size_t i = 0; i < candidate->params.size(); ++i)
                if (candidate->params[i].type != call.args[i]->resolvedType) { exact = false; break; }
            if (exact) return candidate;
        }
        if (!arityMatch)
            Error(call.location, "no overload of '" + call.callee + "' takes " + std::to_string(call.args.size()) + " argument(s)");
        return arityMatch;
    }
};

} // namespace

bool Analyze(Program& program, DiagnosticList& diagnostics)
{
    Analyzer analyzer(program, diagnostics);
    return analyzer.Run();
}

} // namespace Fluxion::ShaderCompiler
