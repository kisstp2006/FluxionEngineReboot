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
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Every property this serializer handles must fit in this many bytes --
// covers every primitive and every Math.h type (FluxionMat4, the
// largest, is exactly 64 bytes). A property larger than this is a
// programming error (FLUXION_ASSERT), not a data problem.
#define FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE 64

// How long a text property may be. Text is read into a buffer of its own
// on the way in, because the setter is handed a pointer rather than the
// characters; anything longer is stepped over rather than truncated.
#define FLUXION_BINARY_SERIALIZER_MAX_TEXT_LENGTH 256

// Serializes or deserializes `instance` of reflected type `typeInfo`:
// id + version once, then one tag per property (name hash, byte size,
// raw value). On read, an unmatched or size-changed tag is skipped by
// its recorded size and the instance keeps its default; a declared
// property with no tag is simply never written. That is what lets old
// and new streams load into old and new structs without agreeing on the
// exact property set. False on a type-id mismatch or stream overflow.
bool Fluxion_BinarySerializer_Serialize(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance);

// The same, leaving every property whose type is in `skipTypes` entirely
// alone -- not written, not looked for on the way back in.
//
// For a value this cannot carry across on its own. A field holding a
// handle is the case that matters: what a handle names is decided by
// whatever handed it out, and its bytes mean nothing anywhere else. The
// caller who knows what such a field means writes it in terms that
// survive, and says so here.
//
// This layer is deliberately not told WHY a type is skipped, only that it
// is: knowing would mean knowing about things built on top of it.
bool Fluxion_BinarySerializer_SerializeExcept(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance,
                                              const FluxionTypeId* skipTypes, u32 skipCount);

#ifdef __cplusplus
}
#endif
