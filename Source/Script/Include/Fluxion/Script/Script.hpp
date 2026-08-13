#pragma once

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Diagnostics.hpp>
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
