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
    // pool, except PushBool, whose operand is the literal 0 or 1, and
    // PushNull, which has no operand.
    PushInt,
    PushFloat,
    PushBool,
    PushString,
    PushNull,

    // Local variable access. The operand is a slot index relative to the
    // current frame's base. Parameters occupy the lowest slots, preceded
    // by the receiver in a method that has one.
    LoadLocal,
    StoreLocal,

    // Discards the top of the stack (an expression evaluated purely for
    // its side effects).
    Pop,

    // Copies the top of the stack. Used where one produced value has to
    // be consumed twice, such as a freshly allocated object that is both
    // the receiver of its constructor and the result of the expression.
    Dup,

    // Copies the top two slots as a pair, leaving `a b` as `a b a b`. An
    // element access names its container and its position together, so a
    // store that also has to produce the stored value needs both of them
    // twice.
    Dup2,

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

    // Engine handle comparison. Two handles are the same when they name
    // the same engine object, which is index and generation together --
    // an index reused after its previous occupant went away compares
    // unequal to a handle taken before that happened.
    EqualHandle,
    NotEqualHandle,

    // Reference comparison, which is identity: two references are equal
    // when they name the same object, never when they merely hold equal
    // field values.
    EqualRef,
    NotEqualRef,

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

    // Object handling. NewObject's operand is a class index; its result
    // has every field zeroed, which is already the declared default for
    // each field type. LoadField/StoreField take a field slot within the
    // object, counted from the base of the inheritance chain so a field
    // sits at the same slot whichever class the reference is seen as.
    NewObject,
    LoadField,
    StoreField,

    // Element storage. NewArray's operand is the class index of the
    // sequence type being created; the element count is popped off the
    // stack, and a negative one is a reported fault rather than a
    // wrapped-around allocation. Elements start out holding the zero of
    // the element type, exactly as fields do.
    //
    // LoadElement and StoreElement take their position from the stack
    // rather than from the instruction, since it is a computed value.
    // Both check it against the length that was allocated: a position
    // outside the sequence stops execution with a fault, and never reads
    // or writes anything.
    NewArray,
    ArrayLength,
    LoadElement,
    StoreElement,

    // Control flow. The operand is a target instruction index within the
    // enclosing function's own code range. JumpIfFalse/JumpIfTrue consume
    // the bool they test.
    Jump,
    JumpIfFalse,
    JumpIfTrue,

    // Calls. The operand is an index into the module's function table for
    // Call, and a NativeFunctionId for CallNative. Arguments are already
    // on the stack, leftmost pushed first, preceded by the receiver when
    // the callee takes one.
    //
    // CallVirtual and CallInterface also name a function, but only to say
    // which signature is being called: the function actually entered is
    // the one the receiver's own class puts in that vtable slot, or in
    // its table for the interface the named method belongs to.
    Call,
    CallVirtual,
    CallInterface,
    CallNative,

    // A method of an engine type the host made visible to the script. The
    // operand is an index into the module's own list of such call sites,
    // which names the type and the method in text -- the module never
    // holds a pointer into the host, so the machine that runs it resolves
    // each site against the table it was given.
    CallBound,

    Return,
    ReturnVoid,

    // A point where the operand stack is known to be empty, so a pending
    // collection can run without having to interpret anything the current
    // expression left behind. Emitted between statements.
    SafePoint,

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

    GcCollect,
    GcLiveObjects,

    Count,
};

inline constexpr u32 kNativeFunctionCount = (u32)NativeFunctionId::Count;

// Only what the compiler needs to resolve a call: which name it answers
// to, what it takes and what it gives back. Several entries can share a
// name, one per accepted argument list, and the whole list is what picks
// between them. `parameterTypes` points at `parameterCount` entries and
// is null when the built-in takes nothing. Where the output ends up is
// the interpreter's business, not the compiler's.
struct NativeFunctionSignature
{
    const char* qualifiedName;
    const ValueType* parameterTypes;
    u32 parameterCount;
    ValueType returnType;
};

// Points at a table of exactly kNativeFunctionCount entries, indexed by
// NativeFunctionId.
const NativeFunctionSignature* NativeFunctionTable();

// Stand-ins for "there is none", used wherever a class, function or
// vtable slot is optional.
inline constexpr u32 kNoClass = 0xFFFFFFFFu;
inline constexpr u32 kNoFunction = 0xFFFFFFFFu;
inline constexpr u32 kNoVtableSlot = 0xFFFFFFFFu;
inline constexpr u32 kNoBoundType = 0xFFFFFFFFu;
inline constexpr u32 kNoBoundMethod = 0xFFFFFFFFu;

// One place in the instruction stream where the script calls into the
// engine. What is stored is the pair of names, not the invoker: a module
// stays a self-contained image, so the machine that loads it looks each
// site up in whichever table it was created with, and refuses to load one
// naming something that table does not have.
struct BoundCallSite
{
    std::string typeName;
    std::string methodName;

    // The receiver is on the stack ahead of the arguments for an instance
    // method, exactly as for a script method, and absent for a static one.
    bool isInstance = false;

    std::vector<ValueType> parameterTypes;
    ValueType returnType = ValueType::Void;
};

// Reference bitmaps are packed into 32-bit words, one bit per slot, and
// are what makes collection precise: they say exactly which frame slots
// and which object fields hold a reference, so nothing else is ever
// mistaken for one.
inline bool IsReferenceBitSet(const std::vector<u32>& words, u32 index)
{
    const u32 word = index >> 5;
    if (word >= words.size()) return false;
    return ((words[word] >> (index & 31u)) & 1u) != 0u;
}

inline void SetReferenceBit(std::vector<u32>& words, u32 index)
{
    const u32 word = index >> 5;
    if (words.size() <= (size_t)word) words.resize((size_t)word + 1, 0u);
    words[word] |= (1u << (index & 31u));
}

inline constexpr u8 kModuleMagic[4] = { 'F', 'L', 'X', 'S' };

// Bumped when the accepted source language changes in a way that alters
// the meaning of already-valid code.
inline constexpr u32 kLanguageVersion = 3;

// Bumped whenever the instruction set or the module layout changes. A
// module carrying any other value is refused by the loader -- running it
// would silently misinterpret opcodes.
inline constexpr u32 kBytecodeVersion = 4;

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

// How a class satisfies one interface: for each slot of that interface's
// own method numbering, the function this class runs. Built per class, so
// a derived class that overrides an implementing method already points at
// the override.
struct InterfaceImplementation
{
    u32 interfaceClass = kNoClass;
    std::vector<u32> methods;
};

struct ClassInfo
{
    std::string name;

    // At most one base class; kNoClass for a root class, an interface, or
    // a class holding only static members.
    u32 baseClass = kNoClass;
    bool isInterface = false;
    bool isStatic = false;

    // An indexable sequence of one element type rather than a set of
    // named fields. Its objects each carry their own element count, so
    // `fieldSlotCount` says nothing about them; `elementIsReference` is
    // what a collection reads to decide whether the whole length has to
    // be followed or none of it does.
    bool isArray = false;
    bool elementIsReference = false;

    // Fields of the whole inheritance chain: a base class's fields keep
    // the slots they had, and a derived class's own fields follow them.
    u32 fieldSlotCount = 0;
    std::vector<u32> fieldReferenceBits;

    // Virtual dispatch table. A slot is introduced by the class that
    // first declares the method `virtual` and keeps its number in every
    // derived class, where an `override` replaces the entry in place.
    std::vector<u32> vtable;

    std::vector<InterfaceImplementation> interfaces;

    u32 constructorFunction = kNoFunction;
};

struct FunctionInfo
{
    std::string qualifiedName;

    // Which source this method's body was written in. A method's body is
    // written in one file whole, so this belongs to the function rather
    // than to each instruction; the line does not, and lives beside the
    // code instead.
    std::string sourceFile;

    ValueType returnType = ValueType::Void;
    std::vector<ValueType> parameterTypes;

    // Total frame size, receiver and parameters included: an instance
    // method's receiver is slot 0 and its parameters follow, a static
    // method's parameters start at slot 0, and locals come after either.
    u32 localSlotCount = 0;

    // This function's instructions are BytecodeModule::code[codeOffset]
    // through [codeOffset + codeLength). A method declared by an
    // interface has none: it exists only to name a signature that a call
    // site dispatches on.
    u32 codeOffset = 0;
    u32 codeLength = 0;
    bool hasBody = true;

    u32 owningClass = kNoClass;
    u32 vtableSlot = kNoVtableSlot;
    bool isInstance = false;

    // Which frame slots hold a reference, so a collection can find every
    // root of an active call without inspecting anything else.
    std::vector<u32> localReferenceBits;
};

// A loadable image: everything the interpreter needs, and nothing that
// points outside itself.
struct BytecodeModule
{
    ModuleHeader header;
    std::string sourceName;

    std::vector<Instruction> code;

    // The line each instruction came from, one entry per instruction. Kept
    // beside the code rather than inside it so an Instruction stays two
    // words: nothing running ever reads this, only something reporting
    // where execution had got to.
    std::vector<u32> sourceLines;

    std::vector<i32> intConstants;
    std::vector<f32> floatConstants;
    std::vector<std::string> stringConstants;
    std::vector<ClassInfo> classes;
    std::vector<FunctionInfo> functions;
    std::vector<BoundCallSite> boundCalls;
};

} // namespace Fluxion::Script
