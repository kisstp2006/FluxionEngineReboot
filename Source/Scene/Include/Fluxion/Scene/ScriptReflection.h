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
#include <Fluxion/Scene/Scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// Script components, described the same way everything else is: one
// entry in the same reflection registry, one property per field --
// serialization, editors and the call-binding runtime all read the one
// account instead of drifting apart on three.
//
// The one difference is where a value lives: a native field is an offset
// in a struct, a script field is a slot in an object of a machine this
// engine does not own, so its property carries a pair of accessor
// functions instead.
//
// A registered script class has a SIZE OF ZERO on purpose -- storage
// takes sizes from the registry, and refusing a zero-size type is what
// stops a script class being attached as a data component.

// Which script component, in the terms its accessors need: the object it
// is on, and which class it is an instance of.
//
// Pass a pointer to one of these as the `instance` argument of a property
// read or write. It names the component rather than pointing at it,
// because what a script component IS lives in the machine, not at an
// address this side can hand out.
typedef struct FluxionScriptInstance
{
    FluxionSceneHandle scene;
    FluxionGameObjectHandle object;
    u32 classIndex;
} FluxionScriptInstance;

// One script component of an object: what type it is, and how to reach it.
typedef struct FluxionScriptComponentRef
{
    // Look this up in the reflection registry to get the field list.
    FluxionTypeId type;

    // Pass a pointer to this as the instance when reading or writing one
    // of those fields.
    FluxionScriptInstance instance;
} FluxionScriptComponentRef;

// Every script component on this object. Passing a null `out` (with `max`
// zero) asks only for the count.
//
// This is the script-side twin of Fluxion_GameObject_GetComponentTypes:
// between the two, everything an object carries can be enumerated and
// described without knowing in advance what any of it is.
u32 Fluxion_GameObject_GetScriptComponents(FluxionSceneHandle scene, FluxionGameObjectHandle object,
                                           FluxionScriptComponentRef* out, u32 max);

// The id a script class is registered under, or FLUXION_TYPE_ID_INVALID
// when this scene runs no such class. Worked out from the class name, so
// a class keeps its identity across a reload and across a save.
FluxionTypeId Fluxion_Scene_ScriptClassTypeId(FluxionSceneHandle scene, u32 classIndex);

#ifdef __cplusplus
}
#endif
