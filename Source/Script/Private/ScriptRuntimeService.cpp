#include <Fluxion/Script/ScriptRuntimeService.hpp>

#include <utility>

namespace Fluxion::Script
{

namespace
{

// Each of these is the whole body of one entry in the table: the service
// exists to make the module reachable through function pointers, so
// nothing here does anything the direct interface does not already do.
// The diagnostics a caller cares about are the ones it passed in.

bool ServiceCompile(const char* source, const CompileOptions* options, DiagnosticList* outDiagnostics, CompiledModule* outModule)
{
    if (!source || !outDiagnostics || !outModule) return false;

    const CompileOptions defaults;
    auto compiled = Compile(std::string(source), options ? *options : defaults, *outDiagnostics);
    if (!compiled.IsOk()) return false;

    *outModule = std::move(compiled.Value());
    return true;
}

Vm* ServiceCreateVm(const CompiledModule* module, const BindingTable* bindings, DiagnosticList* outDiagnostics)
{
    if (!module || !outDiagnostics) return nullptr;
    return CreateVm(*module, *outDiagnostics, bindings);
}

void ServiceDestroyVm(Vm* vm)
{
    DestroyVm(vm);
}

bool ServiceInvoke(Vm* vm, const char* qualifiedName, ScriptValue* outValue)
{
    auto result = Invoke(vm, qualifiedName);
    if (!result.IsOk()) return false;
    if (outValue) *outValue = result.Value();
    return true;
}

u32 ServiceFindClass(const Vm* vm, const char* name)
{
    return FindClass(vm, name);
}

u32 ServiceFindMethod(const Vm* vm, u32 classIndex, const char* name)
{
    return FindMethod(vm, classIndex, name);
}

bool ServiceNewInstance(Vm* vm, u32 classIndex, const ScriptValue* args, u32 argCount, ObjectHandle* outInstance)
{
    auto result = NewInstance(vm, classIndex, args, argCount);
    if (!result.IsOk()) return false;
    if (outInstance) *outInstance = result.Value();
    return true;
}

bool ServiceInvokeMethod(Vm* vm, ObjectHandle instance, u32 methodIndex, const ScriptValue* args, u32 argCount,
    ScriptValue* outValue)
{
    auto result = InvokeMethod(vm, instance, methodIndex, args, argCount);
    if (!result.IsOk()) return false;
    if (outValue) *outValue = result.Value();
    return true;
}

} // namespace

ScriptRuntimeService MakeScriptRuntimeService()
{
    // The header is left as it is: RegisterService fills it in from the
    // type's own name, version and size, and doing it here as well would
    // put the same fact in two places.
    ScriptRuntimeService service{};
    service.compile = &ServiceCompile;
    service.createVm = &ServiceCreateVm;
    service.destroyVm = &ServiceDestroyVm;
    service.invoke = &ServiceInvoke;
    service.findClass = &ServiceFindClass;
    service.findMethod = &ServiceFindMethod;
    service.newInstance = &ServiceNewInstance;
    service.invokeMethod = &ServiceInvokeMethod;
    return service;
}

} // namespace Fluxion::Script
