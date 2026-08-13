#pragma once

#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Binding.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>
#include <Fluxion/Script/Runtime/Vm.hpp>
#include <Fluxion/Script/Script.hpp>

#include <type_traits>

namespace Fluxion::Script
{

// The scripting runtime as a service: a table of function pointers that
// says what a script can be made to do, without the caller having to link
// against this module or know anything about the types behind `Vm`.
//
// It does not register itself. Whoever owns the instance decides when it
// becomes reachable and when it stops being reachable, which matters
// because the registry stores the pointer and does not copy it.
//
// Each entry answers with a plain bool rather than a Result: a result
// carries a static message and a code, and a function pointer table is
// the wrong place to spend that -- what went wrong in detail is what the
// diagnostics and the fault detail are for.
struct ScriptRuntimeService
{
    FluxionServiceHeader header;

    static constexpr const char* Name = "ScriptRuntimeService";
    static constexpr u32 Version = 1;

    // Compiles `source` into `outModule`. `options` may be null, in which
    // case the defaults apply and the source reaches nothing outside
    // itself.
    bool (*compile)(const char* source, const CompileOptions* options, DiagnosticList* outDiagnostics, CompiledModule* outModule);

    // Loads a module, resolving every call it makes into the engine
    // against `bindings`. Answers with null when the module cannot be
    // loaded, saying why in `outDiagnostics`.
    Vm* (*createVm)(const CompiledModule* module, const BindingTable* bindings, DiagnosticList* outDiagnostics);

    void (*destroyVm)(Vm* vm);

    // Runs a static method that takes nothing, named as "Class.Method".
    bool (*invoke)(Vm* vm, const char* qualifiedName, ScriptValue* outValue);

    // Resolved once and kept: neither of these is meant to be on the path
    // a caller walks every frame.
    u32 (*findClass)(const Vm* vm, const char* name);
    u32 (*findMethod)(const Vm* vm, u32 classIndex, const char* name);

    bool (*newInstance)(Vm* vm, u32 classIndex, const ScriptValue* args, u32 argCount, ObjectHandle* outInstance);
    bool (*invokeMethod)(Vm* vm, ObjectHandle instance, u32 methodIndex, const ScriptValue* args, u32 argCount,
        ScriptValue* outValue);
};

static_assert(std::is_standard_layout_v<ScriptRuntimeService>,
    "a service interface must stay standard-layout: the registry reads its header out of a bare pointer");

// A filled-in table the caller then owns. Registering it, and unregistering
// it before it goes away, is the caller's business.
ScriptRuntimeService MakeScriptRuntimeService();

} // namespace Fluxion::Script
