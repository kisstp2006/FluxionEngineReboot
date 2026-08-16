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
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// A field that points at an asset.
//
// An id, not a path and not a handle. A path changes when someone renames
// a folder, and in a shipped game there are no paths left to point at; a
// handle means where something sits in a table right now, and means
// nothing once the program that made it has ended.
//
// One consequence is worth saying out loud: a reference like this
// SURVIVES BEING WRITTEN DOWN WITH NO SPECIAL HANDLING AT ALL. It is
// sixteen bytes that mean the same thing in every run. An entity handle
// needed work to save because the opposite is true of it.
typedef struct FluxionAssetRef
{
    // Nil points at nothing -- a field that was never filled in, which is
    // a normal state and not damage.
    FluxionUUID asset;
} FluxionAssetRef;

// Registered so that a field holding one can be RECOGNISED, which is a
// different need from being saved: whoever packages a build has to be
// able to walk an object's fields and ask which assets it will reach for,
// and a field's declared type is how that question gets an answer.
FluxionTypeId Fluxion_AssetRef_TypeId(void);

static inline bool Fluxion_AssetRef_IsSet(FluxionAssetRef ref)
{
    return !Fluxion_UUID_IsNil(ref.asset);
}

#ifdef __cplusplus
}
#endif
