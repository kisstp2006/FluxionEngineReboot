#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Binding.hpp>
#include <Fluxion/Script/Runtime/Bytecode.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>
#include <Fluxion/Script/Runtime/Vm.hpp>

#include <string>

namespace Fluxion::Script
{

struct CompileOptions
{
    std::string fileName = "<source>";

    // The caller's own revision number for this module; stamped into the
    // header so a host can tell two builds of the same source apart.
    u32 moduleVersion = 1;

    // The engine types this source may reach. Passed in rather than
    // looked up anywhere: nothing in this module holds a table of its
    // own, so what a compilation can see is exactly what its caller
    // decided to show it. Null means the source can reach nothing outside
    // itself. The table must outlive the call, not the module it
    // produces -- a compiled module names what it calls in text.
    const BindingTable* bindings = nullptr;

    // Source the host wants compiled ahead of the caller's own, after
    // the language's own prelude and against the same table. This is
    // where declarations that only make sense once particular engine
    // types are visible belong: nothing in this module names them, so
    // whoever made those types visible is also who writes what builds on
    // them. Empty means there is none.
    //
    // It is lexed under a file name of its own, so a message about it
    // names the host's prelude rather than either of the other two
    // sources in the same compilation.
    std::string hostPrelude;
    std::string hostPreludeName = "<host prelude>";
};

// What Compile produces and what CreateVm consumes. The compiler has
// nothing to add to the image itself, so this names the loadable module
// rather than wrapping it.
using CompiledModule = BytecodeModule;

// Runs the whole pipeline: lex, parse, analyze, emit. `outDiagnostics`
// receives every error and warning produced along the way, including on
// success. The returned Result reports only the pass/fail outcome and
// which step stopped it, because Fluxion::Foundation::Result<T>'s
// message must be a static string -- the per-error detail always lives in
// `outDiagnostics`.
Fluxion::Foundation::Result<CompiledModule> Compile(const std::string& source, const CompileOptions& options, DiagnosticList& outDiagnostics);

} // namespace Fluxion::Script
