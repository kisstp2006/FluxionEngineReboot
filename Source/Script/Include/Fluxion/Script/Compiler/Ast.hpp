#pragma once

#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Bytecode.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>

#include <memory>
#include <string>
#include <vector>

// The tree is built for a translation unit compiled without run-time type
// information: every node carries an explicit kind enum, and a consumer
// switches on that kind before static_cast'ing to the concrete node type.
// Ownership is a plain unique_ptr per node with a vector of children --
// the tree is built once, walked a few times and then dropped whole, so
// there is nothing an arena would buy here.

namespace Fluxion::Script
{

// --- Expressions -------------------------------------------------------

enum class ExprKind
{
    IntLiteral,
    FloatLiteral,
    BoolLiteral,
    StringLiteral,
    Identifier,
    Member,
    Call,
    Unary,
    Binary,
    Assign,
};

struct Expr
{
    ExprKind kind;
    SourceLocation location;

    // Filled in by semantic analysis.
    ValueType resolvedType = ValueType::Unknown;

    // A widening the analyzer decided this expression needs once its own
    // value has been produced: Float means an int result must be widened,
    // String means the result must be turned into text for a
    // concatenation. Unknown means no conversion.
    ValueType conversion = ValueType::Unknown;

    explicit Expr(ExprKind k) : kind(k) {}
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLiteralExpr : Expr
{
    long long value = 0;
    IntLiteralExpr() : Expr(ExprKind::IntLiteral) {}
};

struct FloatLiteralExpr : Expr
{
    double value = 0.0;
    FloatLiteralExpr() : Expr(ExprKind::FloatLiteral) {}
};

struct BoolLiteralExpr : Expr
{
    bool value = false;
    BoolLiteralExpr() : Expr(ExprKind::BoolLiteral) {}
};

struct StringLiteralExpr : Expr
{
    std::string value;
    StringLiteralExpr() : Expr(ExprKind::StringLiteral) {}
};

struct IdentifierExpr : Expr
{
    std::string name;

    // Frame slot the name resolves to, filled in by semantic analysis.
    int localSlot = -1;

    IdentifierExpr() : Expr(ExprKind::Identifier) {}
};

// `A.B`. Only ever meaningful as the callee of a call at this point,
// since no value in the language has members yet.
struct MemberExpr : Expr
{
    ExprPtr base;
    std::string member;
    MemberExpr() : Expr(ExprKind::Member) {}
};

enum class CallTarget
{
    Unresolved,
    ScriptMethod,
    Native,
};

struct CallExpr : Expr
{
    ExprPtr callee;
    std::vector<ExprPtr> args;

    // Filled in by semantic analysis. `targetIndex` is a module function
    // index for ScriptMethod and a NativeFunctionId for Native.
    CallTarget target = CallTarget::Unresolved;
    u32 targetIndex = 0;

    CallExpr() : Expr(ExprKind::Call) {}
};

enum class UnaryOp { Negate, Not };

struct UnaryExpr : Expr
{
    UnaryOp op = UnaryOp::Negate;
    ExprPtr operand;
    UnaryExpr() : Expr(ExprKind::Unary) {}
};

enum class BinaryOp
{
    Add, Sub, Mul, Div, Mod,
    Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual,
    LogicalAnd, LogicalOr,
};

struct BinaryExpr : Expr
{
    BinaryOp op = BinaryOp::Add;
    ExprPtr lhs;
    ExprPtr rhs;

    // The type both sides have been brought to, which selects the
    // opcode. Distinct from resolvedType: a comparison always resolves to
    // bool but may operate on floats.
    ValueType operandType = ValueType::Unknown;

    BinaryExpr() : Expr(ExprKind::Binary) {}
};

enum class AssignOp { Assign, AddAssign, SubAssign, MulAssign, DivAssign };

struct AssignExpr : Expr
{
    AssignOp op = AssignOp::Assign;
    ExprPtr target;
    ExprPtr value;

    // The arithmetic type of a compound assignment, unused for a plain
    // one.
    ValueType operandType = ValueType::Unknown;

    AssignExpr() : Expr(ExprKind::Assign) {}
};

// --- Statements --------------------------------------------------------

enum class StmtKind
{
    LocalDecl,
    Expr,
    Block,
    If,
    While,
    For,
    Return,
    Break,
    Continue,
};

struct Stmt
{
    StmtKind kind;
    SourceLocation location;
    explicit Stmt(StmtKind k) : kind(k) {}
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct LocalDeclStmt : Stmt
{
    // True when the declaration was written with an inferred type, in
    // which case declaredType is filled in from the initializer and an
    // initializer is mandatory.
    bool inferred = false;
    ValueType declaredType = ValueType::Unknown;
    std::string name;
    ExprPtr initializer; // may be null unless `inferred`

    int localSlot = -1;

    LocalDeclStmt() : Stmt(StmtKind::LocalDecl) {}
};

struct ExprStmt : Stmt
{
    ExprPtr expr;
    ExprStmt() : Stmt(StmtKind::Expr) {}
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

struct WhileStmt : Stmt
{
    ExprPtr condition;
    StmtPtr body;
    WhileStmt() : Stmt(StmtKind::While) {}
};

struct ForStmt : Stmt
{
    StmtPtr init;      // may be null; a LocalDeclStmt or an ExprStmt
    ExprPtr condition; // may be null, meaning "always true"
    ExprPtr step;      // may be null
    StmtPtr body;
    ForStmt() : Stmt(StmtKind::For) {}
};

struct ReturnStmt : Stmt
{
    ExprPtr value; // null for a bare `return;`
    ReturnStmt() : Stmt(StmtKind::Return) {}
};

struct BreakStmt : Stmt
{
    BreakStmt() : Stmt(StmtKind::Break) {}
};

struct ContinueStmt : Stmt
{
    ContinueStmt() : Stmt(StmtKind::Continue) {}
};

// --- Declarations ------------------------------------------------------

enum class DeclKind { Class, Method };

struct Decl
{
    DeclKind kind;
    SourceLocation location;
    explicit Decl(DeclKind k) : kind(k) {}
    virtual ~Decl() = default;
};
using DeclPtr = std::unique_ptr<Decl>;

struct ParamDecl
{
    ValueType type = ValueType::Unknown;
    std::string name;
    SourceLocation location;
};

struct MethodDecl : Decl
{
    ValueType returnType = ValueType::Void;
    std::string name;
    std::string qualifiedName; // "Class.Method", filled in by semantic analysis
    std::vector<ParamDecl> params;
    StmtPtr body; // a BlockStmt

    // Filled in by semantic analysis: the index this method will occupy
    // in the module's function table, and the frame size its body needs.
    u32 functionIndex = 0;
    u32 localSlotCount = 0;

    MethodDecl() : Decl(DeclKind::Method) {}
};

struct ClassDecl : Decl
{
    std::string name;
    std::vector<DeclPtr> methods; // each a MethodDecl
    ClassDecl() : Decl(DeclKind::Class) {}
};

struct Program
{
    std::vector<DeclPtr> declarations; // each a ClassDecl
};

} // namespace Fluxion::Script
