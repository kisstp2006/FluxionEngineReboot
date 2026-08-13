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

    std::vector<ValueType> parameterTypes;
    std::vector<u32> parameterBoundTypes;

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

} // namespace Fluxion::Script
