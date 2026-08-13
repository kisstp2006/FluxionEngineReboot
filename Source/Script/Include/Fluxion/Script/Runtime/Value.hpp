#pragma once

#include <Fluxion/Foundation/Types.h>

#include <bit>
#include <string>

namespace Fluxion::Script
{

// The complete set of value types the language has. Every expression
// resolves to exactly one of these; `Unknown` is a compiler-internal
// marker for an expression whose type could not be determined (an error
// was already reported for it) and never reaches a compiled module.
enum class ValueType
{
    Void,
    Bool,
    Int,
    Float,
    String,
    Unknown,
};

inline const char* ValueTypeName(ValueType type)
{
    switch (type)
    {
        case ValueType::Void: return "void";
        case ValueType::Bool: return "bool";
        case ValueType::Int: return "int";
        case ValueType::Float: return "float";
        case ValueType::String: return "string";
        default: return "<unknown>";
    }
}

// One value slot on the virtual machine's stack, and one local variable.
// Fixed at eight bytes so the compiler can compute every frame's size
// statically: `int`, `float`, `bool` and a string handle each occupy
// exactly one slot. The slot itself is untyped -- the opcode that reads
// it already knows what it holds, so there is no runtime tag to check.
struct Slot
{
    u64 bits = 0;
};

static_assert(sizeof(Slot) == 8, "Fluxion: a script value slot must be exactly 8 bytes");

inline Slot MakeIntSlot(i32 value) { Slot slot; slot.bits = (u64)(u32)value; return slot; }
inline i32 SlotAsInt(Slot slot) { return (i32)(u32)slot.bits; }

inline Slot MakeFloatSlot(f32 value) { Slot slot; slot.bits = (u64)std::bit_cast<u32>(value); return slot; }
inline f32 SlotAsFloat(Slot slot) { return std::bit_cast<f32>((u32)slot.bits); }

inline Slot MakeBoolSlot(bool value) { Slot slot; slot.bits = value ? 1u : 0u; return slot; }
inline bool SlotAsBool(Slot slot) { return slot.bits != 0; }

// A string slot holds an index into the virtual machine's string table,
// never a pointer -- slots are copied around freely and must stay
// position-independent.
inline Slot MakeStringSlot(u32 stringId) { Slot slot; slot.bits = stringId; return slot; }
inline u32 SlotAsStringId(Slot slot) { return (u32)slot.bits; }

// The host-facing form of a value: self-contained (a string carries its
// own text rather than an index into some virtual machine's table), so it
// stays valid after the machine that produced it is destroyed.
struct ScriptValue
{
    ValueType type = ValueType::Void;
    i32 intValue = 0;
    f32 floatValue = 0.0f;
    bool boolValue = false;
    std::string stringValue;
};

} // namespace Fluxion::Script
