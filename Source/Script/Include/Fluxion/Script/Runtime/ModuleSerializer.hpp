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

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Bytecode.hpp>

#include <cstddef>
#include <vector>

namespace Fluxion::Script
{

// Turning a compiled module into bytes and back. The versioned header
// (signature + language, instruction-set and engine-interface versions)
// goes first, so a reader can refuse before allocating anything. The
// reader trusts nothing: every length and index is checked against how
// many bytes actually arrived, and anything unaccountable refuses the
// whole image with a reason -- rather than handing the interpreter
// something half-formed to discover the hard way.

// Appends the module's image to `outBytes`, which is cleared first. Fails
// only when some part of the module is larger than the format can name,
// which takes a module with more than four thousand million of something.
bool WriteModule(const BytecodeModule& module, std::vector<u8>& outBytes);

// Reads one back. `outModule` is left as it was when this refuses, and
// `outDiagnostics` says what was wrong with the image. The whole of
// `byteCount` has to be one module: bytes left over at the end are as much
// a refusal as bytes missing from the middle, since either means this is
// not the image it claims to be.
bool ReadModule(const u8* bytes, size_t byteCount, BytecodeModule& outModule, DiagnosticList& outDiagnostics);

} // namespace Fluxion::Script
