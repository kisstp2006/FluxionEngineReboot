#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Compiler/Ast.hpp>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Binding.hpp>
#include <Fluxion/Script/Runtime/Bytecode.hpp>

#include <string>

namespace Fluxion::Script
{

// Lowers an analyzed program to a loadable module image. Expects Analyze
// to have succeeded: the annotations it left behind (resolved types,
// frame slots, call targets) are read here without being re-derived.
//
// `moduleVersion` is the caller's own revision number for the module and
// is stamped into the header alongside the language, bytecode and engine
// interface versions this build was compiled against.
// `bindings` is the same table the analysis was given: a call into the
// engine is written into the module as the pair of names it resolved to,
// which is what keeps the image free of anything pointing at the host.
BytecodeModule Emit(const Program& program, const std::string& sourceName, u32 moduleVersion, DiagnosticList& diagnostics,
    const BindingTable* bindings = nullptr);

} // namespace Fluxion::Script
