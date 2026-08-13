#pragma once

#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Binding.hpp>
#include <Fluxion/Script/Runtime/CompileCache.hpp>
#include <Fluxion/Script/Runtime/ModuleSerializer.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>
#include <Fluxion/Script/Runtime/Vm.hpp>
#include <Fluxion/Script/Script.hpp>

#include <cstddef>
#include <type_traits>
#include <vector>

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

    // Two rather than one because the table grew: a module can now be
    // written out and read back, a compilation can be answered out of what
    // an earlier one produced, and a field of a live object can be reached
    // without a script-written accessor. A caller that resolved this by
    // name and found version one has none of that.
    static constexpr u32 Version = 2;

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

    // --- Reaching an object's state directly ----------------------------

    // A class lists only the fields it declared itself, so a caller after
    // everything an object holds walks the chain `classBaseClass` gives it
    // and asks each class in turn. `objectClass` is where that walk starts
    // when all the caller has is a reference.
    u32 (*objectClass)(const Vm* vm, ObjectHandle instance);
    u32 (*classBaseClass)(const Vm* vm, u32 classIndex);

    // Null when the class does not declare a field of that name. What
    // comes back is only valid for as long as the machine is.
    const FieldInfo* (*findClassField)(const Vm* vm, u32 classIndex, const char* name);

    // Both refuse a field of a value type: it occupies several slots and
    // has no single-value form.
    bool (*readInstanceField)(const Vm* vm, ObjectHandle instance, const FieldInfo* field, ScriptValue* outValue);
    bool (*writeInstanceField)(Vm* vm, ObjectHandle instance, const FieldInfo* field, const ScriptValue* value);

    // --- Keeping a compiled module -------------------------------------

    // `readModule` refuses anything it cannot fully account for rather
    // than answering with a half-built module, and says why through the
    // diagnostics.
    bool (*writeModule)(const CompiledModule* module, std::vector<u8>* outBytes);
    bool (*readModule)(const u8* bytes, size_t byteCount, CompiledModule* outModule, DiagnosticList* outDiagnostics);

    // Compiles unless an image of exactly this compilation is already on
    // disk. A cache file that is stale, truncated or not one of these at
    // all is a miss and never a failure.
    bool (*compileCached)(const char* source, const CompileOptions* options, const CompileCacheOptions* cache,
        DiagnosticList* outDiagnostics, CompileCacheReport* outReport, CompiledModule* outModule);
};

static_assert(std::is_standard_layout_v<ScriptRuntimeService>,
    "a service interface must stay standard-layout: the registry reads its header out of a bare pointer");

// A filled-in table the caller then owns. Registering it, and unregistering
// it before it goes away, is the caller's business.
ScriptRuntimeService MakeScriptRuntimeService();

} // namespace Fluxion::Script
