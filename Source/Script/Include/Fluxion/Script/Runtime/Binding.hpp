#pragma once

#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Bytecode.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>

#include <string>
#include <vector>

namespace Fluxion::Script
{

// Turns one engine handle into the object it names. This is the single
// thing the script side cannot work out for itself: the engine owns its
// objects, so only the engine can say where the object behind a handle
// lives -- or that it no longer lives anywhere, which it reports by
// answering with null. A call made through a handle that answers null
// faults instead of reaching into whatever occupies that place now.
using HandleResolverFn = void* (*)(void* user, EngineHandle handle);

// --- The two script types that are not plain numbers -------------------

// A script `string` reaches a bound method as a null-terminated pointer,
// and leaves one the same way.
//
// THE LIFETIME RULE, which the marshalling enforces and every bound
// method must honour:
//
//   * A string passed *into* a native is valid for that call and no
//     longer. It points at storage the machine owns and may reuse, so a
//     native that keeps the text must copy it before it returns.
//
//   * A string returned *from* a native is copied into the machine's own
//     string table before the call site sees anything, so the native may
//     answer with a pointer to storage it is about to change. Answering
//     with null is answering with empty text.
using FluxionScriptString = const char*;

// A reference to an object on the script heap, travelling as the same
// pair of numbers the machine itself uses. A native holding one past the
// call it received it in must pin it, exactly as any other native holder
// of a script object must.
using FluxionScriptObject = ObjectHandle;

// The identities a reflected method declares to say "this parameter is a
// script string" / "this parameter is a script object". Offered as
// functions so a host never has to spell the token that is hashed.
inline FluxionTypeId ScriptStringTypeId() { return FLUXION_TYPE_ID_OF(FluxionScriptString); }
inline FluxionTypeId ScriptObjectTypeId() { return FLUXION_TYPE_ID_OF(FluxionScriptObject); }

// One method of an engine type, in the terms the script speaks: the
// value types it takes and gives back, and the invoker that carries the
// call across to the C side.
//
// A parameter or a result of type Handle also records which engine type
// the handle names, so a handle of one type is never accepted where a
// handle of another was asked for.
struct BoundMethod
{
    std::string name;
    bool isInstance = false;

    // Set when the method declared FLUXION_METHOD_FLAG_SCRIPT_TYPE_ARGUMENT:
    // its first parameter is not written at the call site at all. The call
    // is written with one type argument instead, and what reaches the
    // native is that type's class index -- a number the compiler already
    // knows, so nothing is ever looked up by name at run time. A method
    // shaped this way that also answers with a script object answers, as
    // far as the compiler is concerned, with that same type.
    bool takesScriptType = false;

    std::vector<ValueType> parameterTypes;
    std::vector<u32> parameterBoundTypes;

    // For a whole-number parameter the host has said takes a constant of
    // a named set: the name of that set, and empty for every ordinary
    // parameter. Nothing about the call changes -- a constant already is
    // the number it stands for, and that number is what the native
    // receives -- but the compiler will only accept a constant of that
    // very set at the call site, so a key cannot be written where a
    // button was meant, nor a bare number where either was.
    std::vector<std::string> parameterConstantSets;

    ValueType returnType = ValueType::Void;
    u32 returnBoundType = kNoBoundType;

    FluxionMethodInvokeFn invoke = nullptr;
};

// One engine type the host chose to make visible. The script may declare
// variables of this type -- they hold a handle, never the object -- and
// call the methods listed here on them.
struct BoundType
{
    std::string name;
    FluxionTypeId typeId = FLUXION_TYPE_ID_INVALID;

    // How a handle of this type becomes something an instance method can
    // run on. A type exposing only static methods needs none.
    HandleResolverFn resolve = nullptr;
    void* resolveUser = nullptr;

    std::vector<BoundMethod> methods;
};

// What a host hands to the compiler and to the machine so a script can
// reach the engine. It is passed in rather than looked up: there is no
// process-wide table here, and two machines in one process may be given
// entirely different ones.
struct BindingTable
{
    std::vector<BoundType> types;
};

// kNoBoundType / kNoBoundMethod when there is no such entry.
u32 FindBoundType(const BindingTable& table, const std::string& name);
u32 FindBoundTypeById(const BindingTable& table, FluxionTypeId id);
u32 FindBoundMethod(const BindingTable& table, u32 typeIndex, const std::string& name);

// Makes one reflected type and every method it carries visible to the
// script, under the type's own reflected name. The methods are read off
// FluxionTypeInfo::methods, so what the script can call is exactly what
// the type declared to the reflection registry and nothing else.
//
// A parameter or return type is accepted when it is one of the value
// types the language has -- i32, f32, bool -- or when it names an engine
// type this table already holds, in which case it is passed as that
// type's handle. Anything else is refused with a diagnostic naming the
// method, and the whole type is left out rather than half added.
//
// Returns the index of the added type, or kNoBoundType on failure.
u32 AddBoundType(BindingTable& table, const FluxionTypeInfo& typeInfo, HandleResolverFn resolve, void* resolveUser,
    DiagnosticList& outDiagnostics);

// Narrows one already-added parameter from "any whole number" to "a
// constant of the set called `setName`". Reflection cannot say this on
// its own: what reaches the native is an ordinary number either way, and
// which set that number has to have come from is a decision about the
// interface rather than anything about the C function -- so whoever made
// the type visible is who says it, and whoever wrote the set is who
// declares it, in the source they hand to the same compilation.
//
// Returns false, changing nothing, when there is no such type, method or
// parameter, or when the parameter is not a whole number to begin with.
bool BindParameterToConstantSet(BindingTable& table, u32 typeIndex, const std::string& methodName, u32 parameterIndex,
    const std::string& setName);

} // namespace Fluxion::Script
