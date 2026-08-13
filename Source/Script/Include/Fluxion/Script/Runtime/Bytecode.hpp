#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Runtime/Value.hpp>

#include <string>
#include <vector>

namespace Fluxion::Script
{

// The instruction set is deliberately type-specialized: the language is
// statically typed, so the type of every operand is known when the code
// is emitted. `AddInt` and `AddFloat` are separate opcodes precisely so
// the interpreter loop never has to inspect an operand's type at runtime.
enum class OpCode : u16
{
    Nop,

    // Constant pushes. The operand is an index into the matching constant
    // pool, except PushBool, whose operand is the literal 0 or 1.
    PushInt,
    PushFloat,
    PushBool,
    PushString,

    // Local variable access. The operand is a slot index relative to the
    // current frame's base. Parameters occupy the lowest slots.
    LoadLocal,
    StoreLocal,

    // Discards the top of the stack (an expression evaluated purely for
    // its side effects).
    Pop,

    // Integer arithmetic.
    AddInt,
    SubInt,
    MulInt,
    DivInt,
    ModInt,
    NegInt,

    // Floating-point arithmetic.
    AddFloat,
    SubFloat,
    MulFloat,
    DivFloat,
    ModFloat,
    NegFloat,

    // Joins two strings into a newly interned one.
    ConcatString,

    // Comparisons, one family per operand type. All of them produce a
    // bool.
    EqualInt,
    NotEqualInt,
    LessInt,
    GreaterInt,
    LessEqualInt,
    GreaterEqualInt,

    EqualFloat,
    NotEqualFloat,
    LessFloat,
    GreaterFloat,
    LessEqualFloat,
    GreaterEqualFloat,

    EqualBool,
    NotEqualBool,

    EqualString,
    NotEqualString,

    // Logical negation. `&&` and `||` have no opcodes of their own -- the
    // emitter expands them into conditional jumps so the right-hand side
    // is skipped when the left already decides the answer.
    NotBool,

    // Conversions. IntToFloat is the language's only implicit numeric
    // widening; the *ToString forms back string concatenation with a
    // non-string operand.
    IntToFloat,
    IntToString,
    FloatToString,
    BoolToString,

    // Control flow. The operand is a target instruction index within the
    // enclosing function's own code range. JumpIfFalse/JumpIfTrue consume
    // the bool they test.
    Jump,
    JumpIfFalse,
    JumpIfTrue,

    // Calls. The operand is an index into the module's function table for
    // Call, and a NativeFunctionId for CallNative. Arguments are already
    // on the stack, leftmost pushed first.
    Call,
    CallNative,
    Return,
    ReturnVoid,

    // Stops execution. Emitted only as a backstop; well-formed code
    // always leaves a function through Return/ReturnVoid.
    Halt,
};

const char* OpCodeName(OpCode op);

struct Instruction
{
    OpCode op = OpCode::Halt;
    u32 operand = 0;
};

// Functions the language can call but cannot declare. Their identity is
// fixed here so the compiler and the interpreter agree on the numbering
// without any name lookup at run time.
enum class NativeFunctionId : u16
{
    ConsoleWriteLineString,
    ConsoleWriteLineInt,
    ConsoleWriteLineFloat,
    ConsoleWriteLineBool,

    ConsoleWriteString,
    ConsoleWriteInt,
    ConsoleWriteFloat,
    ConsoleWriteBool,

    DebugLog,
    DebugLogWarning,
    DebugLogError,

    Count,
};

inline constexpr u32 kNativeFunctionCount = (u32)NativeFunctionId::Count;

// Only what the compiler needs to resolve a call: which name it answers
// to and what it takes. Several entries can share a name, one per
// accepted argument type. Every built-in takes exactly one argument and
// produces no value, so the argument's type alone picks the entry. Where
// the output ends up is the interpreter's business, not the compiler's.
struct NativeFunctionSignature
{
    const char* qualifiedName;
    ValueType parameterType;
};

// Points at a table of exactly kNativeFunctionCount entries, indexed by
// NativeFunctionId.
const NativeFunctionSignature* NativeFunctionTable();

inline constexpr u8 kModuleMagic[4] = { 'F', 'L', 'X', 'S' };

// Bumped when the accepted source language changes in a way that alters
// the meaning of already-valid code.
inline constexpr u32 kLanguageVersion = 1;

// Bumped whenever the instruction set or the module layout changes. A
// module carrying any other value is refused by the loader -- running it
// would silently misinterpret opcodes.
inline constexpr u32 kBytecodeVersion = 1;

// Bumped when the engine-side interface reachable from a module changes
// shape.
inline constexpr u32 kEngineAbiVersion = 1;

struct ModuleHeader
{
    u8 magic[4] = { kModuleMagic[0], kModuleMagic[1], kModuleMagic[2], kModuleMagic[3] };
    u32 languageVersion = kLanguageVersion;
    u32 bytecodeVersion = kBytecodeVersion;
    u32 engineAbiVersion = kEngineAbiVersion;
    u32 moduleVersion = 0;
};

struct FunctionInfo
{
    std::string qualifiedName;
    ValueType returnType = ValueType::Void;
    std::vector<ValueType> parameterTypes;

    // Total frame size, parameters included: parameters occupy slots
    // [0, parameterTypes.size()), locals follow.
    u32 localSlotCount = 0;

    // This function's instructions are BytecodeModule::code[codeOffset]
    // through [codeOffset + codeLength).
    u32 codeOffset = 0;
    u32 codeLength = 0;
};

// A loadable image: everything the interpreter needs, and nothing that
// points outside itself.
struct BytecodeModule
{
    ModuleHeader header;
    std::string sourceName;

    std::vector<Instruction> code;
    std::vector<i32> intConstants;
    std::vector<f32> floatConstants;
    std::vector<std::string> stringConstants;
    std::vector<FunctionInfo> functions;
};

} // namespace Fluxion::Script
