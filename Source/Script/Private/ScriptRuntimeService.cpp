#include <Fluxion/Script/ScriptRuntimeService.hpp>

#include <string>
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

u32 ServiceObjectClass(const Vm* vm, ObjectHandle instance)
{
    return ObjectClass(vm, instance);
}

u32 ServiceClassBaseClass(const Vm* vm, u32 classIndex)
{
    return ClassBaseClass(vm, classIndex);
}

const FieldInfo* ServiceFindClassField(const Vm* vm, u32 classIndex, const char* name)
{
    return FindClassField(vm, classIndex, name);
}

bool ServiceReadInstanceField(const Vm* vm, ObjectHandle instance, const FieldInfo* field, ScriptValue* outValue)
{
    if (!field || !outValue) return false;
    return ReadInstanceField(vm, instance, *field, *outValue);
}

bool ServiceWriteInstanceField(Vm* vm, ObjectHandle instance, const FieldInfo* field, const ScriptValue* value)
{
    if (!field || !value) return false;
    return WriteInstanceField(vm, instance, *field, *value);
}

bool ServiceWriteModule(const CompiledModule* module, std::vector<u8>* outBytes)
{
    if (!module || !outBytes) return false;
    return WriteModule(*module, *outBytes);
}

bool ServiceReadModule(const u8* bytes, size_t byteCount, CompiledModule* outModule, DiagnosticList* outDiagnostics)
{
    if (!outModule || !outDiagnostics) return false;
    return ReadModule(bytes, byteCount, *outModule, *outDiagnostics);
}

bool ServiceCompileCached(const char* source, const CompileOptions* options, const CompileCacheOptions* cache,
    DiagnosticList* outDiagnostics, CompileCacheReport* outReport, CompiledModule* outModule)
{
    if (!source || !cache || !outDiagnostics || !outReport || !outModule) return false;

    const CompileOptions defaults;
    auto compiled = CompileCached(std::string(source), options ? *options : defaults, *cache, *outDiagnostics, *outReport);
    if (!compiled.IsOk()) return false;

    *outModule = std::move(compiled.Value());
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
    service.objectClass = &ServiceObjectClass;
    service.classBaseClass = &ServiceClassBaseClass;
    service.findClassField = &ServiceFindClassField;
    service.readInstanceField = &ServiceReadInstanceField;
    service.writeInstanceField = &ServiceWriteInstanceField;
    service.writeModule = &ServiceWriteModule;
    service.readModule = &ServiceReadModule;
    service.compileCached = &ServiceCompileCached;
    return service;
}

} // namespace Fluxion::Script
