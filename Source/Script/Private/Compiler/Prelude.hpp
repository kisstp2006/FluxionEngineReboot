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

namespace Fluxion::Script
{

// Types every program can use without declaring them, written in the
// language itself and compiled ahead of the caller's source into the same
// module. Nothing here is special-cased anywhere else: a growable list is
// a declaration with type parameters over a sequence, which is precisely
// what a program of the caller's own could have written.
//
// A declaration with type parameters costs nothing until something names
// it with arguments, so a program that never uses these carries no trace
// of them. A value type is not free in the same way -- it is laid out
// whether or not anything names it -- but it is only a handful of numbers
// and a set of methods, and having every program agree on what a position
// or a colour is worth far more than that.
const char* PreludeSource();

// The name diagnostics from the prelude are reported against. A message
// carrying it means the prelude itself failed to compile, which is a
// defect in this module rather than anything the caller did.
const char* PreludeSourceName();

} // namespace Fluxion::Script
