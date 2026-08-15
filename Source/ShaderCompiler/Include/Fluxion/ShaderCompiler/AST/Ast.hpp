#pragma once

#include <Fluxion/ShaderCompiler/Diagnostics.hpp>

#include <memory>
#include <string>
#include <vector>

// A hand-rolled, NO_RTTI-safe AST: every node carries an explicit Kind
// enum instead of relying on dynamic_cast/typeid, and callers switch on
// Kind() before static_cast'ing to the concrete node type -- the same
// closed-set-of-variants pattern the engine already uses elsewhere for
// backend/state enums (e.g. FluxionRHIBackendType's switch in RHI.c).

namespace Fluxion::ShaderCompiler
{

enum class TypeKind
{
    Void, Bool, Int, Uint, Float,
    Vec2, Vec3, Vec4, Mat3, Mat4,
    Sampler2D, SamplerCube,
    Unresolved, // a named (struct) type not yet looked up
};

// The logical binding-frequency group a [Uniform]/[Texture] resource
// belongs to (matches the portable RHI's FluxionRHIBindGroupLayoutHandle
// indices: Global=0, Frame=1, Material=2, Object=3) -- lives here rather
// than in IR/ShaderIR.hpp because UniformDecl (an AST node) needs it, and
// ShaderIR.hpp already includes this header, not the other way around.
enum class BindingGroup { Global, Frame, Material, Object };

struct ShaderType
{
    TypeKind kind = TypeKind::Void;
    std::string structName; // valid only when kind == Unresolved

    // A real constructor (rather than relying on aggregate brace-init)
    // so call sites can write `{ TypeKind::Float }` without every one
    // also having to spell out `structName`'s default.
    ShaderType(TypeKind k = TypeKind::Void, std::string name = "") : kind(k), structName(std::move(name)) {}

    bool operator==(const ShaderType& other) const { return kind == other.kind && structName == other.structName; }
    bool operator!=(const ShaderType& other) const { return !(*this == other); }
};

// --- Expressions -------------------------------------------------------

enum class ExprKind
{
    IntLiteral, FloatLiteral, BoolLiteral,
    VarRef, Call, Binary, Unary, Member, Index, Constructor, Ternary, Assign,
};

struct Expr
{
    ExprKind kind;
    SourceLocation location;
    ShaderType resolvedType; // filled in by semantic analysis

    explicit Expr(ExprKind k) : kind(k) {}
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLiteralExpr : Expr { long long value = 0; IntLiteralExpr() : Expr(ExprKind::IntLiteral) {} };
struct FloatLiteralExpr : Expr { double value = 0.0; FloatLiteralExpr() : Expr(ExprKind::FloatLiteral) {} };
struct BoolLiteralExpr : Expr { bool value = false; BoolLiteralExpr() : Expr(ExprKind::BoolLiteral) {} };
struct VarRefExpr : Expr { std::string name; VarRefExpr() : Expr(ExprKind::VarRef) {} };

struct CallExpr : Expr
{
    std::string callee;
    std::vector<ExprPtr> args;
    CallExpr() : Expr(ExprKind::Call) {}
};

enum class BinaryOp { Add, Sub, Mul, Div, Mod, Eq, NotEq, Less, Greater, LessEq, GreaterEq, And, Or };

struct BinaryExpr : Expr
{
    BinaryOp op;
    ExprPtr lhs, rhs;
    BinaryExpr() : Expr(ExprKind::Binary) {}
};

enum class UnaryOp { Negate, Not };

struct UnaryExpr : Expr
{
    UnaryOp op;
    ExprPtr operand;
    UnaryExpr() : Expr(ExprKind::Unary) {}
};

// Covers both swizzle access on a vector (`.xyz`) and, in the future,
// struct-field access -- semantic analysis decides which based on the
// base expression's resolved type.
struct MemberExpr : Expr
{
    ExprPtr base;
    std::string member;
    MemberExpr() : Expr(ExprKind::Member) {}
};

struct IndexExpr : Expr
{
    ExprPtr base;
    ExprPtr index;
    IndexExpr() : Expr(ExprKind::Index) {}
};

// A type-name-as-function-call, e.g. `vec3(a, b, c)`.
struct ConstructorExpr : Expr
{
    ShaderType targetType;
    std::vector<ExprPtr> args;
    ConstructorExpr() : Expr(ExprKind::Constructor) {}
};

struct TernaryExpr : Expr
{
    ExprPtr condition, thenExpr, elseExpr;
    TernaryExpr() : Expr(ExprKind::Ternary) {}
};

enum class AssignOp { Assign, AddAssign, SubAssign, MulAssign, DivAssign };

struct AssignExpr : Expr
{
    AssignOp op;
    ExprPtr target;
    ExprPtr value;
    AssignExpr() : Expr(ExprKind::Assign) {}
};

// --- Statements ----------------------------------------------------------

enum class StmtKind { Expr, VarDecl, Block, If, For, While, Return, Discard };

struct Stmt
{
    StmtKind kind;
    SourceLocation location;
    explicit Stmt(StmtKind k) : kind(k) {}
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct ExprStmt : Stmt { ExprPtr expr; ExprStmt() : Stmt(StmtKind::Expr) {} };

// This pixel does not exist. Carries nothing: unlike a return, there is
// no value, and unlike a break there is nowhere it goes to -- the whole
// invocation stops and writes nothing at all.
struct DiscardStmt : Stmt { DiscardStmt() : Stmt(StmtKind::Discard) {} };

struct VarDeclStmt : Stmt
{
    ShaderType type;
    std::string name;
    ExprPtr initializer; // may be null
    VarDeclStmt() : Stmt(StmtKind::VarDecl) {}
};

struct BlockStmt : Stmt
{
    std::vector<StmtPtr> statements;
    BlockStmt() : Stmt(StmtKind::Block) {}
};

struct IfStmt : Stmt
{
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch; // may be null
    IfStmt() : Stmt(StmtKind::If) {}
};

struct ForStmt : Stmt
{
    StmtPtr init;      // may be null (VarDeclStmt or ExprStmt)
    ExprPtr condition;  // may be null
    ExprPtr iteration;  // may be null
    StmtPtr body;
    ForStmt() : Stmt(StmtKind::For) {}
};

struct WhileStmt : Stmt
{
    ExprPtr condition;
    StmtPtr body;
    WhileStmt() : Stmt(StmtKind::While) {}
};

// Inside the entry function, `return expr;` is what routes a value to
// the function's designated [Target(N)]/[Output] -- the backends handle
// that routing at emission time (see HLSLBackend.cpp/GLSLBackend.cpp),
// since it doesn't change the meaning of this node itself.
struct ReturnStmt : Stmt
{
    ExprPtr value; // may be null (void return)
    ReturnStmt() : Stmt(StmtKind::Return) {}
};

// --- Top-level declarations ------------------------------------------------

enum class DeclKind { Function, Uniform, StageIO, OutputSlot, GlobalConst, Struct };

struct Decl
{
    DeclKind kind;
    SourceLocation location;
    explicit Decl(DeclKind k) : kind(k) {}
    virtual ~Decl() = default;
};
using DeclPtr = std::unique_ptr<Decl>;

struct Param { ShaderType type; std::string name; };

struct FunctionDecl : Decl
{
    ShaderType returnType;
    std::string name;
    std::vector<Param> params;
    StmtPtr body; // BlockStmt
    FunctionDecl() : Decl(DeclKind::Function) {}
};

struct UniformDecl : Decl
{
    ShaderType type;
    std::string name;
    // From an optional `[Uniform(Group)]`/`[Texture(Group)]`/
    // `[Buffer(Group)]` argument -- defaults to Material, the most common
    // case for a per-material constant, texture, or buffer.
    BindingGroup group = BindingGroup::Material;
    // True when this declaration came from `[Buffer(Group)]` -- a
    // read-write storage buffer (always element type `float` in v1)
    // rather than a `[Uniform]` constant or a `[Texture]` opaque resource.
    bool isStorageBuffer = false;
    UniformDecl() : Decl(DeclKind::Uniform) {}
};

enum class StageIODirection { In, Out };

struct StageIODecl : Decl
{
    StageIODirection direction;
    ShaderType type;
    std::string name;
    StageIODecl() : Decl(DeclKind::StageIO) {}
};

// From `[Target(N)] T name;`.
struct OutputSlotDecl : Decl
{
    int slot = 0;
    ShaderType type;
    std::string name;
    OutputSlotDecl() : Decl(DeclKind::OutputSlot) {}
};

struct GlobalConstDecl : Decl
{
    ShaderType type;
    std::string name;
    ExprPtr initializer;
    GlobalConstDecl() : Decl(DeclKind::GlobalConst) {}
};

struct StructField { ShaderType type; std::string name; };

struct StructDecl : Decl
{
    std::string name;
    std::vector<StructField> fields;
    StructDecl() : Decl(DeclKind::Struct) {}
};

struct Program
{
    std::vector<DeclPtr> declarations;
};

} // namespace Fluxion::ShaderCompiler
