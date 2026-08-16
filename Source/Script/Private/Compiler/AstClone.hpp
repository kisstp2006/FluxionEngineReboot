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

#include <Fluxion/Script/Compiler/Ast.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace Fluxion::Script
{

// A declaration written with type parameters is a pattern, not a type: it
// is never laid out and never emitted. Each distinct set of arguments it
// is used with gets its own copy of the whole declaration -- fields,
// method bodies and all -- with the parameter names replaced by the
// arguments, and that copy is what gets a layout, a field bitmap, tables
// and instructions of its own.
//
// Copying the tree rather than walking the original once per argument set
// is what keeps the two apart: the analyzer annotates nodes in place, so
// two argument sets sharing one tree would each overwrite the other's
// resolved types, frame slots and call targets.
std::unique_ptr<ClassDecl> CloneClassDecl(const ClassDecl& source);

// Replaces every mention of a type parameter in `target` with the type it
// stands for. Only types are rewritten -- a name used as a value is left
// alone, so a parameter used where a value belongs is reported as the
// undeclared name it is.
//
// `substitution` maps a parameter name to an already-resolved type, and a
// parameter written with `[]` after it keeps those: `T[]` under `T = int`
// becomes `int[]`.
void SubstituteTypeParameters(ClassDecl& target, const std::unordered_map<std::string, TypeRef>& substitution);

} // namespace Fluxion::Script
