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
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where an object is, stored the same way every other per-object value
// is: as a component, in the blocks, one column per field group.
//
// It is a component so that a pass over many objects' world matrices --
// which is what drawing and culling are -- reads them one after another
// rather than reaching into a large record per object. The one that has
// to happen every frame regardless is the previous-world copy, and as a
// column that is one memcpy per block.
//
// It differs from every other component in one way, and only one: an
// object cannot be without it. It is given at creation and cannot be
// taken away, so "where is this object" never has to be answered with
// "nowhere".

typedef enum FluxionTransformDirty
{
    FLUXION_TRANSFORM_CLEAN = 0,

    // The local position, rotation or scale changed, so the matrix built
    // from the three of them no longer matches them.
    FLUXION_TRANSFORM_DIRTY_LOCAL = 1u << 0,

    // The world matrix no longer matches -- either because this object's
    // own local changed, or because something above it did.
    FLUXION_TRANSFORM_DIRTY_WORLD = 1u << 1,
} FluxionTransformDirty;

typedef struct FluxionTransform
{
    FluxionVec3 localPosition;
    FluxionQuat localRotation;
    FluxionVec3 localScale;

    // This object's translation, rotation and scale composed with those of
    // everything above it. Written by the update, read by everything that
    // draws.
    FluxionMat4 worldMatrix;

    // What worldMatrix was one step ago. Kept because a renderer that
    // compares frames -- for motion blur, for reprojection, for anything
    // temporal -- needs to know where a thing was, and by the time it
    // asks, the only copy of that would otherwise have been overwritten.
    //
    // Exactly one step behind, always: it is refreshed for every object at
    // the start of each update, whether that object moved or not, so a
    // thing that stopped moving reports no motion rather than the motion
    // it had two steps ago.
    FluxionMat4 previousWorldMatrix;

    // FluxionTransformDirty, as a set.
    u32 dirtyFlags;
} FluxionTransform;

// The id this component is registered under. The scripting layer names
// the same type by the same id, so a script's transform and a stored
// transform are one thing rather than two that happen to agree.
FluxionTypeId Fluxion_Transform_TypeId(void);

// What ties an object to the scripts attached to it.
//
// The scripts themselves are not stored here. A script is an object in
// another runtime, with a lifetime and a class and pinned references, and
// none of that is bytes an entity can own; what an entity owns is the
// link. So this holds the head of the object's list of them, and the
// records those number into live in the scripting half of this module.
//
// It is a component rather than a field on the object because that gives
// it one home rather than two, and because it makes "which objects have
// scripts" a question the storage can answer directly instead of a walk
// over every object.
//
// Like the transform, every object has one and it cannot be taken away.
// That costs a word per object whether or not it carries scripts, and
// buys the absence of a case where attaching the first script to an
// object would move that object's storage -- which would happen exactly
// while the scripts of the step are running over it.
typedef struct FluxionScriptComponent
{
    // FLUXION_SCENE_NO_COMPONENT when this object carries no scripts.
    u32 firstComponent;
} FluxionScriptComponent;

FluxionTypeId Fluxion_ScriptComponent_TypeId(void);

// Where a copy came from.
//
// Only the objects made by copying a prefab carry this -- which is what
// storage grouped by composition is for: the ones that did not, do not
// pay for it.
//
// Two ids rather than one, because a prefab holds several objects and a
// copy of it holds several too: the first says which prefab, the second
// says which of ITS objects this one was copied from. Without the second,
// telling a copy what it should look like would mean guessing which
// original it corresponds to.
typedef struct FluxionPrefabLink
{
    FluxionUUID prefab;
    FluxionUUID sourceEntity;
} FluxionPrefabLink;

FluxionTypeId Fluxion_PrefabLink_TypeId(void);

#ifdef __cplusplus
}
#endif
