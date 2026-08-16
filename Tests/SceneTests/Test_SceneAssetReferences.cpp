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

#include <Fluxion/Assets/AssetRef.h>
#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneSerialization.h>

#include <cstring>

// Which assets a scene will reach for.
//
// A scene does not announce that anywhere -- it is spread across the
// fields of its objects' components -- so a build that has to know cannot
// ask the scene, it has to look. This is that looking.

namespace
{

struct TestMeshRenderer
{
    static constexpr auto Name = "TestMeshRenderer";
    FluxionAssetRef mesh;
    FluxionAssetRef material;
    u32 layer;
};

// A component with no asset field at all, so a pass that simply read
// every field of every component would be caught rather than agreed with.
struct TestSpin
{
    static constexpr auto Name = "TestSpin";
    f32 speed;
};

// A field that CLAIMS to be a reference but is not the size of one. It
// must be passed over: reading it as one would fold whatever sits next to
// it into an id.
struct TestMislabelled
{
    static constexpr auto Name = "TestMislabelled";
    u32 notAReference;
};

FluxionPropertyInfo* RendererProperties()
{
    static FluxionPropertyInfo properties[] = {
        FLUXION_REFLECT_PROPERTY(TestMeshRenderer, mesh, Fluxion_AssetRef_TypeId(), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(TestMeshRenderer, material, Fluxion_AssetRef_TypeId(), FLUXION_PROPERTY_FLAG_NONE),
        FLUXION_REFLECT_PROPERTY(TestMeshRenderer, layer, FLUXION_TYPE_ID_OF(u32), FLUXION_PROPERTY_FLAG_NONE),
    };
    return properties;
}

FluxionPropertyInfo* SpinProperties()
{
    static FluxionPropertyInfo properties[] = {
        FLUXION_REFLECT_PROPERTY(TestSpin, speed, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
    };
    return properties;
}

FluxionPropertyInfo* MislabelledProperties()
{
    static FluxionPropertyInfo properties[] = {
        FLUXION_REFLECT_PROPERTY(TestMislabelled, notAReference, Fluxion_AssetRef_TypeId(), FLUXION_PROPERTY_FLAG_NONE),
    };
    return properties;
}

FluxionTypeInfo MakeTypeInfo(const char* name, usize size, FluxionPropertyInfo* properties, usize count)
{
    FluxionTypeInfo info;
    info.name = Fluxion_StringView_FromCStr(name);
    info.id = Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(name));
    info.kind = FLUXION_TYPE_KIND_STRUCT;
    info.size = size;
    info.version = 1;
    info.members = Fluxion_Span_Make(properties, count, sizeof(FluxionPropertyInfo));
    info.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));
    return info;
}

void RegisterTypes()
{
    static FluxionTypeInfo renderer = MakeTypeInfo(TestMeshRenderer::Name, sizeof(TestMeshRenderer), RendererProperties(), 3);
    static FluxionTypeInfo spin = MakeTypeInfo(TestSpin::Name, sizeof(TestSpin), SpinProperties(), 1);
    static FluxionTypeInfo mislabelled = MakeTypeInfo(TestMislabelled::Name, sizeof(TestMislabelled), MislabelledProperties(), 1);

    Fluxion_Reflection_RegisterType(&renderer);
    Fluxion_Reflection_RegisterType(&spin);
    Fluxion_Reflection_RegisterType(&mislabelled);
}

FluxionTypeId RendererType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestMeshRenderer::Name)); }
FluxionTypeId SpinType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestSpin::Name)); }
FluxionTypeId MislabelledType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestMislabelled::Name)); }

bool Contains(const FluxionUUID* assets, u32 count, FluxionUUID wanted)
{
    for (u32 i = 0; i < count; ++i)
    {
        if (Fluxion_UUID_Equals(assets[i], wanted)) return true;
    }
    return false;
}

} // namespace

void Test_SceneAssetReferences_Run(TestContext& ctx)
{
    std::fprintf(stderr, "  Test_SceneAssetReferences\n");

    RegisterTypes();

    FluxionSceneHandle scene = Fluxion_Scene_Create();
    TEST_CHECK(ctx, Fluxion_Scene_IsValid(scene));

    const FluxionUUID meshAsset = Fluxion_UUID_Generate();
    const FluxionUUID materialAsset = Fluxion_UUID_Generate();
    const FluxionUUID otherMaterial = Fluxion_UUID_Generate();

    // Two objects sharing a mesh, one of them nested, so the walk has to
    // go down as well as across -- and one asset used twice has to come
    // back once.
    FluxionGameObjectHandle root = Fluxion_Scene_CreateGameObject(scene, "Root");
    FluxionGameObjectHandle child = Fluxion_Scene_CreateGameObject(scene, "Child");
    Fluxion_GameObject_SetParent(scene, child, root);

    {
        TestMeshRenderer value{};
        value.mesh = FluxionAssetRef{ meshAsset };
        value.material = FluxionAssetRef{ materialAsset };
        value.layer = 3;
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, root, RendererType(), &value) != nullptr);
    }

    {
        TestMeshRenderer value{};
        value.mesh = FluxionAssetRef{ meshAsset };
        value.material = FluxionAssetRef{ otherMaterial };
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, child, RendererType(), &value) != nullptr);
    }

    {
        TestSpin value{ 1.0f };
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, child, SpinType(), &value) != nullptr);
    }

    // A field claiming to be a reference while being the wrong size, set
    // to something that would look like a plausible id if it were read.
    {
        TestMislabelled value{ 0xDEADBEEFu };
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, root, MislabelledType(), &value) != nullptr);
    }

    // A third object whose reference was never filled in. Nil points at
    // nothing, and nothing is not an asset to package.
    FluxionGameObjectHandle empty = Fluxion_Scene_CreateGameObject(scene, "Empty");
    {
        TestMeshRenderer value{};
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, empty, RendererType(), &value) != nullptr);
    }

    FluxionUUID assets[16];
    const u32 count = Fluxion_Scene_GatherAssetReferences(scene, assets, 16);

    TEST_CHECK(ctx, count == 3);
    TEST_CHECK(ctx, Contains(assets, count, meshAsset));
    TEST_CHECK(ctx, Contains(assets, count, materialAsset));
    TEST_CHECK(ctx, Contains(assets, count, otherMaterial));

    // Asking with nowhere to put the answer still says how many there
    // are, which is how a caller finds out what size buffer to bring.
    TEST_CHECK(ctx, Fluxion_Scene_GatherAssetReferences(scene, nullptr, 0) == 3);

    // Too small a buffer fills what it can and still reports the real
    // number -- silently returning two would be a build quietly missing
    // an asset.
    FluxionUUID tooSmall[2];
    const u32 reported = Fluxion_Scene_GatherAssetReferences(scene, tooSmall, 2);
    TEST_CHECK(ctx, reported == 3);

    Fluxion_Scene_Destroy(scene);

    FluxionSceneHandle emptyScene = Fluxion_Scene_Create();
    TEST_CHECK(ctx, Fluxion_Scene_GatherAssetReferences(emptyScene, assets, 16) == 0);
    Fluxion_Scene_Destroy(emptyScene);

    Fluxion_Reflection_UnregisterType(RendererType());
    Fluxion_Reflection_UnregisterType(SpinType());
    Fluxion_Reflection_UnregisterType(MislabelledType());
}
