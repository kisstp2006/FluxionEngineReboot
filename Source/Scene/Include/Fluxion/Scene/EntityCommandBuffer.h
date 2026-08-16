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

// Structural change -- making an object, destroying one, moving one under
// another, attaching or taking away a component -- written down now and
// carried out later, all of it at one point the caller picks.
//
// Why write it down at all: code that walks a set of objects and changes
// which objects exist while it walks is asking the set to change under it.
// Recording the change instead leaves the walk looking at exactly what it
// started with, and every recorded change lands afterwards, in the order
// it was asked for.
//
// A buffer belongs to nobody in particular and names no scene until it is
// played back, so the same one may be filled by several pieces of code and
// emptied once.

typedef struct FluxionEntityCommandBuffer FluxionEntityCommandBuffer;

// Which object a recorded command is about.
//
// An object that already exists is named by its handle. An object that
// this same buffer has yet to make has no handle to name it by -- it does
// not exist yet -- so it is named by the id the buffer handed back when
// the making of it was recorded. Playback works through the commands in
// order, so by the time a command naming a pending object is reached, that
// object has been made and the id finds it.
typedef struct FluxionEntityTarget
{
    // Invalid when the object is named by id instead.
    FluxionGameObjectHandle handle;

    // Nil when the object is named by handle instead.
    FluxionUUID uuid;
} FluxionEntityTarget;

FluxionEntityTarget Fluxion_EntityTarget_Existing(FluxionGameObjectHandle object);
FluxionEntityTarget Fluxion_EntityTarget_Pending(FluxionUUID uuid);

// Null when there is no memory for it.
FluxionEntityCommandBuffer* Fluxion_EntityCommandBuffer_Create(void);
void Fluxion_EntityCommandBuffer_Destroy(FluxionEntityCommandBuffer* buffer);

// Records the making of an object and hands back the id it will have, so
// that later commands in this same buffer -- and the caller afterwards --
// can name it. A nil id means the command could not be recorded.
//
// `name` may be null, which names the object the empty string.
FluxionUUID Fluxion_EntityCommandBuffer_CreateGameObject(FluxionEntityCommandBuffer* buffer, const char* name);

// Destroys the object and everything below it, as
// Fluxion_GameObject_Destroy does.
bool Fluxion_EntityCommandBuffer_DestroyGameObject(FluxionEntityCommandBuffer* buffer, FluxionEntityTarget object);

// A `parent` naming no object -- an invalid handle and a nil id together
// -- moves the object back up to the scene's roots.
bool Fluxion_EntityCommandBuffer_SetParent(FluxionEntityCommandBuffer* buffer, FluxionEntityTarget object, FluxionEntityTarget parent);

// `value` may be null, which starts the component as all zero bytes;
// otherwise `valueSize` bytes are copied out of it now and kept in the
// buffer, so the caller's copy need not outlive this call.
//
// A `valueSize` that does not match the registered size of the type is
// refused here rather than at playback, where the caller is no longer
// standing over it.
bool Fluxion_EntityCommandBuffer_AddComponent(FluxionEntityCommandBuffer* buffer, FluxionEntityTarget object, FluxionTypeId type, const void* value, usize valueSize);

bool Fluxion_EntityCommandBuffer_RemoveComponent(FluxionEntityCommandBuffer* buffer, FluxionEntityTarget object, FluxionTypeId type);

// How many commands are waiting.
u32 Fluxion_EntityCommandBuffer_Count(const FluxionEntityCommandBuffer* buffer);

// Carries out every recorded command against this scene, in the order they
// were recorded, and empties the buffer -- including when the scene is not
// live, so that a buffer never quietly holds a turn's worth of commands
// into the next one.
//
// A command that cannot be carried out -- it names an object that has
// since gone, or a component type the scene has no room for -- is passed
// over and the rest still run. The count of those is the answer, so zero
// means every command landed.
u32 Fluxion_EntityCommandBuffer_Playback(FluxionEntityCommandBuffer* buffer, FluxionSceneHandle scene);

// Throws away every recorded command without carrying any of them out.
void Fluxion_EntityCommandBuffer_Clear(FluxionEntityCommandBuffer* buffer);

// --- The scene's own buffer ---------------------------------------------

// Every scene keeps one, played back at the end of each
// Fluxion_Scene_Tick -- after every component has been through the turn,
// so that no component sees the set of objects change mid-turn. Anything
// with no buffer of its own can record into this one.
//
// Null for a handle that names no live scene. It belongs to the scene:
// destroying it is not the caller's to do.
FluxionEntityCommandBuffer* Fluxion_Scene_GetCommandBuffer(FluxionSceneHandle scene);

#ifdef __cplusplus
}
#endif
