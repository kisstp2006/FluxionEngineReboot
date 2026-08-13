#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Binding.hpp>
#include <Fluxion/Script/Runtime/Bytecode.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>

#include <string>
#include <vector>

namespace Fluxion::Script
{

// Which stream a piece of script output belongs to. The host decides what
// to do with each; the default handler sends Console to standard output,
// LogInfo/LogWarning/LogError to the diagnostic streams.
enum class OutputChannel
{
    Console,
    LogInfo,
    LogWarning,
    LogError,
};

// `text` is null-terminated and valid only for the duration of the call.
// A line-oriented write already has its terminating newline included, so
// a handler never has to guess whether to add one.
using OutputHandler = void (*)(void* user, OutputChannel channel, const char* text);

// Opaque: the interpreter's internals are not part of the module's public
// surface.
struct Vm;

// Validates the module header and takes a private copy of the image, so
// the caller's module may be destroyed immediately afterwards. Returns
// null and records a diagnostic if the magic or the bytecode version does
// not match what this build understands -- a module that fails validation
// is never executed. The returned machine must be released with
// DestroyVm.
//
// `bindings` names the engine types this machine may call into. A module
// records each such call as a pair of names, so the table handed here is
// what those names are resolved against: a module naming something this
// table does not have is refused rather than loaded and faulted on
// later. The table and everything it points at must outlive the machine.
Vm* CreateVm(const BytecodeModule& module, DiagnosticList& outDiagnostics, const BindingTable* bindings = nullptr);

void DestroyVm(Vm* vm);

// Redirects everything the script prints. Passing a null handler restores
// the default streams.
void SetOutputHandler(Vm* vm, OutputHandler handler, void* user);

// Runs a static method named as "Class.Method". The method must take no
// parameters and must not need a receiver. On success the result holds
// the method's return value (a Void value for a void method). The error
// message is a static string -- the code distinguishes the cases.
Fluxion::Foundation::Result<ScriptValue> Invoke(Vm* vm, const char* qualifiedName);

// --- Reaching a class and its methods without a name lookup ------------

// Everything below exists so the per-frame work of driving script objects
// costs no string comparison: a caller resolves a class and a method once
// and keeps the indices, then creates instances and calls methods with
// nothing but those.

// kNoClass when the module declares no such class.
u32 FindClass(const Vm* vm, const char* name);

// The module function index of `name` as `classIndex` sees it, its base
// classes included, or kNoFunction when there is no such method. A method
// that is dispatched on resolves to the signature being called; which body
// runs is still decided by the instance it is called on.
u32 FindMethod(const Vm* vm, u32 classIndex, const char* name);

const char* ClassName(const Vm* vm, u32 classIndex);
const char* MethodName(const Vm* vm, u32 methodIndex);

// True when `classIndex` is `baseIndex` or has it somewhere in the chain
// of classes it was declared on top of.
bool ClassDerivesFrom(const Vm* vm, u32 classIndex, u32 baseIndex);

// What a method was written to take, so a caller that found one by name
// can establish it has the shape it meant to call before calling it.
// Void for a parameter index the method does not have.
u32 MethodParameterCount(const Vm* vm, u32 methodIndex);
ValueType MethodParameterType(const Vm* vm, u32 methodIndex, u32 parameterIndex);

// --- Reading back what a declaration said about itself ------------------

// Everything written in brackets ahead of a class or one of its fields is
// carried in the module and readable here. None of it is consulted while
// code runs: it exists so a host can act on it -- refusing to attach
// something whose stated requirement is missing, laying out an editing
// surface, and so on.

u32 ClassAttributeCount(const Vm* vm, u32 classIndex);
const Attribute* ClassAttributeAt(const Vm* vm, u32 classIndex, u32 attributeIndex);

// The first annotation of that name on the class, or null. Only the class
// itself is looked at; a base class's annotations are read off the base.
const Attribute* FindClassAttribute(const Vm* vm, u32 classIndex, const char* name);

// The fields the class declared itself, in declaration order.
u32 ClassFieldCount(const Vm* vm, u32 classIndex);
const FieldInfo* ClassFieldAt(const Vm* vm, u32 classIndex, u32 fieldIndex);
const FieldInfo* FindClassField(const Vm* vm, u32 classIndex, const char* name);
const Attribute* FindFieldAttribute(const Vm* vm, u32 classIndex, const char* fieldName, const char* attributeName);

// Creates an instance of `classIndex` and runs its constructor with
// `argCount` arguments. The object is not held by anything afterwards, so
// a caller that means to keep it beyond the next collection must pin it.
Fluxion::Foundation::Result<ObjectHandle> NewInstance(Vm* vm, u32 classIndex, const ScriptValue* args, u32 argCount);

// Runs `methodIndex` on `instance`. The method is entered by the same
// dispatch a script call would use, so an override on the instance's own
// class is what runs.
Fluxion::Foundation::Result<ScriptValue> InvokeMethod(Vm* vm, ObjectHandle instance, u32 methodIndex,
    const ScriptValue* args, u32 argCount);

// Runs a static method, named by index rather than by text.
Fluxion::Foundation::Result<ScriptValue> InvokeStatic(Vm* vm, u32 methodIndex, const ScriptValue* args, u32 argCount);

// --- What a fault leaves behind ----------------------------------------

// One active call at the moment execution stopped.
struct FaultFrame
{
    std::string method;
    std::string file;
    u32 line = 0;
};

// The whole story of the last fault, which the Result cannot carry: its
// message has to be a static string, so the detail lives here and is
// asked for separately -- the same split the compiler makes between a
// pass/fail Result and a DiagnosticList.
//
// `frames` runs innermost first: the method that faulted, then its
// caller, and so on out to the one that was entered from outside.
struct FaultDetail
{
    bool faulted = false;
    i32 code = 0;
    const char* message = nullptr;
    std::vector<FaultFrame> frames;
};

// The detail of the most recent fault on this machine. `faulted` is false
// when nothing has faulted yet, or when the last thing that ran finished
// normally.
const FaultDetail& GetFaultDetail(const Vm* vm);

// What the object heap is currently holding. `totalAllocations` counts
// every object ever created by this machine and never goes down, so the
// gap between it and `liveObjects` is what a collection reclaimed.
struct HeapStats
{
    u32 liveObjects = 0;
    u64 totalAllocations = 0;
    u32 collectionCount = 0;
};

HeapStats GetHeapStats(const Vm* vm);

// Runs a collection now when nothing is executing. Called while a script
// is running it instead requests one, which the next point with an empty
// operand stack carries out -- collecting in the middle of an expression
// would mean having to interpret the values that expression left on the
// stack, and the whole design exists to avoid that.
void CollectGarbage(Vm* vm);

// True while `handle` still names the object it was taken from. A handle
// to a reclaimed object reports false rather than reading whatever
// occupies that record now.
bool IsObjectAlive(const Vm* vm, ObjectHandle handle);

// Keeps an object reachable for as long as native code holds onto it,
// independently of anything the script can see. Pins nest: an object
// pinned twice needs unpinning twice. Returns false if the handle does
// not name a live object.
bool PinObject(Vm* vm, ObjectHandle handle);
void UnpinObject(Vm* vm, ObjectHandle handle);

} // namespace Fluxion::Script
