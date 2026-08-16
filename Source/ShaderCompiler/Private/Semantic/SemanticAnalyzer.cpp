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

#include <Fluxion/ShaderCompiler/Semantic/SemanticAnalyzer.hpp>

#include <unordered_map>
#include <unordered_set>
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
                    CheckName(d->name, d->location);
                    m_globals[d->name] = d->type;
                    if (d->isStorageBuffer) m_storageBuffers.insert(d->name);
                    break;
                }
                case DeclKind::StageIO: {
                    auto* d = static_cast<StageIODecl*>(decl.get());
                    CheckName(d->name, d->location);
                    m_globals[d->name] = d->type;
                    break;
                }
                case DeclKind::GlobalConst: {
                    auto* d = static_cast<GlobalConstDecl*>(decl.get());
                    CheckName(d->name, d->location);
                    m_globals[d->name] = d->type;
                    break;
                }
                case DeclKind::OutputSlot: {
                    auto* d = static_cast<OutputSlotDecl*>(decl.get());
                    CheckName(d->name, d->location);
                    m_globals[d->name] = d->type;
                    break;
                }
                case DeclKind::Function: {
                    auto* d = static_cast<FunctionDecl*>(decl.get());
                    CheckName(d->name, d->location);
                    for (const Param& parameter : d->params) CheckName(parameter.name, d->location);
                    m_functions[d->name].push_back(d);
                    break;
                }
                case DeclKind::Struct: {
                    auto* d = static_cast<StructDecl*>(decl.get());
                    CheckName(d->name, d->location);
                    for (const StructField& field : d->fields) CheckName(field.name, d->location);
                    m_structs[d->name] = d;
                    break;
                }
                default: break;
            }
        }

        // A struct's own fields are checked before anything uses them, so
        // a field of an undeclared type is reported once, here, rather
        // than at every place that reads it.
        for (DeclPtr& decl : m_program.declarations)
        {
            if (decl->kind != DeclKind::Struct) continue;
            auto* d = static_cast<StructDecl*>(decl.get());
            for (const StructField& field : d->fields)
                ValidateType(field.type, d->location, "a field's type");
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

    // The globals that are storage buffers, by name. Needed because a
    // buffer's declared type is its ELEMENT type: `lights[i]` on a buffer
    // of structs must come out as the struct, and `irradianceSH[0]` on a
    // buffer of vectors as the vector -- while `v[0]` on an ordinary
    // vector variable stays a component. The expression alone cannot tell
    // those apart; what the name was declared as can.
    std::unordered_set<std::string> m_storageBuffers;
    std::unordered_map<std::string, std::vector<FunctionDecl*>> m_functions;

    // What each declared struct contains. A field's type is the only way
    // to know what `s.roughness` is, and without it every such read would
    // have to be guessed at -- which for a field the struct does not have
    // means guessing rather than saying so.
    std::unordered_map<std::string, StructDecl*> m_structs;
    std::vector<std::unordered_map<std::string, ShaderType>> m_scopes;

    void Error(const SourceLocation& loc, const std::string& msg) { m_diagnostics.AddError(loc, msg); }

    // Names this language accepts and one of its target languages does
    // not -- the union of both targets' reserved words that are not
    // keywords here. A shader compiles to two languages that disagree
    // about which words are taken (`packed` is fine in HLSL, reserved in
    // GLSL), so such a name would fail on one backend only, as a syntax
    // error in generated text at a line the author cannot see. Refused
    // outright: the alternatives are a rule about a language the author
    // is not writing in, or a silent rename that makes the generated
    // text stop matching what they wrote.
    static bool IsReservedByATargetLanguage(const std::string& name)
    {
        static const std::unordered_set<std::string> kReserved = {
            // Reserved by GLSL.
            "active", "asm", "attribute", "buffer", "cast", "class", "coherent",
            "common", "enum", "extern", "external", "filter", "fixed", "flat",
            "fvec2", "fvec3", "fvec4", "goto", "half", "highp", "hvec2", "hvec3",
            "hvec4", "inline", "input", "interface", "layout", "long", "lowp",
            "mediump", "namespace", "noinline", "noperspective", "output",
            "packed", "partition", "patch", "precision", "public", "readonly",
            "resource", "restrict", "sample", "shared", "short", "sizeof",
            "smooth", "subroutine", "superp", "template", "this", "typedef",
            "union", "unsigned", "using", "varying", "volatile", "writeonly",
            // Reserved by HLSL.
            "cbuffer", "centroid", "column_major", "compile", "groupshared",
            "linear", "matrix", "nointerpolation", "packoffset", "pass",
            "precise", "register", "row_major", "snorm", "static", "technique",
            "tbuffer", "unorm", "vector",
        };
        return kReserved.count(name) != 0;
    }

    void CheckName(const std::string& name, const SourceLocation& location)
    {
        if (!IsReservedByATargetLanguage(name)) return;

        Error(location, "'" + name + "' cannot be used as a name here -- it is a reserved word in one of the "
                        "languages this compiles to, and a shader using it would fail on some backends and not others");
    }

    static bool IsBuiltinFunction(const std::string& name)
    {
        static const std::unordered_set<std::string> kBuiltins = {
            "abs", "acos", "all", "any", "asin", "atan", "ceil", "clamp", "cos", "cosh",
            "cross", "degrees", "determinant", "distance", "dot", "exp", "exp2",
            "faceforward", "floor", "fract", "fwidth", "inverse", "inversesqrt",
            "length", "log", "log2", "max", "min", "mix", "mod", "normalize", "pow",
            "radians", "reflect", "refract", "round", "sign", "sin", "sinh",
            "smoothstep", "sqrt", "step", "tan", "tanh", "transpose", "trunc",
            "dFdx", "dFdy",
            "texture", "texture2D", "textureCube", "textureLod",
        };
        return kBuiltins.count(name) != 0;
    }

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

    // A type that names a struct nobody declared is caught here rather
    // than passed on. The backends take a type's name from the type
    // itself, so an unknown one would be emitted verbatim and fail in
    // whatever tool compiles the result -- with a message about generated
    // text nobody wrote.
    void ValidateType(const ShaderType& type, const SourceLocation& location, const char* what)
    {
        if (type.kind != TypeKind::Unresolved) return;
        if (m_structs.count(type.structName) != 0) return;

        Error(location, std::string(what) + " names '" + type.structName + "', which is not a declared struct");
    }

    void AnalyzeFunction(FunctionDecl& fn)
    {
        ValidateType(fn.returnType, fn.location, "this function's return type");

        PushScope();
        for (Param& p : fn.params)
        {
            ValidateType(p.type, fn.location, "a parameter's type");
            Declare(p.name, p.type);
        }
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
                CheckName(s.name, s.location);
                ValidateType(s.type, s.location, "this variable's type");
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
            case StmtKind::Discard:
                // Nothing to check here. It carries no expression, and
                // WHERE it is allowed depends on the stage, which this
                // pass does not know -- BuildIR does, and says so there.
                break;
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

                // A struct field, not a swizzle. Checked first because
                // the swizzle rules below would reject every one of them
                // as "used on a non-vector type", which is true and
                // unhelpful.
                if (e.base->resolvedType.kind == TypeKind::Unresolved)
                {
                    auto structIt = m_structs.find(e.base->resolvedType.structName);
                    if (structIt == m_structs.end())
                    {
                        Error(e.location, "'" + e.base->resolvedType.structName + "' is not a declared struct");
                        expr.resolvedType = { TypeKind::Float };
                        break;
                    }

                    bool foundField = false;
                    for (const StructField& field : structIt->second->fields)
                    {
                        if (field.name != e.member) continue;
                        expr.resolvedType = field.type;
                        foundField = true;
                        break;
                    }

                    if (!foundField)
                    {
                        Error(e.location, "'" + e.base->resolvedType.structName + "' has no field named '" + e.member + "'");
                        expr.resolvedType = { TypeKind::Float };
                    }
                    break;
                }

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

                // Indexing a STORAGE BUFFER yields one element of it,
                // whatever the element is. The vector rule below must not
                // apply: a buffer declared `Vector4 name` holds many
                // Vector4s, and `name[0]` is the first of them, not the
                // first component of anything. The two cases read
                // identically at the expression, so the declaration is
                // what tells them apart.
                if (e.base->kind == ExprKind::VarRef &&
                    m_storageBuffers.count(static_cast<VarRefExpr&>(*e.base).name) != 0)
                {
                    expr.resolvedType = e.base->resolvedType;
                    break;
                }

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
        // table -- this covers the ordinary maths and sampling builtins
        // without hand-listing every overload.
        static const std::unordered_map<std::string, TypeKind> kFixedResultBuiltins = {
            { "dot", TypeKind::Float }, { "length", TypeKind::Float }, { "distance", TypeKind::Float },
            { "texture2D", TypeKind::Vec4 }, { "texture", TypeKind::Vec4 }, { "textureCube", TypeKind::Vec4 },
            { "textureLod", TypeKind::Vec4 },
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

        // A name that is neither declared here nor built in.
        //
        // Reported rather than passed through. What used to happen was
        // that any unknown name became a call with a guessed result type,
        // the text was emitted verbatim, and it failed in whatever tool
        // compiled the output -- naming generated text nobody wrote. A
        // mistyped function and a material missing the one function it
        // has to declare both landed there.
        if (userFn == m_functions.end() && !IsBuiltinFunction(call.callee))
        {
            Error(call.location, "call to '" + call.callee + "', which is neither declared here nor a built-in function");
            return { TypeKind::Float };
        }

        // A builtin outside the fixed-result table
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
