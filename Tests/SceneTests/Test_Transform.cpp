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

#include <Fluxion/Scene/Scene.h>

#include <cmath>

namespace
{

bool Near(f32 a, f32 b)
{
    const f32 difference = a - b;
    return (difference < 0.0f ? -difference : difference) < 0.001f;
}

// The point a matrix puts the origin of whatever it describes: the last
// column of the row-major arrangement the whole engine uses.
FluxionVec3 OriginOf(const FluxionMat4& matrix)
{
    FluxionVec3 point = { matrix.m[0][3], matrix.m[1][3], matrix.m[2][3] };
    return point;
}

bool NearPoint(FluxionVec3 point, f32 x, f32 y, f32 z)
{
    return Near(point.x, x) && Near(point.y, y) && Near(point.z, z);
}

FluxionQuat AboutY(f32 radians)
{
    FluxionQuat rotation;
    rotation.x = 0.0f;
    rotation.y = std::sin(radians * 0.5f);
    rotation.z = 0.0f;
    rotation.w = std::cos(radians * 0.5f);
    return rotation;
}

} // namespace

void Test_Transform_Run(TestContext& ctx)
{
    // --- What an object starts with -------------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");

        TEST_CHECK(ctx, NearPoint(Fluxion_GameObject_GetLocalPosition(scene, object), 0.0f, 0.0f, 0.0f));
        TEST_CHECK(ctx, NearPoint(Fluxion_GameObject_GetLocalScale(scene, object), 1.0f, 1.0f, 1.0f));

        const FluxionQuat rotation = Fluxion_GameObject_GetLocalRotation(scene, object);
        TEST_CHECK(ctx, Near(rotation.x, 0.0f) && Near(rotation.y, 0.0f) && Near(rotation.z, 0.0f) && Near(rotation.w, 1.0f));

        const FluxionMat4 world = Fluxion_GameObject_GetWorldMatrix(scene, object);
        TEST_CHECK(ctx, Near(world.m[0][0], 1.0f) && Near(world.m[1][1], 1.0f) && Near(world.m[2][2], 1.0f) && Near(world.m[3][3], 1.0f));
        TEST_CHECK(ctx, NearPoint(OriginOf(world), 0.0f, 0.0f, 0.0f));

        Fluxion_Scene_Destroy(scene);
    }

    // --- A child sees what its parent does ------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle parent = Fluxion_Scene_CreateGameObject(scene, "parent");
        FluxionGameObjectHandle child = Fluxion_Scene_CreateGameObject(scene, "child");
        Fluxion_GameObject_SetParent(scene, child, parent);

        FluxionVec3 parentPosition = { 10.0f, 0.0f, 0.0f };
        FluxionVec3 childPosition = { 0.0f, 2.0f, 0.0f };
        Fluxion_GameObject_SetLocalPosition(scene, parent, parentPosition);
        Fluxion_GameObject_SetLocalPosition(scene, child, childPosition);

        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, child)), 10.0f, 2.0f, 0.0f));

        // Moving the parent moves the child, without the child being
        // touched at all.
        FluxionVec3 moved = { -4.0f, 1.0f, 3.0f };
        Fluxion_GameObject_SetLocalPosition(scene, parent, moved);
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, child)), -4.0f, 3.0f, 3.0f));
        TEST_CHECK(ctx, NearPoint(Fluxion_GameObject_GetLocalPosition(scene, child), 0.0f, 2.0f, 0.0f));

        // A quarter turn of the parent about Y carries the child round
        // with it: a child two along +Y stays on +Y, while one along +X
        // ends up on -Z.
        Fluxion_GameObject_SetLocalPosition(scene, parent, FluxionVec3{ 0.0f, 0.0f, 0.0f });
        Fluxion_GameObject_SetLocalRotation(scene, parent, AboutY(1.57079633f));
        Fluxion_GameObject_SetLocalPosition(scene, child, FluxionVec3{ 1.0f, 0.0f, 0.0f });
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, child)), 0.0f, 0.0f, -1.0f));

        // And the parent's scale multiplies where the child lands.
        Fluxion_GameObject_SetLocalRotation(scene, parent, Fluxion_Quat_Identity());
        Fluxion_GameObject_SetLocalScale(scene, parent, FluxionVec3{ 2.0f, 3.0f, 4.0f });
        Fluxion_GameObject_SetLocalPosition(scene, child, FluxionVec3{ 1.0f, 1.0f, 1.0f });
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, child)), 2.0f, 3.0f, 4.0f));

        Fluxion_Scene_Destroy(scene);
    }

    // --- Reparenting ----------------------------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle left = Fluxion_Scene_CreateGameObject(scene, "left");
        FluxionGameObjectHandle right = Fluxion_Scene_CreateGameObject(scene, "right");
        FluxionGameObjectHandle child = Fluxion_Scene_CreateGameObject(scene, "child");

        Fluxion_GameObject_SetLocalPosition(scene, left, FluxionVec3{ 1.0f, 0.0f, 0.0f });
        Fluxion_GameObject_SetLocalPosition(scene, right, FluxionVec3{ 0.0f, 0.0f, 5.0f });
        Fluxion_GameObject_SetLocalPosition(scene, child, FluxionVec3{ 0.0f, 1.0f, 0.0f });

        Fluxion_GameObject_SetParent(scene, child, left);
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, child)), 1.0f, 1.0f, 0.0f));

        // The local transform is what is kept, so where the object ends up
        // in the world is whatever that now means under the new parent.
        Fluxion_GameObject_SetParent(scene, child, right);
        TEST_CHECK(ctx, NearPoint(Fluxion_GameObject_GetLocalPosition(scene, child), 0.0f, 1.0f, 0.0f));
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, child)), 0.0f, 1.0f, 5.0f));

        // Back up to the scene's roots, and the local transform is the
        // whole of it.
        Fluxion_GameObject_SetParent(scene, child, Fluxion_GameObject_InvalidHandle());
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, child)), 0.0f, 1.0f, 0.0f));

        Fluxion_Scene_Destroy(scene);
    }

    // --- A deep chain composes the whole way down -----------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionGameObjectHandle chain[5];
        chain[0] = Fluxion_Scene_CreateGameObject(scene, "level0");
        for (u32 i = 1; i < 5; ++i)
        {
            chain[i] = Fluxion_Scene_CreateGameObject(scene, "level");
            Fluxion_GameObject_SetParent(scene, chain[i], chain[i - 1]);
            Fluxion_GameObject_SetLocalPosition(scene, chain[i], FluxionVec3{ 1.0f, 0.0f, 0.0f });
        }
        Fluxion_GameObject_SetLocalPosition(scene, chain[0], FluxionVec3{ 1.0f, 0.0f, 0.0f });

        // Five links of one each.
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, chain[4])), 5.0f, 0.0f, 0.0f));

        // Halving the top halves every step below it.
        Fluxion_GameObject_SetLocalScale(scene, chain[0], FluxionVec3{ 0.5f, 0.5f, 0.5f });
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, chain[4])), 3.0f, 0.0f, 0.0f));

        // Turning the second link about Y sends everything below it along
        // -Z instead of +X, while the two links above it stay put.
        Fluxion_GameObject_SetLocalScale(scene, chain[0], FluxionVec3{ 1.0f, 1.0f, 1.0f });
        Fluxion_GameObject_SetLocalRotation(scene, chain[1], AboutY(1.57079633f));
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, chain[1])), 2.0f, 0.0f, 0.0f));
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, chain[4])), 2.0f, 0.0f, -3.0f));

        Fluxion_Scene_Destroy(scene);
    }

    // --- Turning by an amount rather than setting an amount -------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");
        FluxionGameObjectHandle child = Fluxion_Scene_CreateGameObject(scene, "child");
        Fluxion_GameObject_SetParent(scene, child, object);
        Fluxion_GameObject_SetLocalPosition(scene, child, FluxionVec3{ 1.0f, 0.0f, 0.0f });

        // Two eighths of a turn about Y make a quarter.
        Fluxion_GameObject_Rotate(scene, object, FluxionVec3{ 0.0f, 0.78539816f, 0.0f });
        Fluxion_GameObject_Rotate(scene, object, FluxionVec3{ 0.0f, 0.78539816f, 0.0f });
        TEST_CHECK(ctx, NearPoint(OriginOf(Fluxion_GameObject_GetWorldMatrix(scene, child)), 0.0f, 0.0f, -1.0f));

        Fluxion_Scene_Destroy(scene);
    }
}
