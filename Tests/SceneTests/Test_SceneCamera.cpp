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

#include <Fluxion/Scene/Camera.h>
#include <Fluxion/Scene/Scene.h>

#include <cmath>

// The camera as a component: where the eye is comes from the object, and
// the two matrices a view takes come from here and nowhere else.

namespace
{

bool Near(f32 value, f32 expected, f32 tolerance)
{
    const f32 difference = value - expected;
    return (difference < 0.0f ? -difference : difference) <= tolerance;
}

// world = view^-1 applied to the origin gives the camera's position, so
// view applied to the camera's position must give the origin.
FluxionVec3 Apply(const FluxionMat4& m, FluxionVec3 p)
{
    FluxionVec3 r;
    r.x = m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3];
    r.y = m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3];
    r.z = m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3];
    return r;
}

void ASceneWithNoCameraSaysSo(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();
    Fluxion_Scene_CreateGameObject(scene, "NotACamera");
    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionMat4 view = Fluxion_Mat4_Identity();
    FluxionMat4 projection = Fluxion_Mat4_Identity();
    TEST_CHECK(ctx, !Fluxion_Scene_GatherCamera(scene, 1.0f, &view, &projection));

    Fluxion_Scene_Destroy(scene);
}

void TheViewUndoesWhereTheCameraStands(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Eye");
    Fluxion_GameObject_SetLocalPosition(scene, object, FluxionVec3{ 3.0f, 4.0f, 5.0f });

    FluxionCamera camera{};
    camera.fovYRadians = 1.0472f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, object, Fluxion_Camera_TypeId(), &camera) != nullptr);

    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionMat4 view = Fluxion_Mat4_Identity();
    FluxionMat4 projection = Fluxion_Mat4_Identity();
    TEST_CHECK(ctx, Fluxion_Scene_GatherCamera(scene, 1.0f, &view, &projection));

    // The camera's own position lands on the origin of camera space.
    const FluxionVec3 atEye = Apply(view, FluxionVec3{ 3.0f, 4.0f, 5.0f });
    TEST_CHECK(ctx, Near(atEye.x, 0.0f, 0.001f) && Near(atEye.y, 0.0f, 0.001f) && Near(atEye.z, 0.0f, 0.001f));

    // A point one unit in front of the eye (negative Z) stays one unit in
    // front in camera space -- the view moves the world, not the axes.
    const FluxionVec3 ahead = Apply(view, FluxionVec3{ 3.0f, 4.0f, 4.0f });
    TEST_CHECK(ctx, Near(ahead.z, -1.0f, 0.001f));

    Fluxion_Scene_Destroy(scene);
}

void TheProjectionMapsThePlanesWhereItSays(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Eye");
    FluxionCamera camera{};
    camera.fovYRadians = 1.0472f;
    camera.nearPlane = 1.0f;
    camera.farPlane = 10.0f;
    Fluxion_GameObject_AddComponent(scene, object, Fluxion_Camera_TypeId(), &camera);
    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionMat4 view = Fluxion_Mat4_Identity();
    FluxionMat4 projection = Fluxion_Mat4_Identity();
    TEST_CHECK(ctx, Fluxion_Scene_GatherCamera(scene, 1.0f, &view, &projection));

    // A point on the near plane (z = -near) comes out at depth -1 after
    // the divide; the far plane at +1. The numbers, not the shape: this
    // is the convention every backend's viewport transform assumes.
    const f32 nearClip = projection.m[2][2] * -1.0f + projection.m[2][3];
    const f32 nearW = projection.m[3][2] * -1.0f;
    TEST_CHECK(ctx, Near(nearClip / nearW, -1.0f, 0.001f));

    const f32 farClip = projection.m[2][2] * -10.0f + projection.m[2][3];
    const f32 farW = projection.m[3][2] * -10.0f;
    TEST_CHECK(ctx, Near(farClip / farW, 1.0f, 0.001f));

    Fluxion_Scene_Destroy(scene);
}

void TwoCamerasGiveOneAnswer(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    for (int i = 0; i < 2; ++i)
    {
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Eye");
        FluxionCamera camera{};
        camera.fovYRadians = 1.0f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.0f;
        Fluxion_GameObject_AddComponent(scene, object, Fluxion_Camera_TypeId(), &camera);
    }
    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionMat4 view = Fluxion_Mat4_Identity();
    FluxionMat4 projection = Fluxion_Mat4_Identity();
    TEST_CHECK(ctx, Fluxion_Scene_GatherCamera(scene, 1.0f, &view, &projection));

    Fluxion_Scene_Destroy(scene);
}

} // namespace

void Test_SceneCamera_Run(TestContext& ctx)
{
    ASceneWithNoCameraSaysSo(ctx);
    TheViewUndoesWhereTheCameraStands(ctx);
    TheProjectionMapsThePlanesWhereItSays(ctx);
    TwoCamerasGiveOneAnswer(ctx);
}
