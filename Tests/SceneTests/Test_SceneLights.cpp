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

#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/Assets/AssetRef.h>
#include <Fluxion/Scene/Light.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneSerialization.h>

#include <cmath>
#include <cstring>

// Turning a scene's lights into the flat list a renderer takes.
//
// The interesting part is not the copying. It is that where a light is
// and which way it faces are NOT on the light -- they come from the
// object's transform -- so this is really a check that the two halves are
// read together and that neither is invented.

namespace
{

bool Near(f32 value, f32 expected, f32 tolerance)
{
    const f32 difference = value - expected;
    return (difference < 0.0f ? -difference : difference) <= tolerance;
}

void ALightTakesItsPlaceFromTheObject(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();
    TEST_CHECK(ctx, Fluxion_Scene_IsValid(scene));

    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Lamp");
    Fluxion_GameObject_SetLocalPosition(scene, object, FluxionVec3{ 3.0f, 4.0f, 5.0f });

    FluxionPointLight lamp{};
    lamp.color = FluxionVec3{ 1.0f, 2.0f, 3.0f };
    lamp.range = 12.0f;
    TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, object, Fluxion_PointLight_TypeId(), &lamp) != nullptr);

    // The world matrices have to have been worked out, or the light is at
    // wherever the matrix was left.
    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionRenderLight lights[4];
    const u32 count = Fluxion_Scene_GatherLights(scene, lights, 4);
    TEST_CHECK(ctx, count == 1);

    if (count == 1)
    {
        TEST_CHECK(ctx, lights[0].type == FLUXION_RENDER_LIGHT_POINT);
        TEST_CHECK(ctx, Near(lights[0].position.x, 3.0f, 0.001f));
        TEST_CHECK(ctx, Near(lights[0].position.y, 4.0f, 0.001f));
        TEST_CHECK(ctx, Near(lights[0].position.z, 5.0f, 0.001f));
        TEST_CHECK(ctx, Near(lights[0].range, 12.0f, 0.001f));
        TEST_CHECK(ctx, Near(lights[0].color.y, 2.0f, 0.001f));

        // Unrotated, an object faces its negative Z -- the same way a
        // camera looks.
        TEST_CHECK(ctx, Near(lights[0].direction.z, -1.0f, 0.001f));
    }

    // Moving the object moves the light, which is the whole reason the
    // position is not stored on the component.
    Fluxion_GameObject_SetLocalPosition(scene, object, FluxionVec3{ -1.0f, 0.0f, 0.0f });
    Fluxion_Scene_Tick(scene, 0.0f);

    TEST_CHECK(ctx, Fluxion_Scene_GatherLights(scene, lights, 4) == 1);
    TEST_CHECK(ctx, Near(lights[0].position.x, -1.0f, 0.001f));

    Fluxion_Scene_Destroy(scene);
}

void AChildLightInheritsWhereItsParentIs(TestContext& ctx)
{
    // The reason the world matrix is read rather than the local position:
    // a lamp bolted to a moving thing moves with it, and nothing on the
    // lamp says so.
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle parent = Fluxion_Scene_CreateGameObject(scene, "Vehicle");
    Fluxion_GameObject_SetLocalPosition(scene, parent, FluxionVec3{ 10.0f, 0.0f, 0.0f });

    FluxionGameObjectHandle child = Fluxion_Scene_CreateGameObject(scene, "Headlight");
    Fluxion_GameObject_SetParent(scene, child, parent);
    Fluxion_GameObject_SetLocalPosition(scene, child, FluxionVec3{ 0.0f, 1.0f, 0.0f });

    FluxionSpotLight headlight{};
    headlight.color = FluxionVec3{ 5.0f, 5.0f, 5.0f };
    headlight.range = 20.0f;
    headlight.innerConeAngle = 0.2f;
    headlight.outerConeAngle = 0.4f;
    TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, child, Fluxion_SpotLight_TypeId(), &headlight) != nullptr);

    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionRenderLight lights[4];
    TEST_CHECK(ctx, Fluxion_Scene_GatherLights(scene, lights, 4) == 1);
    TEST_CHECK(ctx, Near(lights[0].position.x, 10.0f, 0.001f));
    TEST_CHECK(ctx, Near(lights[0].position.y, 1.0f, 0.001f));

    // The cosines, taken on this side. Larger angle, smaller cosine --
    // and the inner one must not be below the outer, or the smooth step
    // between them comes out inverted.
    TEST_CHECK(ctx, Near(lights[0].innerConeCos, std::cos(0.2f), 0.001f));
    TEST_CHECK(ctx, Near(lights[0].outerConeCos, std::cos(0.4f), 0.001f));
    TEST_CHECK(ctx, lights[0].innerConeCos >= lights[0].outerConeCos);

    Fluxion_Scene_Destroy(scene);
}

void ConeAnglesTheWrongWayRoundAreMadeHarmless(TestContext& ctx)
{
    // An inner cone wider than its outer one gives a negative span, and
    // the transition across it would come out inverted -- a spot light
    // dark in the middle and bright at the rim, which reads as a shader
    // bug rather than as two numbers the wrong way round.
    FluxionSceneHandle scene = Fluxion_Scene_Create();
    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Backwards");

    FluxionSpotLight backwards{};
    backwards.color = FluxionVec3{ 1.0f, 1.0f, 1.0f };
    backwards.range = 5.0f;
    backwards.innerConeAngle = 0.9f; // wider than the outer, which is wrong
    backwards.outerConeAngle = 0.3f;
    Fluxion_GameObject_AddComponent(scene, object, Fluxion_SpotLight_TypeId(), &backwards);

    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionRenderLight lights[4];
    TEST_CHECK(ctx, Fluxion_Scene_GatherLights(scene, lights, 4) == 1);
    TEST_CHECK(ctx, lights[0].innerConeCos >= lights[0].outerConeCos);

    Fluxion_Scene_Destroy(scene);
}

void TheCountIsHowManyThereAreNotHowManyFitted(TestContext& ctx)
{
    // The same shape as the asset gather, and the same trap: a count that
    // stopped at the buffer's end would make asking twice give two
    // different answers, and the whole point of the count is to size the
    // next call.
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    for (u32 i = 0; i < 5; ++i)
    {
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Lamp");
        FluxionPointLight lamp{};
        lamp.color = FluxionVec3{ 1.0f, 1.0f, 1.0f };
        lamp.range = 1.0f;
        Fluxion_GameObject_AddComponent(scene, object, Fluxion_PointLight_TypeId(), &lamp);
    }

    Fluxion_Scene_Tick(scene, 0.0f);

    // Asked with no room at all.
    TEST_CHECK(ctx, Fluxion_Scene_GatherLights(scene, nullptr, 0) == 5);

    // Asked with room for two: still five, and exactly two written.
    FluxionRenderLight lights[2];
    std::memset(lights, 0, sizeof(lights));
    TEST_CHECK(ctx, Fluxion_Scene_GatherLights(scene, lights, 2) == 5);
    TEST_CHECK(ctx, lights[0].type == FLUXION_RENDER_LIGHT_POINT);
    TEST_CHECK(ctx, lights[1].type == FLUXION_RENDER_LIGHT_POINT);

    Fluxion_Scene_Destroy(scene);
}

void AllThreeKindsComeBackTogether(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle sunObject = Fluxion_Scene_CreateGameObject(scene, "Sun");
    FluxionDirectionalLight sun{};
    sun.color = FluxionVec3{ 1.0f, 1.0f, 1.0f };
    Fluxion_GameObject_AddComponent(scene, sunObject, Fluxion_DirectionalLight_TypeId(), &sun);

    FluxionGameObjectHandle lampObject = Fluxion_Scene_CreateGameObject(scene, "Lamp");
    FluxionPointLight lamp{};
    lamp.color = FluxionVec3{ 1.0f, 1.0f, 1.0f };
    lamp.range = 3.0f;
    Fluxion_GameObject_AddComponent(scene, lampObject, Fluxion_PointLight_TypeId(), &lamp);

    FluxionGameObjectHandle torchObject = Fluxion_Scene_CreateGameObject(scene, "Torch");
    FluxionSpotLight torch{};
    torch.color = FluxionVec3{ 1.0f, 1.0f, 1.0f };
    torch.range = 4.0f;
    torch.innerConeAngle = 0.1f;
    torch.outerConeAngle = 0.2f;
    Fluxion_GameObject_AddComponent(scene, torchObject, Fluxion_SpotLight_TypeId(), &torch);

    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionRenderLight lights[8];
    const u32 count = Fluxion_Scene_GatherLights(scene, lights, 8);
    TEST_CHECK(ctx, count == 3);

    bool sawDirectional = false;
    bool sawPoint = false;
    bool sawSpot = false;
    for (u32 i = 0; i < count; ++i)
    {
        if (lights[i].type == FLUXION_RENDER_LIGHT_DIRECTIONAL) sawDirectional = true;
        if (lights[i].type == FLUXION_RENDER_LIGHT_POINT) sawPoint = true;
        if (lights[i].type == FLUXION_RENDER_LIGHT_SPOT) sawSpot = true;
    }
    TEST_CHECK(ctx, sawDirectional && sawPoint && sawSpot);

    // An object with no light on it contributes none, which is what keeps
    // the count meaningful in a scene that is mostly not lights.
    Fluxion_Scene_CreateGameObject(scene, "Nothing");
    Fluxion_Scene_Tick(scene, 0.0f);
    TEST_CHECK(ctx, Fluxion_Scene_GatherLights(scene, lights, 8) == 3);

    Fluxion_Scene_Destroy(scene);
}

// A made-up id, used only so that "which id came back" is a question
// with a wrong answer available.
FluxionUUID SkyId()
{
    FluxionUUID id{};
    for (u32 i = 0; i < 16; ++i) id.bytes[i] = (u8)(0xA0 + i);
    return id;
}

void ASceneWithNoSkyIsNotAnError(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    // A light, so that "nothing found" cannot come from an empty scene.
    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Sun");
    FluxionDirectionalLight sun{};
    sun.color = FluxionVec3{ 1.0f, 1.0f, 1.0f };
    Fluxion_GameObject_AddComponent(scene, object, Fluxion_DirectionalLight_TypeId(), &sun);
    Fluxion_Scene_Tick(scene, 0.0f);

    // Filled in first, and checked afterwards: a false return that had
    // written something anyway would leave a caller holding a sky it was
    // told it did not have.
    FluxionEnvironmentLight environment{};
    environment.intensity = 12345.0f;

    TEST_CHECK(ctx, !Fluxion_Scene_GatherEnvironment(scene, &environment));
    TEST_CHECK(ctx, Near(environment.intensity, 12345.0f, 0.001f));

    Fluxion_Scene_Destroy(scene);
}

void TheSkyComesBackWithItsAssetAndItsIntensity(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Sky");

    FluxionEnvironmentLight sky{};
    sky.environment.asset = SkyId();
    sky.intensity = 0.75f;
    TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, object, Fluxion_EnvironmentLight_TypeId(), &sky) != nullptr);

    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionEnvironmentLight found{};
    TEST_CHECK(ctx, Fluxion_Scene_GatherEnvironment(scene, &found));
    TEST_CHECK(ctx, Fluxion_UUID_Equals(found.environment.asset, SkyId()));
    TEST_CHECK(ctx, Near(found.intensity, 0.75f, 0.001f));

    // Where the object is does not reach the sky. An environment is what
    // surrounds everything; moving the object that carries it would
    // otherwise look like it should move the world.
    Fluxion_GameObject_SetLocalPosition(scene, object, FluxionVec3{ 100.0f, 0.0f, 0.0f });
    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionEnvironmentLight moved{};
    TEST_CHECK(ctx, Fluxion_Scene_GatherEnvironment(scene, &moved));
    TEST_CHECK(ctx, Fluxion_UUID_Equals(moved.environment.asset, SkyId()));

    Fluxion_Scene_Destroy(scene);
}

void ASkyAndALampOnOneObjectStayApart(TestContext& ctx)
{
    // Both on one object, so the two of them share a block. A gather that
    // read its column by position rather than by type would hand back the
    // wrong bytes here and nowhere else -- on separate objects the two
    // blocks each hold one thing, and the mistake would come out right.
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "SkyAndLamp");

    FluxionEnvironmentLight sky{};
    sky.environment.asset = SkyId();
    sky.intensity = 0.5f;
    Fluxion_GameObject_AddComponent(scene, object, Fluxion_EnvironmentLight_TypeId(), &sky);

    FluxionPointLight lamp{};
    lamp.color = FluxionVec3{ 7.0f, 8.0f, 9.0f };
    lamp.range = 4.0f;
    Fluxion_GameObject_AddComponent(scene, object, Fluxion_PointLight_TypeId(), &lamp);

    Fluxion_Scene_Tick(scene, 0.0f);

    FluxionEnvironmentLight found{};
    TEST_CHECK(ctx, Fluxion_Scene_GatherEnvironment(scene, &found));
    TEST_CHECK(ctx, Fluxion_UUID_Equals(found.environment.asset, SkyId()));
    TEST_CHECK(ctx, Near(found.intensity, 0.5f, 0.001f));

    // And the lamp is still a lamp, with its own numbers.
    FluxionRenderLight lights[4];
    const u32 count = Fluxion_Scene_GatherLights(scene, lights, 4);
    TEST_CHECK(ctx, count == 1);
    if (count == 1)
    {
        TEST_CHECK(ctx, lights[0].type == FLUXION_RENDER_LIGHT_POINT);
        TEST_CHECK(ctx, Near(lights[0].color.x, 7.0f, 0.001f));
        TEST_CHECK(ctx, Near(lights[0].range, 4.0f, 0.001f));
    }

    Fluxion_Scene_Destroy(scene);
}

void TwoSkiesGiveOneAnswer(TestContext& ctx)
{
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    for (u32 i = 0; i < 2; ++i)
    {
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Sky");
        FluxionEnvironmentLight sky{};
        sky.environment.asset = SkyId();
        sky.intensity = 1.0f + (f32)i;
        Fluxion_GameObject_AddComponent(scene, object, Fluxion_EnvironmentLight_TypeId(), &sky);
    }

    Fluxion_Scene_Tick(scene, 0.0f);

    // One, not two and not none. Which one is not promised -- what is
    // promised is that the caller gets a usable answer and a warning it
    // can act on, rather than the second one quietly overwriting the
    // first.
    FluxionEnvironmentLight found{};
    TEST_CHECK(ctx, Fluxion_Scene_GatherEnvironment(scene, &found));
    TEST_CHECK(ctx, Fluxion_UUID_Equals(found.environment.asset, SkyId()));
    TEST_CHECK(ctx, Near(found.intensity, 1.0f, 0.001f) || Near(found.intensity, 2.0f, 0.001f));

    Fluxion_Scene_Destroy(scene);
}

void TheSkysAssetIsOneTheBuildWillFind(TestContext& ctx)
{
    // The reason the reference is REFLECTED rather than just stored.
    // Whoever packages a build walks the fields of every component and
    // asks which of them are asset references; a sky whose field was not
    // declared would be left out of the package, and the failure would be
    // a black sky in a shipped game and nothing at all before it.
    FluxionSceneHandle scene = Fluxion_Scene_Create();

    FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "Sky");
    FluxionEnvironmentLight sky{};
    sky.environment.asset = SkyId();
    sky.intensity = 1.0f;
    Fluxion_GameObject_AddComponent(scene, object, Fluxion_EnvironmentLight_TypeId(), &sky);

    FluxionUUID assets[4];
    const u32 count = Fluxion_Scene_GatherAssetReferences(scene, assets, 4);
    TEST_CHECK(ctx, count == 1);
    if (count == 1) TEST_CHECK(ctx, Fluxion_UUID_Equals(assets[0], SkyId()));

    Fluxion_Scene_Destroy(scene);
}

} // namespace

void Test_SceneLights_Run(TestContext& ctx)
{
    ALightTakesItsPlaceFromTheObject(ctx);
    AChildLightInheritsWhereItsParentIs(ctx);
    ConeAnglesTheWrongWayRoundAreMadeHarmless(ctx);
    TheCountIsHowManyThereAreNotHowManyFitted(ctx);
    AllThreeKindsComeBackTogether(ctx);
    ASceneWithNoSkyIsNotAnError(ctx);
    TheSkyComesBackWithItsAssetAndItsIntensity(ctx);
    ASkyAndALampOnOneObjectStayApart(ctx);
    TwoSkiesGiveOneAnswer(ctx);
    TheSkysAssetIsOneTheBuildWillFind(ctx);
}
