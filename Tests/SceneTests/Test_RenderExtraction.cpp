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

#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Scene/Camera.h>
#include <Fluxion/Scene/Light.h>
#include <Fluxion/Scene/MeshRenderer.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneSerialization.h>

// The scene, once a frame, in the shape the renderer takes.
//
// What is checked here is the WALK: that one pass over the world answers
// the questions three separate calls used to, that an object whose
// assets are not there yet is left out rather than half drawn, and that
// what a component points at survives being saved.
//
// Whether a ready mesh really becomes a drawn one is a question for a
// device, and the sample answers it every time it starts: 784 objects,
// three draw calls.

namespace
{

void ASceneWithNoCameraIsNotAFrame(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionRenderWorld world;
    TEST_CHECK(ctx, Fluxion_RenderWorld_Init(&world));

    Fluxion_Scene_CreateGameObject(scene, "NotACamera");
    Fluxion_Scene_Tick(scene, 0.0f);

    // False, and everything else still filled in: a world with no eye is
    // a world, it is just not a picture.
    TEST_CHECK(ctx, !Fluxion_Scene_ExtractRenderWorld(scene, 1.0f, &world));
    TEST_CHECK(ctx, world.objectCount == 0);
    TEST_CHECK(ctx, !world.camera.valid);

    Fluxion_RenderWorld_Shutdown(&world);
    Fluxion_Scene_Destroy(scene);
}

void OneWalkAnswersWhatThreeCallsDid(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle eye = Fluxion_Scene_CreateGameObject(scene, "Eye");
    Fluxion_GameObject_SetLocalPosition(scene, eye, FluxionVec3{ 0.0f, 1.0f, 5.0f });
    FluxionCamera camera{};
    camera.fovYRadians = 1.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    Fluxion_GameObject_AddComponent(scene, eye, Fluxion_Camera_TypeId(), &camera);

    FluxionGameObjectHandle sun = Fluxion_Scene_CreateGameObject(scene, "Sun");
    FluxionDirectionalLight light{};
    light.color = FluxionVec3{ 10.0f, 10.0f, 10.0f };
    Fluxion_GameObject_AddComponent(scene, sun, Fluxion_DirectionalLight_TypeId(), &light);

    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionRenderWorld world;
    TEST_CHECK(ctx, Fluxion_RenderWorld_Init(&world));

    TEST_CHECK(ctx, Fluxion_Scene_ExtractRenderWorld(scene, 1.5f, &world));
    TEST_CHECK(ctx, world.camera.valid);
    TEST_CHECK(ctx, world.lightCount == 1);
    TEST_CHECK(ctx, world.lightsInWorld == 1);

    // The camera's own position lands on the origin of camera space --
    // the same thing Fluxion_Scene_GatherCamera answers, reached the new
    // way.
    const FluxionMat4& view = world.camera.view;
    const f32 x = view.m[0][0] * 0.0f + view.m[0][1] * 1.0f + view.m[0][2] * 5.0f + view.m[0][3];
    const f32 y = view.m[1][0] * 0.0f + view.m[1][1] * 1.0f + view.m[1][2] * 5.0f + view.m[1][3];
    const f32 z = view.m[2][0] * 0.0f + view.m[2][1] * 1.0f + view.m[2][2] * 5.0f + view.m[2][3];
    TEST_CHECK(ctx, x > -0.001f && x < 0.001f);
    TEST_CHECK(ctx, y > -0.001f && y < 0.001f);
    TEST_CHECK(ctx, z > -0.001f && z < 0.001f);

    // Clearing and extracting again gives the same answer rather than
    // twice as many lights: the world is filled from scratch each time.
    TEST_CHECK(ctx, Fluxion_Scene_ExtractRenderWorld(scene, 1.5f, &world));
    TEST_CHECK(ctx, world.lightCount == 1);

    Fluxion_RenderWorld_Shutdown(&world);
    Fluxion_Scene_Destroy(scene);
}

void AnObjectWaitingForItsAssetsIsLeftOut(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle eye = Fluxion_Scene_CreateGameObject(scene, "Eye");
    FluxionCamera camera{};
    camera.fovYRadians = 1.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    Fluxion_GameObject_AddComponent(scene, eye, Fluxion_Camera_TypeId(), &camera);

    // A renderer pointing at assets no database has ever heard of, which
    // is the same shape as one whose assets have not finished loading.
    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Waiting");
    FluxionMeshRenderer renderer{};
    renderer.mesh.asset = Fluxion_UUID_Generate();
    renderer.material.asset = Fluxion_UUID_Generate();
    TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, object, Fluxion_MeshRenderer_TypeId(), &renderer) != nullptr);

    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionRenderWorld world;
    TEST_CHECK(ctx, Fluxion_RenderWorld_Init(&world));

    // The frame is drawable -- there is a camera -- and the object is
    // simply not in it. Not half drawn, and not drawn with whatever mesh
    // happened to be loaded.
    TEST_CHECK(ctx, Fluxion_Scene_ExtractRenderWorld(scene, 1.0f, &world));
    TEST_CHECK(ctx, world.objectCount == 0);

    Fluxion_RenderWorld_Shutdown(&world);
    Fluxion_Scene_Destroy(scene);
}

void WhatAComponentPointsAtSurvivesBeingSaved(TestContext& ctx)
{
    const FluxionUUID meshId = Fluxion_UUID_Generate();
    const FluxionUUID materialId = Fluxion_UUID_Generate();

    FluxionSceneHandle source = Fluxion_Scene_Create();
    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(source, "Thing");

    FluxionMeshRenderer renderer{};
    renderer.mesh.asset = meshId;
    renderer.material.asset = materialId;
    renderer.layerMask = 0x00000003u;
    Fluxion_GameObject_AddComponent(source, object, Fluxion_MeshRenderer_TypeId(), &renderer);
    Fluxion_Scene_Tick(source, 0.0f);

    const FluxionUUID objectId = Fluxion_GameObject_GetUUID(source, object);

    usize size = 0;
    u8* bytes = Fluxion_Scene_SaveToBuffer(source, 4096, &size);
    TEST_CHECK(ctx, bytes != nullptr);
    if (bytes == nullptr) return;
    Fluxion_Scene_Destroy(source);

    FluxionSceneHandle loaded = Fluxion_Scene_Create();
    {
        FluxionStream reader;
        Fluxion_MemoryStream_InitReader(&reader, bytes, size);
        TEST_CHECK(ctx, Fluxion_Scene_Load(loaded, &reader));
    }
    Fluxion_Scene_FreeBuffer(bytes, size);

    FluxionEntityHandle reloadedObject = Fluxion_Scene_FindByUUID(loaded, objectId);
    const FluxionMeshRenderer* reloaded =
        (const FluxionMeshRenderer*)Fluxion_GameObject_GetComponent(loaded, reloadedObject, Fluxion_MeshRenderer_TypeId());
    TEST_CHECK(ctx, reloaded != nullptr);
    if (reloaded != nullptr)
    {
        // Sixteen bytes that mean the same thing in every run -- which is
        // the whole reason a component points at an id rather than at a
        // handle.
        TEST_CHECK(ctx, Fluxion_UUID_Equals(reloaded->mesh.asset, meshId));
        TEST_CHECK(ctx, Fluxion_UUID_Equals(reloaded->material.asset, materialId));
        TEST_CHECK(ctx, reloaded->layerMask == 0x00000003u);
    }

    Fluxion_Scene_Destroy(loaded);
}

} // namespace

void Test_RenderExtraction_Run(TestContext& ctx)
{
    ASceneWithNoCameraIsNotAFrame(ctx);
    OneWalkAnswersWhatThreeCallsDid(ctx);
    AnObjectWaitingForItsAssetsIsLeftOut(ctx);
    WhatAComponentPointsAtSurvivesBeingSaved(ctx);
}
