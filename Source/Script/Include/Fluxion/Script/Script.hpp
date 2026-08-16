// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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
