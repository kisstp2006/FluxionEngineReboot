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

#include <Fluxion/Scene/Scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// A thing that can be put into a scene more than once.
//
// A prefab is a scene written down: an object and everything below it,
// with their components. Nothing about it is a special kind of storage --
// it is the same bytes a saved scene is made of, which is why a prefab
// survives whatever a scene survives and needs no format of its own.
//
// What a copy remembers is the one thing that cannot be worked out again:
// which prefab it came from, and which of that prefab's objects each of
// its own objects corresponds to. Everything else about the relationship
// -- which values were changed since, what to put back, what to push
// across -- is worked out by comparing the two, at the moment it is
// asked.
//
// That is not a shortcut. Recording changes as they happen is the obvious
// alternative and it cannot be done here: a component is written through
// a plain pointer into the storage, so there is no moment to record.

typedef struct FluxionPrefab FluxionPrefab;

// Takes a copy of `root` and everything below it.
//
// The scene it came from is not touched and is not remembered: the prefab
// holds its own copy, and destroying that scene afterwards is fine.
FluxionPrefab* Fluxion_Prefab_CreateFromObject(FluxionSceneHandle scene, FluxionGameObjectHandle root);

void Fluxion_Prefab_Destroy(FluxionPrefab* prefab);

// What this prefab is called. Objects copied from it carry this, so a
// copy can be told which prefab it belongs to without holding a pointer
// to something that may have been destroyed.
FluxionUUID Fluxion_Prefab_GetId(const FluxionPrefab* prefab);

// Puts a copy into a scene and hands back its root.
//
// Every object of the copy gets an id of its own -- a copy is a new
// object, and two objects answering to one id would make every lookup by
// id a coin toss. References inside the prefab are remapped to point
// within the copy; references out of it name nothing.
FluxionEntityHandle Fluxion_Prefab_Instantiate(const FluxionPrefab* prefab, FluxionSceneHandle scene);

// Whether this object's component holds something other than what the
// prefab says it should.
//
// False when the object came from no prefab, from a different one, or
// when the type is one the prefab's copy of that object does not carry.
bool Fluxion_Prefab_IsOverridden(const FluxionPrefab* prefab, FluxionSceneHandle scene,
                                 FluxionEntityHandle object, FluxionTypeId type);

// Puts the prefab's values back over this object's, undoing every change
// made since it was copied. The object keeps its own id and its place in
// the scene -- only what its components hold changes.
bool Fluxion_Prefab_Revert(const FluxionPrefab* prefab, FluxionSceneHandle scene, FluxionEntityHandle object);

// The other direction: what this object holds becomes what the prefab
// says. Copies made afterwards get the new values; copies already made
// keep theirs until they are reverted.
bool Fluxion_Prefab_Apply(FluxionPrefab* prefab, FluxionSceneHandle scene, FluxionEntityHandle object);

// --- What is and is not carried ------------------------------------------
//
// Native data components are carried by all four of the above. Script
// components are not: what a script component holds lives in a machine
// belonging to one scene, and moving it to another means building a new
// instance of the class there. That is a larger question than a prefab,
// and answering it badly would be worse than saying so.

#ifdef __cplusplus
}
#endif
