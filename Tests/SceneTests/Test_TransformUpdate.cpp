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

#include "TestFramework.h"

#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Scene/EntityQuery.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/Transform.h>

#include <cstdio>
#include <cstring>

// The transform as a component, and working the world matrices out for a
// whole scene at once.
//
// Two things here are the sort that go wrong quietly. A child worked out
// before its parent gives a matrix that is merely one step stale, which
// looks like nothing at all when the object was not moving much. And a
// previous-world that is two steps behind instead of one produces motion
// twice as long as the real thing -- which nothing shows today, and which
// whatever first draws with it would show as smeared movement, far from
// here.
//
// So both are checked against something arithmetic rather than against
// "it looked right": positions along a chain multiply out to numbers that
// can be written down in advance.

namespace
{

bool NearlyEqual(f32 a, f32 b)
{
    const f32 difference = (a > b) ? (a - b) : (b - a);
    return difference < 0.0001f;
}

bool SameMatrix(const FluxionMat4& a, const FluxionMat4& b)
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            if (!NearlyEqual(a.m[row][column], b.m[row][column])) return false;
        }
    }
    return true;
}

FluxionVec3 Translation(const FluxionMat4& matrix)
{
    FluxionVec3 out;
    out.x = matrix.m[0][3];
    out.y = matrix.m[1][3];
    out.z = matrix.m[2][3];
    return out;
}

FluxionVec3 MakeVec3(f32 x, f32 y, f32 z)
{
    FluxionVec3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}

// A straight line of objects, each one child of the one before, each
// moved one unit further along x. Object i therefore sits at x == i+1 in
// the world, which is an answer that can be written down without running
// the thing being tested.
void BuildChain(FluxionSceneHandle scene, FluxionGameObjectHandle* out, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "link %u", i);
        out[i] = Fluxion_Scene_CreateGameObject(scene, name);
        if (i != 0) Fluxion_GameObject_SetParent(scene, out[i], out[i - 1]);
        Fluxion_GameObject_SetLocalPosition(scene, out[i], MakeVec3(1.0f, 0.0f, 0.0f));
    }
}

// The same line, but built so that the order the objects were MADE in is
// the reverse of the order they have to be worked out in: the object made
// first ends up deepest.
//
// This distinction is the whole point of the test that uses it. A chain
// built the ordinary way is stored in the same order it has to be
// computed in, so an update that ignored depth entirely and just went
// through the objects as it found them would still get the right answer,
// by luck. Reversed, that luck runs out.
//
// out[0] is the deepest link and out[count-1] is the root, so out[i] sits
// at x == count - i in the world.
void BuildReversedChain(FluxionSceneHandle scene, FluxionGameObjectHandle* out, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "reversed link %u", i);
        out[i] = Fluxion_Scene_CreateGameObject(scene, name);
    }
    for (u32 i = 0; i < count; ++i)
    {
        if (i + 1 < count) Fluxion_GameObject_SetParent(scene, out[i], out[i + 1]);
        Fluxion_GameObject_SetLocalPosition(scene, out[i], MakeVec3(1.0f, 0.0f, 0.0f));
    }
}

} // namespace

// Every check below, run once per way the work can be spread out.
static void Test_TransformUpdate_Body(TestContext& ctx)
{
    // --- Every object has one, and it cannot be taken away ---------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");

        TEST_CHECK(ctx, Fluxion_GameObject_HasComponent(scene, object, Fluxion_Transform_TypeId()));

        // A brand new object is at the origin, unrotated, unscaled -- not
        // at a zero scale, which is what the storage's own zeroing would
        // have left if nothing wrote over it.
        {
            const FluxionVec3 scale = Fluxion_GameObject_GetLocalScale(scene, object);
            const FluxionQuat rotation = Fluxion_GameObject_GetLocalRotation(scene, object);
            TEST_CHECK(ctx, NearlyEqual(scale.x, 1.0f) && NearlyEqual(scale.y, 1.0f) && NearlyEqual(scale.z, 1.0f));
            TEST_CHECK(ctx, NearlyEqual(rotation.w, 1.0f));
            TEST_CHECK(ctx, SameMatrix(Fluxion_GameObject_GetWorldMatrix(scene, object), Fluxion_Mat4_Identity()));
        }

        TEST_CHECK(ctx, !Fluxion_GameObject_RemoveComponent(scene, object, Fluxion_Transform_TypeId()));
        TEST_CHECK(ctx, Fluxion_GameObject_HasComponent(scene, object, Fluxion_Transform_TypeId()));

        // Reachable as a component too, and it is the same storage the
        // named entry points read -- not a copy that could drift.
        {
            FluxionTransform* transform = (FluxionTransform*)Fluxion_GameObject_GetComponent(scene, object, Fluxion_Transform_TypeId());
            TEST_CHECK(ctx, transform != nullptr);

            Fluxion_GameObject_SetLocalPosition(scene, object, MakeVec3(4.0f, 5.0f, 6.0f));
            TEST_CHECK(ctx, transform != nullptr && NearlyEqual(transform->localPosition.y, 5.0f));
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- A child is never worked out before its parent -------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        constexpr u32 kLinks = 12;
        FluxionGameObjectHandle chain[kLinks];
        BuildChain(scene, chain, kLinks);

        // One turn, and then every link is where the chain puts it. Worked
        // out all at once, in depth order -- a link done before its parent
        // would sit one unit short, which is exactly the kind of wrongness
        // that looks like nothing.
        Fluxion_Scene_Tick(scene, 0.016f);

        for (u32 i = 0; i < kLinks; ++i)
        {
            const FluxionVec3 world = Translation(Fluxion_GameObject_GetWorldMatrix(scene, chain[i]));
            TEST_CHECK(ctx, NearlyEqual(world.x, (f32)(i + 1)));
            TEST_CHECK(ctx, NearlyEqual(world.y, 0.0f));
        }

        // Moving the root moves everything below it, and only after the
        // turn that carries the change -- nothing here is worked out
        // early.
        Fluxion_GameObject_SetLocalPosition(scene, chain[0], MakeVec3(10.0f, 0.0f, 0.0f));
        Fluxion_Scene_Tick(scene, 0.016f);

        for (u32 i = 0; i < kLinks; ++i)
        {
            const FluxionVec3 world = Translation(Fluxion_GameObject_GetWorldMatrix(scene, chain[i]));
            TEST_CHECK(ctx, NearlyEqual(world.x, (f32)(i + 10)));
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- ... even when the storage order is the wrong way round ----------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        // Made first, deepest. An update that walked the objects in the
        // order it found them, taking no notice of depth, would work each
        // link out from a parent that had not been worked out yet -- and
        // every link would come out one step stale. The chain built the
        // ordinary way cannot show that, because there the two orders
        // agree by accident.
        constexpr u32 kLinks = 10;
        FluxionGameObjectHandle chain[kLinks];
        BuildReversedChain(scene, chain, kLinks);

        Fluxion_Scene_Tick(scene, 0.016f);

        for (u32 i = 0; i < kLinks; ++i)
        {
            const FluxionVec3 world = Translation(Fluxion_GameObject_GetWorldMatrix(scene, chain[i]));
            TEST_CHECK(ctx, NearlyEqual(world.x, (f32)(kLinks - i)));
        }

        // And again after moving the root, which is the deepest-numbered
        // one here.
        Fluxion_GameObject_SetLocalPosition(scene, chain[kLinks - 1], MakeVec3(20.0f, 0.0f, 0.0f));
        Fluxion_Scene_Tick(scene, 0.016f);

        for (u32 i = 0; i < kLinks; ++i)
        {
            const FluxionVec3 world = Translation(Fluxion_GameObject_GetWorldMatrix(scene, chain[i]));
            TEST_CHECK(ctx, NearlyEqual(world.x, (f32)(kLinks - i - 1) + 20.0f));
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- Asking early and asking late give the same answer ---------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        constexpr u32 kLinks = 6;
        FluxionGameObjectHandle chain[kLinks];
        FluxionMat4 asked[kLinks];
        BuildChain(scene, chain, kLinks);

        // Asked before any turn has run: each answer is worked out there
        // and then, walking up the parents.
        for (u32 i = 0; i < kLinks; ++i) asked[i] = Fluxion_GameObject_GetWorldMatrix(scene, chain[i]);

        // And now the batched way. The two paths are separate code and
        // must not be able to disagree; if they could, a scene would look
        // different depending on whether anything happened to read a
        // transform mid-turn.
        Fluxion_GameObject_SetLocalPosition(scene, chain[0], MakeVec3(1.0f, 0.0f, 0.0f));
        Fluxion_Scene_Tick(scene, 0.016f);

        for (u32 i = 0; i < kLinks; ++i)
        {
            TEST_CHECK(ctx, SameMatrix(asked[i], Fluxion_GameObject_GetWorldMatrix(scene, chain[i])));
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- Depth follows a change of parent, subtree and all ---------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        // Two chains, then the head of the second is hung off the tail of
        // the first: everything below it drops by the first chain's depth,
        // and its world position picks up the first chain's offset.
        constexpr u32 kEach = 4;
        FluxionGameObjectHandle first[kEach];
        FluxionGameObjectHandle second[kEach];

        BuildChain(scene, first, kEach);
        BuildChain(scene, second, kEach);
        Fluxion_Scene_Tick(scene, 0.016f);

        // Before: the second chain is its own, starting from the origin.
        TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetWorldMatrix(scene, second[kEach - 1])).x, (f32)kEach));

        Fluxion_GameObject_SetParent(scene, second[0], first[kEach - 1]);
        Fluxion_Scene_Tick(scene, 0.016f);

        // After: every link of the second chain sits past the whole of the
        // first. A depth left stale on the deeper links would show up as
        // exactly this number being short.
        for (u32 i = 0; i < kEach; ++i)
        {
            const FluxionVec3 world = Translation(Fluxion_GameObject_GetWorldMatrix(scene, second[i]));
            TEST_CHECK(ctx, NearlyEqual(world.x, (f32)(kEach + i + 1)));
        }

        // Back up to a root, and the subtree comes with it.
        Fluxion_GameObject_SetParent(scene, second[0], Fluxion_GameObject_InvalidHandle());
        Fluxion_Scene_Tick(scene, 0.016f);

        for (u32 i = 0; i < kEach; ++i)
        {
            TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetWorldMatrix(scene, second[i])).x, (f32)(i + 1)));
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- The previous world is one turn behind, and exactly one ----------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "mover");

        // Before anything has happened it equals where the object is, so
        // nothing reports motion for having just been created.
        TEST_CHECK(ctx, SameMatrix(Fluxion_GameObject_GetPreviousWorldMatrix(scene, object), Fluxion_Mat4_Identity()));

        Fluxion_GameObject_SetLocalPosition(scene, object, MakeVec3(5.0f, 0.0f, 0.0f));
        Fluxion_Scene_Tick(scene, 0.016f);

        // One turn after moving: previous is where it was, current is
        // where it is.
        TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetWorldMatrix(scene, object)).x, 5.0f));
        TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetPreviousWorldMatrix(scene, object)).x, 0.0f));

        // A second turn with no movement: previous catches up. This is the
        // one that a stale-by-two mistake fails -- it would still say
        // zero, and everything downstream would go on drawing motion for
        // an object standing still.
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetWorldMatrix(scene, object)).x, 5.0f));
        TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetPreviousWorldMatrix(scene, object)).x, 5.0f));

        // And further turns leave it alone rather than drifting.
        Fluxion_Scene_Tick(scene, 0.016f);
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetPreviousWorldMatrix(scene, object)).x, 5.0f));

        // Moving again starts it over.
        Fluxion_GameObject_SetLocalPosition(scene, object, MakeVec3(9.0f, 0.0f, 0.0f));
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetPreviousWorldMatrix(scene, object)).x, 5.0f));
        TEST_CHECK(ctx, NearlyEqual(Translation(Fluxion_GameObject_GetWorldMatrix(scene, object)).x, 9.0f));

        Fluxion_Scene_Destroy(scene);
    }

    // --- Enough objects that the work is actually handed to workers ------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        // Wide rather than deep: one parent with many children, so a
        // single depth holds enough objects to be worth splitting. This is
        // the shape that runs on several threads at once, and the answers
        // must be the same ones a single thread gives.
        constexpr u32 kChildren = 500;
        FluxionGameObjectHandle parent = Fluxion_Scene_CreateGameObject(scene, "parent");
        static FluxionGameObjectHandle children[kChildren];

        Fluxion_GameObject_SetLocalPosition(scene, parent, MakeVec3(100.0f, 0.0f, 0.0f));
        for (u32 i = 0; i < kChildren; ++i)
        {
            children[i] = Fluxion_Scene_CreateGameObject(scene, "child");
            Fluxion_GameObject_SetParent(scene, children[i], parent);
            Fluxion_GameObject_SetLocalPosition(scene, children[i], MakeVec3(0.0f, (f32)i, 0.0f));
        }

        Fluxion_Scene_Tick(scene, 0.016f);

        for (u32 i = 0; i < kChildren; ++i)
        {
            const FluxionVec3 world = Translation(Fluxion_GameObject_GetWorldMatrix(scene, children[i]));
            TEST_CHECK(ctx, NearlyEqual(world.x, 100.0f));
            TEST_CHECK(ctx, NearlyEqual(world.y, (f32)i));
        }

        // The whole set again through the storage rather than one at a
        // time, which is the way anything drawing them will read them: the
        // column and the per-object answers have to agree.
        {
            const FluxionTypeId required = Fluxion_Transform_TypeId();
            FluxionEntityQueryDesc desc;
            desc.required = &required;
            desc.requiredCount = 1;
            desc.excluded = nullptr;
            desc.excludedCount = 0;

            FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
            FluxionEntityChunkView chunk;
            u32 seen = 0;

            while (Fluxion_EntityQuery_Next(&query, &chunk))
            {
                const FluxionTransform* transforms = (const FluxionTransform*)Fluxion_EntityChunk_Column(&chunk, required);
                TEST_CHECK(ctx, transforms != nullptr);
                if (transforms == nullptr) continue;

                for (u32 row = 0; row < chunk.count; ++row)
                {
                    const FluxionMat4 asked = Fluxion_GameObject_GetWorldMatrix(scene, chunk.entities[row]);
                    TEST_CHECK(ctx, SameMatrix(transforms[row].worldMatrix, asked));
                    ++seen;
                }
            }
            TEST_CHECK(ctx, seen == kChildren + 1u);
        }

        Fluxion_Scene_Destroy(scene);
    }
}

void Test_TransformUpdate_Run(TestContext& ctx)
{
    // Three ways the same work can be spread out, and all three have to
    // give the same answers.
    //
    // With real workers the objects at one depth are worked out at the
    // same time as each other; forced onto one thread they are worked out
    // in order; with no job system at all a different branch runs
    // entirely. Anything that only holds because the work happened to
    // happen in one particular order fails one of the three.

    Fluxion_JobSystem_Init(0, false);
    Test_TransformUpdate_Body(ctx);
    Fluxion_JobSystem_Shutdown();

    Fluxion_JobSystem_Init(0, true);
    Test_TransformUpdate_Body(ctx);
    Fluxion_JobSystem_Shutdown();

    Test_TransformUpdate_Body(ctx);
}
