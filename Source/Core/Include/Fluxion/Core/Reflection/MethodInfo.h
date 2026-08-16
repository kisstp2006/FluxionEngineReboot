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
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed C signature every reflected method's invoker must match,
// regardless of what the method actually takes and returns -- the same
// arrangement FluxionPropertyInfo's accessor mode already uses for
// getters and setters.
//
// `instance` points at the object the call runs on, and is NULL for a
// static method. `args` points at `parameterCount` pointers, one per
// declared parameter in order, each addressing storage of that
// parameter's own type; it may be NULL when the method takes nothing.
// `returnValue` points at storage of the return type, and is NULL for a
// method returning nothing. The caller owns all of that storage and the
// invoker only reads and writes through it, so nothing crossing this
// boundary is ever allocated on one side and released on the other.
typedef void (*FluxionMethodInvokeFn)(void* instance, void** args, void* returnValue);

typedef u32 FluxionMethodFlags;

#define FLUXION_METHOD_FLAG_NONE   ((FluxionMethodFlags)0)
#define FLUXION_METHOD_FLAG_STATIC FLUXION_BIT(0)

// The method's first parameter is not one a caller writes: it identifies
// a type, and whoever makes the call fills it in from the type the call
// was written with. Declared here rather than guessed from the parameter
// list, since an i32 first parameter is otherwise an ordinary number.
#define FLUXION_METHOD_FLAG_SCRIPT_TYPE_ARGUMENT FLUXION_BIT(1)

typedef struct FluxionMethodInfo
{
    FluxionStringView name;

    // FLUXION_TYPE_ID_INVALID for a method that returns nothing.
    FluxionTypeId returnType;

    const FluxionTypeId* parameterTypes;
    u32 parameterCount;

    FluxionMethodFlags flags;
    FluxionMethodInvokeFn invoke;
} FluxionMethodInfo;

#ifdef __cplusplus
}
#endif
