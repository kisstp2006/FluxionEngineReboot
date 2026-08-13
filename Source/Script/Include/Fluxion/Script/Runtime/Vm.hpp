#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Bytecode.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>

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
Vm* CreateVm(const BytecodeModule& module, DiagnosticList& outDiagnostics);

void DestroyVm(Vm* vm);

// Redirects everything the script prints. Passing a null handler restores
// the default streams.
void SetOutputHandler(Vm* vm, OutputHandler handler, void* user);

// Runs a static method named as "Class.Method". The method must take no
// parameters and must not need a receiver. On success the result holds
// the method's return value (a Void value for a void method). The error
// message is a static string -- the code distinguishes the cases.
Fluxion::Foundation::Result<ScriptValue> Invoke(Vm* vm, const char* qualifiedName);

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
