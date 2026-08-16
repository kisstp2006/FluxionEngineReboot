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

#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Scene/Scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// Writing a scene down and reading it back.
//
// What is written is what cannot be worked out again: which objects there
// are, what each is called, what is under what, and what each object's
// components hold. What is left out is everything derived -- world
// matrices, which block an object sits in, which composition it belongs
// to. All of that arises again from the same rules that produced it the
// first time.
//
// AN OBJECT IS NAMED BY ITS ID, NEVER BY ITS HANDLE. A handle is where an
// object sits in a table right now; it means nothing once the program
// that wrote it has ended. So every reference in a file -- a parent, a
// component field pointing at another object -- is written as the id, and
// resolved on the way back in.
//
// That resolution is why reading happens in two passes. A reference may
// point at an object written later in the file, and there is no order
// that avoids it: two objects can point at each other. So every object is
// made first, and only then is anything pointed anywhere.

// What this build writes and the oldest it can read.
//
// A file whose version is HIGHER than this is refused rather than read as
// far as it goes: it was written by something that knew things this does
// not, and a partial read of it would be a scene quietly missing whatever
// those were.
#define FLUXION_SCENE_FORMAT_VERSION 1

// Writes the scene into `stream`, which must be a writer.
//
// False when the scene is not live, or when the stream ran out of room --
// which is not an error so much as an answer: ask again with a bigger
// buffer. Fluxion_Scene_SaveToBuffer does that for you.
bool Fluxion_Scene_Save(FluxionSceneHandle scene, FluxionStream* stream);

// Reads a scene out of `stream`, which must be a reader, into `scene`.
//
// Everything the scene already held is destroyed first: this is a load,
// not a merge. False when the scene is not live, when the file was
// written by a newer build, or when the file is damaged -- and in the
// last two cases the scene is left empty rather than half filled, because
// half a scene is harder to notice than none.
bool Fluxion_Scene_Load(FluxionSceneHandle scene, FluxionStream* stream);

// The same, into memory the caller owns.
//
// `capacity` is only where it starts; if that is not enough the buffer is
// grown and the write tried again, so a caller does not have to guess how
// big a scene is -- which it cannot.
//
// What comes back is exactly `*outSize` bytes long, whatever `capacity`
// was, and that is the number to give back to Fluxion_Scene_FreeBuffer.
// Handing back a buffer larger than the caller was told would mean the
// caller could not free it correctly.
u8* Fluxion_Scene_SaveToBuffer(FluxionSceneHandle scene, usize capacity, usize* outSize);
void Fluxion_Scene_FreeBuffer(u8* buffer, usize size);

// Which assets this scene will reach for, gathered from the fields of its
// objects' components, each one listed once.
//
// The return value is how many there ARE, which may be more than
// `capacity`; in that case the first `capacity` were written and the
// return says how big a buffer would have been enough. Pass NULL and zero
// to ask only for the count.
//
// This exists so that whoever packages a build can say what a scene
// requires. A scene does not announce that anywhere -- it is spread
// across component fields -- and a build that leaves out something a
// scene needs is a game that starts and then does not work.
u32 Fluxion_Scene_GatherAssetReferences(FluxionSceneHandle scene, FluxionUUID* outAssets, u32 capacity);

#ifdef __cplusplus
}
#endif
