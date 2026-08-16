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

// Reading every entity that carries a particular set of components.
//
// Entities are stored grouped by which components they carry, in blocks.
// A query walks the blocks whose grouping matches, and hands each one
// over whole: the entities in it, and for each named type a pointer to
// that type's values for exactly those entities, laid out one after
// another with nothing in between.
//
// That is why this is block-shaped rather than entity-shaped. Asking for
// one entity at a time would give the same answers and throw away the
// only thing this storage is for -- that the values a pass reads are
// already next to each other. Code that would rather see one entity at a
// time can walk a block's rows itself; code that wants speed reads the
// columns.
//
// A query holds no storage and takes nothing: it is a position in the
// scene, made where it is used and abandoned when the walk ends. What it
// does hold is the assumption that nothing structural happens while it
// runs -- see below.

// One block: the entities in it, and where its columns are.
//
// `entities` and every column of this block have `count` entries, in the
// same order, so entry i of a column belongs to entry i of `entities`.
typedef struct FluxionEntityChunkView
{
    const FluxionEntityHandle* entities;
    u32 count;

    // Which block this is. Only Fluxion_EntityChunk_Column reads these;
    // to a caller they are the block's identity and nothing more.
    FluxionSceneHandle scene;
    u32 archetypeIndex;
    u32 chunkIndex;
} FluxionEntityChunkView;

// Which entities a query is about.
//
// An entity matches when it carries EVERY type in `required` and NONE of
// the types in `excluded`. It may carry others besides: a query names
// what it needs, not what an entity is allowed to be, so a pass that
// reads positions keeps working when something else starts giving those
// same entities a colour.
//
// An empty `required` matches every entity in the scene, which is how
// something that has to see all of them asks. `excluded` may be null with
// a count of zero.
//
// Both arrays belong to the caller and are only read while the query is
// being made; they need not outlive it.
typedef struct FluxionEntityQueryDesc
{
    const FluxionTypeId* required;
    u32 requiredCount;

    const FluxionTypeId* excluded;
    u32 excludedCount;
} FluxionEntityQueryDesc;

// A position part-way through a walk. Made by Fluxion_Scene_Query,
// advanced by Fluxion_EntityQuery_Next, and finished when that returns
// false. Copy it and the copy walks the same remaining blocks.
typedef struct FluxionEntityQuery
{
    FluxionSceneHandle scene;

    FluxionTypeId required[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    u32 requiredCount;
    FluxionTypeId excluded[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    u32 excludedCount;

    // Where the walk has got to. Both move; the archetype only when its
    // blocks run out.
    u32 archetypeIndex;
    u32 chunkIndex;

    // Cleared when the description could not be taken as given -- more
    // types than a scene can hold, or a scene that is not live. Such a
    // query yields nothing rather than yielding everything, because
    // "every entity" is the answer to an empty query and would be a
    // dangerous thing to return by accident.
    bool valid;
} FluxionEntityQuery;

FluxionEntityQuery Fluxion_Scene_Query(FluxionSceneHandle scene, const FluxionEntityQueryDesc* desc);

// Fills `outChunk` with the next matching block and answers true, or
// answers false once there are none left. Blocks with no entities in them
// are passed over, so a block handed out always has at least one.
bool Fluxion_EntityQuery_Next(FluxionEntityQuery* query, FluxionEntityChunkView* outChunk);

// This block's values for one type, or null when the block's entities do
// not carry that type. Asking for a type the query required always
// answers.
//
// The pointer is good for as long as the block view is -- which is to say
// until the next structural change in the scene. A walk that has to
// change things records them into an entity command buffer and lets them
// land after it ends; changing them in the middle moves the very values
// being read.
void* Fluxion_EntityChunk_Column(const FluxionEntityChunkView* chunk, FluxionTypeId type);

// How many entities match, without looking at any of their values. Walks
// the same blocks and adds up their counts.
u32 Fluxion_Scene_CountMatching(FluxionSceneHandle scene, const FluxionEntityQueryDesc* desc);

#ifdef __cplusplus
}
#endif
