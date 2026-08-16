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

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Not thread-safe yet. Callers own the FluxionTypeInfo they register: the
// registry only stores the pointer, it does not copy or take ownership.
void Fluxion_Reflection_Init(void);
void Fluxion_Reflection_Shutdown(void);

// Whether the registry has been brought up. Everything below it requires
// that; this is how a caller who can do something other than fail asks
// first, rather than finding out by tripping the assert inside.
bool Fluxion_Reflection_IsInitialized(void);

bool Fluxion_Reflection_RegisterType(const FluxionTypeInfo* typeInfo);

// Takes a type back out again. False when nothing was registered under
// that id.
//
// Necessary because the registry stores the pointer it was given rather
// than a copy: a descriptor built for something with a lifetime -- a
// script module, a loaded plugin -- has to leave when that does, or the
// next walk of the registry follows a pointer to storage that is gone.
bool Fluxion_Reflection_UnregisterType(FluxionTypeId id);
const FluxionTypeInfo* Fluxion_Reflection_FindTypeById(FluxionTypeId id);
const FluxionTypeInfo* Fluxion_Reflection_FindTypeByName(FluxionStringView name);

#ifdef __cplusplus
}
#endif
