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

// A type as it was written down. `name` is set only when the source named
// a declared type, in which case `type` is Object and `classIndex` is
// filled in once the analyzer has seen every declaration in the file.
//
// `typeArgs` holds what was written between angle brackets and
// `arrayDepth` counts the trailing `[]` pairs, so `Box<int>[][]` arrives
// here whole. Resolution replaces all three with a single concrete class:
// afterwards `type` is Object, `classIndex` names the concrete type, and
// both `typeArgs` and `arrayDepth` are empty -- there is no such thing as
// an unresolved type once the analyzer is done with it.
struct TypeRef
{
    ValueType type = ValueType::Unknown;
    std::string name;
    std::vector<TypeRef> typeArgs;
    u32 arrayDepth = 0;
    u32 classIndex = kNoClass;
};

// --- Annotations -------------------------------------------------------

// One value written inside an annotation's brackets. A number, a piece of
// text, or `typeof(Name)` -- which is written as a type and resolved into
// the class it names, so nothing downstream carries the name in text.
struct AttributeArgNode
{
    AttributeArgumentKind kind = AttributeArgumentKind::Int;

    long long intValue = 0;
    double floatValue = 0.0;
    std::string stringValue;

    TypeRef typeValue;
    u32 classIndex = kNoClass; // filled in by semantic analysis for a `typeof`

    SourceLocation location;
};

// `[Name]` or `[Name(args)]`, as written ahead of a class or a field.
struct AttributeNode
{
    std::string name;
    std::vector<AttributeArgNode> args;
    SourceLocation location;
};

// --- Expressions -------------------------------------------------------

enum class ExprKind
{
    IntLiteral,
    FloatLiteral,
    BoolLiteral,
    StringLiteral,
    NullLiteral,
    This,
    New,
    NewArray,
    Identifier,
    Member,
    Index,
    Call,
    Unary,
    Binary,
    Assign,
};

struct Expr
{
    ExprKind kind;
    SourceLocation location;

    // Filled in by semantic analysis. `resolvedClass` is meaningful only
    // when `resolvedType` is Object, and names the class or interface the
    // expression is statically known as.
    ValueType resolvedType = ValueType::Unknown;
    u32 resolvedClass = kNoClass;

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

// `null`. Its type is Null, which is assignable to a reference of any
// class and comparable with one, and to nothing else.
struct NullLiteralExpr : Expr
{
    NullLiteralExpr() : Expr(ExprKind::NullLiteral) {}
};

// `this`. Always frame slot zero, which is where the receiver of an
// instance method lands.
struct ThisExpr : Expr
{
    ThisExpr() : Expr(ExprKind::This) {}
};

// `new Name(args)`, `Name` possibly carrying type arguments.
struct NewExpr : Expr
{
    TypeRef type;
    std::vector<ExprPtr> args;

    // Filled in by semantic analysis.
    u32 classIndex = kNoClass;
    u32 constructorFunction = kNoFunction;

    NewExpr() : Expr(ExprKind::New) {}
};

// `new T[count]`. The element type is what was written before the
// brackets, so `new int[4][]` creates four empty `int[]` slots.
struct NewArrayExpr : Expr
{
    TypeRef elementType;
    ExprPtr length;

    // The sequence type this produces, filled in by semantic analysis.
    u32 arrayClass = kNoClass;

    NewArrayExpr() : Expr(ExprKind::NewArray) {}
};

struct IdentifierExpr : Expr
{
    std::string name;

    // Frame slot the name resolves to, filled in by semantic analysis.
    int localSlot = -1;

    // Set when the name is not a local at all but a field of the
    // surrounding instance, in which case it reads as if `this.` had been
    // written in front of it.
    bool isField = false;
    u32 fieldSlot = 0;

    // Set for a name the surrounding construct owns rather than the body:
    // a loop's own variable is handed a fresh value each turn, so nothing
    // inside the body may store into it.
    bool isReadOnly = false;

    IdentifierExpr() : Expr(ExprKind::Identifier) {}
};

// What `A.B` turned out to mean.
enum class MemberBinding
{
    Unresolved,

    // A field of the object `base` evaluates to.
    Field,

    // A name used purely to qualify something else -- a class holding the
    // method being called, or a built-in's group. Never a value on its
    // own, so `base` is left unanalyzed.
    Scope,

    // How many elements the sequence `base` evaluates to was created
    // with. Read-only, and the one member a sequence has.
    ArrayLength,
};

// `A.B`.
struct MemberExpr : Expr
{
    ExprPtr base;
    std::string member;

    // What was written between angle brackets after the member name. Only
    // a call may carry any: naming a type at a call site is how the type
    // itself becomes one of the call's arguments, and there is nothing
    // else for it to mean on a member that is merely read.
    std::vector<TypeRef> typeArgs;
    SourceLocation typeArgLocation;

    MemberBinding binding = MemberBinding::Unresolved;
    u32 fieldSlot = 0;

    MemberExpr() : Expr(ExprKind::Member) {}
};

// `base[index]`, both to read an element and, as an assignment target, to
// write one.
struct IndexExpr : Expr
{
    ExprPtr base;
    ExprPtr index;

    IndexExpr() : Expr(ExprKind::Index) {}
};

enum class CallTarget
{
    Unresolved,

    // A method reached without a receiver.
    ScriptMethod,

    // A method reached through a receiver and bound at compile time,
    // because neither it nor anything overriding it can be reached any
    // other way.
    InstanceMethod,

    // A method reached through a receiver and chosen at run time, from
    // the receiver's own class: by vtable slot for Virtual, by the
    // receiver's table for the named interface for Interface.
    VirtualMethod,
    InterfaceMethod,

    Native,

    // A method of an engine type the host made visible. Named by the
    // engine type and the method within it rather than by a module
    // function index, since no such function exists in the module.
    BoundMethod,
};

struct CallExpr : Expr
{
    ExprPtr callee;
    std::vector<ExprPtr> args;

    // The object the call runs on, moved here out of the callee during
    // analysis. Null for a call that needs no receiver.
    ExprPtr receiver;

    // Filled in by semantic analysis. `targetIndex` is a module function
    // index for every script call -- including the virtual and interface
    // forms, where it names the signature being called rather than the
    // body that will run -- and a NativeFunctionId for Native.
    CallTarget target = CallTarget::Unresolved;
    u32 targetIndex = 0;

    // Which engine type and which of its methods, for a call the host
    // made visible. Both kNoBoundType/kNoBoundMethod for every other kind
    // of call.
    u32 boundType = kNoBoundType;
    u32 boundMethod = kNoBoundMethod;

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
    // bool but may operate on floats or on references.
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
    ForEach,
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
    TypeRef declaredType;
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

// `foreach (T name in sequence) body`. Lowered to a counted loop: the
// sequence and the position are held in frame slots of their own, and the
// loop variable is written from the sequence at the top of every turn.
struct ForEachStmt : Stmt
{
    TypeRef declaredType;
    std::string name;
    ExprPtr sequence;
    StmtPtr body;

    // Filled in by semantic analysis.
    int loopSlot = -1;
    int indexSlot = -1;
    int sequenceSlot = -1;

    // How each turn reaches the sequence. An array answers both questions
    // with an instruction; anything else answers them with the two
    // methods it declared, and those are bound here like any other call.
    bool overArray = true;
    CallTarget countTarget = CallTarget::Unresolved;
    u32 countFunction = kNoFunction;
    CallTarget elementTarget = CallTarget::Unresolved;
    u32 elementFunction = kNoFunction;

    ForEachStmt() : Stmt(StmtKind::ForEach) {}
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

enum class DeclKind { Class, Method, Field };

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
    TypeRef type;
    std::string name;
    SourceLocation location;
};

struct FieldDecl : Decl
{
    TypeRef type;
    std::string name;
    std::vector<AttributeNode> attributes;

    // Slot within the object, counted across the whole inheritance chain
    // and filled in by semantic analysis.
    u32 fieldSlot = 0;

    FieldDecl() : Decl(DeclKind::Field) {}
};

struct MethodDecl : Decl
{
    TypeRef returnType;
    std::string name;
    std::string qualifiedName; // "Class.Method", filled in by semantic analysis
    std::vector<ParamDecl> params;
    StmtPtr body; // a BlockStmt; null for a method an interface only declares

    bool isStatic = false;
    bool isVirtual = false;
    bool isOverride = false;
    bool isConstructor = false;

    // A constructor's `: base(args)`, and where it was written.
    bool hasBaseCall = false;
    std::vector<ExprPtr> baseArgs;
    SourceLocation baseCallLocation;

    // Filled in by semantic analysis: the index this method will occupy
    // in the module's function table, the frame size its body needs,
    // which class it belongs to, which vtable slot it answers to if it is
    // dispatched on, the base constructor it chains to, and which frame
    // slots hold a reference.
    u32 functionIndex = 0;
    u32 localSlotCount = 0;
    u32 owningClass = kNoClass;
    u32 vtableSlot = kNoVtableSlot;
    u32 baseConstructorFunction = kNoFunction;
    std::vector<u8> localIsReference;

    MethodDecl() : Decl(DeclKind::Method) {}
};

struct ClassDecl : Decl
{
    std::string name;
    bool isStatic = false;
    bool isInterface = false;
    std::vector<AttributeNode> attributes;

    // The names written between angle brackets after the declared name.
    // A declaration that has any is a pattern rather than a type: it is
    // never laid out, numbered or emitted, and each distinct set of
    // arguments it is used with produces a separate declaration that is.
    std::vector<std::string> typeParams;
    std::vector<SourceLocation> typeParamLocations;

    // What followed the colon. The first entry may be a base class; the
    // analyzer decides, since the parser cannot tell a class name from an
    // interface name.
    std::vector<TypeRef> baseTypes;
    std::vector<SourceLocation> baseLocations;

    std::vector<DeclPtr> fields;  // each a FieldDecl
    std::vector<DeclPtr> methods; // each a MethodDecl, constructors included

    // A declaration the analyzer created rather than one the source
    // wrote: an indexable sequence of `arrayElementType`, which has no
    // members of its own and whose length belongs to each object.
    bool isArray = false;
    TypeRef arrayElementType;
    bool arrayElementIsReference = false;

    // Filled in by semantic analysis: everything the module's class table
    // needs.
    u32 classIndex = kNoClass;
    u32 baseClass = kNoClass;
    u32 fieldSlotCount = 0;
    std::vector<u32> fieldReferenceBits;
    std::vector<u32> vtable;
    std::vector<InterfaceImplementation> interfaceTables;
    u32 constructorFunction = kNoFunction;

    ClassDecl() : Decl(DeclKind::Class) {}
};

struct Program
{
    std::vector<DeclPtr> declarations; // each a ClassDecl
};

} // namespace Fluxion::Script
