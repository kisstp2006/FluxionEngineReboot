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
//
// `Object` covers every reference: an instance of a declared class and a
// value seen through an interface are both references, and the class the
// compiler statically knows about travels beside the type rather than
// inside it. `Null` is the type of the `null` literal alone -- it is
// assignable to any reference but no variable is ever declared with it.
//
// `Handle` names something the engine owns rather than something the
// script heap does. It is a value, not a reference: it is never followed
// by a collection, never compared with `null`, and the object it names
// goes away when the engine says so and not when the script stops
// mentioning it.
//
// `Struct` is the one type whose values are not confined to a single
// slot: a declared value type occupies as many slots as its fields need,
// and which declaration is meant travels beside the type exactly as it
// does for a reference. `Enum` is a named set of whole numbers, one slot
// wide, and likewise carries the declaration it belongs to -- which is
// what keeps two differently named sets from being mistaken for each
// other or for a plain `int`.
enum class ValueType
{
    Void,
    Bool,
    Int,
    Float,
    String,
    Object,
    Null,
    Handle,
    Struct,
    Enum,
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
        case ValueType::Object: return "a reference";
        case ValueType::Null: return "null";
        case ValueType::Handle: return "an engine handle";
        case ValueType::Struct: return "a value type";
        case ValueType::Enum: return "a named constant";
        default: return "<unknown>";
    }
}

// One value slot on the virtual machine's stack, and one local variable.
// Fixed at eight bytes so the compiler can compute every frame's size
// statically: `int`, `float`, `bool`, a named constant, a string handle
// and an object reference each occupy exactly one slot. A declared value
// type occupies one slot per field, laid out in declaration order and
// always moved whole, so its width is equally a compile-time number. The
// slot itself is untyped -- the opcode that reads it already knows what
// it holds, so there is no runtime tag to check.
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

// How the language names an object: never an address. `index` picks a
// record out of the machine's object table and `generation` says which
// occupant of that record is meant, so a reference kept past the point
// its object was reclaimed is detected on use instead of reading whatever
// moved in afterwards.
//
// Index zero is reserved and never handed out, which makes an all-zero
// slot the null reference -- a freshly zeroed frame slot or object field
// therefore already reads as null with no initialization code.
struct ObjectHandle
{
    u32 index = 0;
    u32 generation = 0;

    bool IsNull() const { return index == 0; }
};

inline bool operator==(ObjectHandle a, ObjectHandle b) { return a.index == b.index && a.generation == b.generation; }
inline bool operator!=(ObjectHandle a, ObjectHandle b) { return !(a == b); }

inline Slot MakeObjectSlot(ObjectHandle handle)
{
    Slot slot;
    slot.bits = ((u64)handle.generation << 32) | (u64)handle.index;
    return slot;
}

inline ObjectHandle SlotAsObject(Slot slot)
{
    ObjectHandle handle;
    handle.index = (u32)(slot.bits & 0xFFFFFFFFull);
    handle.generation = (u32)(slot.bits >> 32);
    return handle;
}

inline Slot MakeNullSlot() { return Slot{}; }
inline bool SlotIsNull(Slot slot) { return slot.bits == 0; }

// How the engine names one of its own objects, in the shape every engine
// handle has. It occupies one slot exactly as a reference does, but it is
// deliberately not one: no reference bitmap ever marks a slot holding one
// of these, so a collection neither follows it nor keeps anything alive
// on account of it. The engine decides when the thing behind it stops
// existing, and a handle whose generation no longer matches is refused
// when it is used rather than quietly resolving to whatever took its
// place.
struct EngineHandle
{
    u32 index = 0;
    u32 generation = 0;
};

inline bool operator==(EngineHandle a, EngineHandle b) { return a.index == b.index && a.generation == b.generation; }
inline bool operator!=(EngineHandle a, EngineHandle b) { return !(a == b); }

inline Slot MakeHandleSlot(EngineHandle handle)
{
    Slot slot;
    slot.bits = ((u64)handle.generation << 32) | (u64)handle.index;
    return slot;
}

inline EngineHandle SlotAsHandle(Slot slot)
{
    EngineHandle handle;
    handle.index = (u32)(slot.bits & 0xFFFFFFFFull);
    handle.generation = (u32)(slot.bits >> 32);
    return handle;
}

// The host-facing form of a value: self-contained (a string carries its
// own text rather than an index into some virtual machine's table), so it
// stays valid after the machine that produced it is destroyed. A
// reference is the one exception it cannot make self-contained -- the
// object it names belongs to a particular machine, so what travels is the
// handle.
struct ScriptValue
{
    ValueType type = ValueType::Void;
    i32 intValue = 0;
    f32 floatValue = 0.0f;
    bool boolValue = false;
    std::string stringValue;
    ObjectHandle objectValue;
    EngineHandle handleValue;
};

} // namespace Fluxion::Script
