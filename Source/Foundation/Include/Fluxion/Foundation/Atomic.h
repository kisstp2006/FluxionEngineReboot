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

#ifdef __cplusplus
extern "C" {
#endif

// Thin, C-ABI atomic wrappers usable identically from C and C++. Do not
// touch `value` directly — always go through the functions below, which
// perform genuine atomic operations in Atomic.c.
typedef struct FluxionAtomicI32 { i32 value; } FluxionAtomicI32;
typedef struct FluxionAtomicI64 { i64 value; } FluxionAtomicI64;

void Fluxion_AtomicI32_Store(FluxionAtomicI32* atomic, i32 value);
i32  Fluxion_AtomicI32_Load(const FluxionAtomicI32* atomic);
i32  Fluxion_AtomicI32_Increment(FluxionAtomicI32* atomic);
i32  Fluxion_AtomicI32_Decrement(FluxionAtomicI32* atomic);
bool Fluxion_AtomicI32_CompareExchange(FluxionAtomicI32* atomic, i32* expected, i32 desired);

void Fluxion_AtomicI64_Store(FluxionAtomicI64* atomic, i64 value);
i64  Fluxion_AtomicI64_Load(const FluxionAtomicI64* atomic);
i64  Fluxion_AtomicI64_Increment(FluxionAtomicI64* atomic);
i64  Fluxion_AtomicI64_Decrement(FluxionAtomicI64* atomic);
bool Fluxion_AtomicI64_CompareExchange(FluxionAtomicI64* atomic, i64* expected, i64 desired);

#ifdef __cplusplus
}
#endif
