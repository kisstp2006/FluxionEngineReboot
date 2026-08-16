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

#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Scene/Prefab.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneSerialization.h>
#include <Fluxion/Scene/Transform.h>

#include <cstdlib>
#include <cstring>

// Writing a scene down and reading it back.
//
// Every mistake in this area is quiet. A reference written as a raw
// handle still loads -- it just names a different object, or none, and
// the scene looks almost right. A property left out is a value silently
// back at its default. So the checks below compare against numbers
// written down in advance, and the strongest of them compares a scene
// against itself: saved, loaded, saved again, byte for byte.

namespace
{

struct TestStats
{
    static constexpr auto Name = "TestSaveStats";
    i32 health;
    f32 speed;
};

// One field naming another object, and one that must not be written at
// all. The handle field is what proves the reference path: its raw bytes
// are an index and a generation, which after a reload name whatever
// happens to sit there.
struct TestLink
{
    static constexpr auto Name = "TestSaveLink";
    FluxionEntityHandle target;
    i32 kept;
    i32 derived;
};

FluxionTypeId StatsType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestStats::Name)); }
FluxionTypeId LinkType()  { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestLink::Name)); }

void RegisterTypes()
{
    static FluxionPropertyInfo statsProperties[2];
    static FluxionPropertyInfo linkProperties[3];
    static FluxionTypeInfo stats;
    static FluxionTypeInfo link;

    {
        const FluxionPropertyInfo described[2] = {
            FLUXION_REFLECT_PROPERTY(TestStats, health, FLUXION_TYPE_ID_OF(i32), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestStats, speed, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        };
        std::memcpy(statsProperties, described, sizeof(described));
    }
    {
        const FluxionPropertyInfo described[3] = {
            FLUXION_REFLECT_PROPERTY(TestLink, target, FLUXION_TYPE_ID_OF(FluxionEntityHandle), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestLink, kept, FLUXION_TYPE_ID_OF(i32), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestLink, derived, FLUXION_TYPE_ID_OF(i32), FLUXION_PROPERTY_FLAG_TRANSIENT),
        };
        std::memcpy(linkProperties, described, sizeof(described));
    }

    stats.name = Fluxion_StringView_FromCStr(TestStats::Name);
    stats.id = StatsType();
    stats.kind = FLUXION_TYPE_KIND_STRUCT;
    stats.size = sizeof(TestStats);
    stats.version = 1;
    stats.members = Fluxion_Span_Make(statsProperties, 2, sizeof(FluxionPropertyInfo));
    stats.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));

    link.name = Fluxion_StringView_FromCStr(TestLink::Name);
    link.id = LinkType();
    link.kind = FLUXION_TYPE_KIND_STRUCT;
    link.size = sizeof(TestLink);
    link.version = 1;
    link.members = Fluxion_Span_Make(linkProperties, 3, sizeof(FluxionPropertyInfo));
    link.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));

    Fluxion_Reflection_RegisterType(&stats);
    Fluxion_Reflection_RegisterType(&link);
}

bool SameEntity(FluxionEntityHandle a, FluxionEntityHandle b)
{
    return a.index == b.index && a.generation == b.generation;
}

} // namespace

void Test_SceneSerialization_Run(TestContext& ctx)
{
    RegisterTypes();

    // --- What is written comes back ---------------------------------------

    {
        FluxionSceneHandle source = Fluxion_Scene_Create();

        FluxionEntityHandle parent = Fluxion_Scene_CreateGameObject(source, "parent");
        FluxionEntityHandle child = Fluxion_Scene_CreateGameObject(source, "child");
        Fluxion_GameObject_SetParent(source, child, parent);

        {
            FluxionVec3 position = { 1.0f, 2.0f, 3.0f };
            Fluxion_GameObject_SetLocalPosition(source, parent, position);
        }
        {
            TestStats seed = { 77, 4.5f };
            Fluxion_GameObject_AddComponent(source, child, StatsType(), &seed);
        }

        const FluxionUUID parentId = Fluxion_GameObject_GetUUID(source, parent);
        const FluxionUUID childId = Fluxion_GameObject_GetUUID(source, child);

        usize size = 0;
        u8* bytes = Fluxion_Scene_SaveToBuffer(source, 64, &size);
        TEST_CHECK(ctx, bytes != nullptr && size > 0);
        if (bytes == nullptr) return;

        // Deliberately started smaller than a scene can be, so the growing
        // is exercised rather than merely available.
        Fluxion_Scene_Destroy(source);

        FluxionSceneHandle loaded = Fluxion_Scene_Create();
        {
            FluxionStream reader;
            Fluxion_MemoryStream_InitReader(&reader, bytes, size);
            TEST_CHECK(ctx, Fluxion_Scene_Load(loaded, &reader));
        }

        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(loaded) == 2);

        // The objects are found by the ids they had, which is the whole
        // point of writing ids rather than handles.
        FluxionEntityHandle loadedParent = Fluxion_Scene_FindByUUID(loaded, parentId);
        FluxionEntityHandle loadedChild = Fluxion_Scene_FindByUUID(loaded, childId);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(loaded, loadedParent));
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(loaded, loadedChild));

        TEST_CHECK(ctx, std::strcmp(Fluxion_GameObject_GetName(loaded, loadedParent), "parent") == 0);
        TEST_CHECK(ctx, SameEntity(Fluxion_GameObject_GetParent(loaded, loadedChild), loadedParent));

        {
            const FluxionVec3 position = Fluxion_GameObject_GetLocalPosition(loaded, loadedParent);
            TEST_CHECK(ctx, position.x == 1.0f && position.y == 2.0f && position.z == 3.0f);
        }
        {
            const TestStats* stats = (const TestStats*)Fluxion_GameObject_GetComponent(loaded, loadedChild, StatsType());
            TEST_CHECK(ctx, stats != nullptr);
            TEST_CHECK(ctx, stats != nullptr && stats->health == 77 && stats->speed == 4.5f);
        }

        // Saved again, and the same bytes come out. This is the check that
        // catches what the ones above cannot: anything read into the wrong
        // place, or left unread, shows up here as a difference even when
        // every value that was looked at happened to be right.
        {
            usize secondSize = 0;
            u8* second = Fluxion_Scene_SaveToBuffer(loaded, size, &secondSize);
            TEST_CHECK(ctx, second != nullptr);
            TEST_CHECK(ctx, second != nullptr && secondSize == size);
            TEST_CHECK(ctx, second != nullptr && secondSize == size && std::memcmp(second, bytes, size) == 0);
            Fluxion_Scene_FreeBuffer(second, size);
        }

        Fluxion_Scene_FreeBuffer(bytes, size);
        Fluxion_Scene_Destroy(loaded);
    }

    // --- Objects naming each other ----------------------------------------

    {
        FluxionSceneHandle source = Fluxion_Scene_Create();

        // Each names the other, so there is no order in which both already
        // exist when read. Only a second pass can do this, and only ids
        // survive the trip at all.
        FluxionEntityHandle first = Fluxion_Scene_CreateGameObject(source, "first");
        FluxionEntityHandle second = Fluxion_Scene_CreateGameObject(source, "second");

        {
            TestLink seed;
            seed.target = second;
            seed.kept = 11;
            seed.derived = 999;
            Fluxion_GameObject_AddComponent(source, first, LinkType(), &seed);
        }
        {
            TestLink seed;
            seed.target = first;
            seed.kept = 22;
            seed.derived = 888;
            Fluxion_GameObject_AddComponent(source, second, LinkType(), &seed);
        }

        const FluxionUUID firstId = Fluxion_GameObject_GetUUID(source, first);
        const FluxionUUID secondId = Fluxion_GameObject_GetUUID(source, second);

        usize size = 0;
        u8* bytes = Fluxion_Scene_SaveToBuffer(source, 4096, &size);
        TEST_CHECK(ctx, bytes != nullptr);
        if (bytes == nullptr) return;
        Fluxion_Scene_Destroy(source);

        FluxionSceneHandle loaded = Fluxion_Scene_Create();

        // Something made first, so that the handles the loaded objects get
        // are NOT the ones they had. A reference written as a raw handle
        // would still load here -- pointing at the wrong thing.
        FluxionEntityHandle decoy = Fluxion_Scene_CreateGameObject(loaded, "decoy");
        (void)decoy;

        {
            FluxionStream reader;
            Fluxion_MemoryStream_InitReader(&reader, bytes, size);
            TEST_CHECK(ctx, Fluxion_Scene_Load(loaded, &reader));
        }
        Fluxion_Scene_FreeBuffer(bytes, size);

        FluxionEntityHandle loadedFirst = Fluxion_Scene_FindByUUID(loaded, firstId);
        FluxionEntityHandle loadedSecond = Fluxion_Scene_FindByUUID(loaded, secondId);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(loaded, loadedFirst));
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(loaded, loadedSecond));

        {
            const TestLink* a = (const TestLink*)Fluxion_GameObject_GetComponent(loaded, loadedFirst, LinkType());
            const TestLink* b = (const TestLink*)Fluxion_GameObject_GetComponent(loaded, loadedSecond, LinkType());
            TEST_CHECK(ctx, a != nullptr && b != nullptr);
            if (a == nullptr || b == nullptr) return;

            TEST_CHECK(ctx, SameEntity(a->target, loadedSecond));
            TEST_CHECK(ctx, SameEntity(b->target, loadedFirst));
            TEST_CHECK(ctx, a->kept == 11 && b->kept == 22);

            // The one marked as not worth saving was not saved: it comes
            // back at whatever a fresh component starts at, not at what it
            // held when the scene was written.
            TEST_CHECK(ctx, a->derived == 0 && b->derived == 0);
        }

        Fluxion_Scene_Destroy(loaded);
    }

    // --- A reference to something that is not in the file ------------------

    {
        FluxionSceneHandle source = Fluxion_Scene_Create();
        FluxionEntityHandle holder = Fluxion_Scene_CreateGameObject(source, "holder");
        FluxionEntityHandle doomed = Fluxion_Scene_CreateGameObject(source, "doomed");

        TestLink seed;
        seed.target = doomed;
        seed.kept = 5;
        seed.derived = 0;
        Fluxion_GameObject_AddComponent(source, holder, LinkType(), &seed);

        // Gone before the save, so the field names an object that will not
        // be in the file. It has to come back naming nothing rather than
        // naming whatever now sits where it used to.
        Fluxion_GameObject_Destroy(source, doomed);

        const FluxionUUID holderId = Fluxion_GameObject_GetUUID(source, holder);
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

        const TestLink* link = (const TestLink*)Fluxion_GameObject_GetComponent(
            loaded, Fluxion_Scene_FindByUUID(loaded, holderId), LinkType());
        TEST_CHECK(ctx, link != nullptr);
        TEST_CHECK(ctx, link != nullptr && !FLUXION_HANDLE_IS_VALID(link->target));
        TEST_CHECK(ctx, link != nullptr && link->kept == 5);

        Fluxion_Scene_Destroy(loaded);
    }

    // --- What a reader must refuse ----------------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        Fluxion_Scene_CreateGameObject(scene, "one");

        usize size = 0;
        u8* bytes = Fluxion_Scene_SaveToBuffer(scene, 4096, &size);
        TEST_CHECK(ctx, bytes != nullptr);
        if (bytes == nullptr) return;

        // Something that is not one of these at all.
        {
            u8 rubbish[64];
            std::memset(rubbish, 0x5A, sizeof(rubbish));

            FluxionStream reader;
            Fluxion_MemoryStream_InitReader(&reader, rubbish, sizeof(rubbish));
            TEST_CHECK(ctx, !Fluxion_Scene_Load(scene, &reader));
        }

        // Written by something that knew more than this build does.
        // Refused outright rather than read as far as it goes, because
        // what it knew might be exactly what is missing afterwards.
        {
            u8* newer = (u8*)std::malloc(size);
            TEST_CHECK(ctx, newer != nullptr);
            if (newer == nullptr) return;
            std::memcpy(newer, bytes, size);

            const u32 futureVersion = FLUXION_SCENE_FORMAT_VERSION + 1u;
            std::memcpy(newer + sizeof(u32), &futureVersion, sizeof(futureVersion));

            FluxionStream reader;
            Fluxion_MemoryStream_InitReader(&reader, newer, size);
            TEST_CHECK(ctx, !Fluxion_Scene_Load(scene, &reader));
            std::free(newer);
        }

        // Cut short. The reader must say so rather than build half a
        // scene, because half a scene is harder to notice than none.
        {
            FluxionStream reader;
            Fluxion_MemoryStream_InitReader(&reader, bytes, size / 2);
            TEST_CHECK(ctx, !Fluxion_Scene_Load(scene, &reader));
        }

        Fluxion_Scene_FreeBuffer(bytes, size);
        Fluxion_Scene_Destroy(scene);
    }

    // --- A prefab, and what a copy remembers ------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionEntityHandle root = Fluxion_Scene_CreateGameObject(scene, "turret");
        FluxionEntityHandle barrel = Fluxion_Scene_CreateGameObject(scene, "barrel");
        FluxionEntityHandle outside = Fluxion_Scene_CreateGameObject(scene, "not part of it");
        Fluxion_GameObject_SetParent(scene, barrel, root);

        {
            FluxionVec3 position = { 3.0f, 0.0f, 0.0f };
            Fluxion_GameObject_SetLocalPosition(scene, barrel, position);
        }
        {
            TestStats seed = { 50, 1.5f };
            Fluxion_GameObject_AddComponent(scene, root, StatsType(), &seed);
        }

        FluxionPrefab* prefab = Fluxion_Prefab_CreateFromObject(scene, root);
        TEST_CHECK(ctx, prefab != nullptr);
        if (prefab == nullptr) return;
        TEST_CHECK(ctx, !Fluxion_UUID_IsNil(Fluxion_Prefab_GetId(prefab)));

        // Only the subtree: the object beside it is not in the prefab, so
        // a copy brings two objects and not three.
        (void)outside;

        FluxionEntityHandle copyRoot = Fluxion_Prefab_Instantiate(prefab, scene);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, copyRoot));
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 5);
        TEST_CHECK(ctx, Fluxion_GameObject_GetChildCount(scene, copyRoot) == 1);

        // A copy is a new object: it must not answer to the id of what it
        // was copied from, or every lookup by id would be a coin toss.
        TEST_CHECK(ctx, !Fluxion_UUID_Equals(Fluxion_GameObject_GetUUID(scene, copyRoot),
                                             Fluxion_GameObject_GetUUID(scene, root)));
        TEST_CHECK(ctx, !SameEntity(copyRoot, root));

        // And it knows where it came from.
        {
            const FluxionPrefabLink* link =
                (const FluxionPrefabLink*)Fluxion_GameObject_GetComponent(scene, copyRoot, Fluxion_PrefabLink_TypeId());
            TEST_CHECK(ctx, link != nullptr);
            TEST_CHECK(ctx, link != nullptr && Fluxion_UUID_Equals(link->prefab, Fluxion_Prefab_GetId(prefab)));
            TEST_CHECK(ctx, link != nullptr &&
                Fluxion_UUID_Equals(link->sourceEntity, Fluxion_GameObject_GetUUID(scene, root)));
        }

        // The values came across.
        {
            const TestStats* stats = (const TestStats*)Fluxion_GameObject_GetComponent(scene, copyRoot, StatsType());
            TEST_CHECK(ctx, stats != nullptr);
            TEST_CHECK(ctx, stats != nullptr && stats->health == 50 && stats->speed == 1.5f);
        }

        // Nothing changed yet, so nothing differs.
        TEST_CHECK(ctx, !Fluxion_Prefab_IsOverridden(prefab, scene, copyRoot, StatsType()));

        // Changed, and now it does -- worked out by comparing, because
        // nothing recorded the change as it happened.
        {
            TestStats* stats = (TestStats*)Fluxion_GameObject_GetComponent(scene, copyRoot, StatsType());
            stats->health = 999;
        }
        TEST_CHECK(ctx, Fluxion_Prefab_IsOverridden(prefab, scene, copyRoot, StatsType()));

        // Put back, and it does not.
        TEST_CHECK(ctx, Fluxion_Prefab_Revert(prefab, scene, copyRoot));
        TEST_CHECK(ctx, !Fluxion_Prefab_IsOverridden(prefab, scene, copyRoot, StatsType()));
        {
            const TestStats* stats = (const TestStats*)Fluxion_GameObject_GetComponent(scene, copyRoot, StatsType());
            TEST_CHECK(ctx, stats != nullptr && stats->health == 50);
        }

        // Pushed the other way: the prefab now says what the copy said,
        // and a copy made afterwards has it.
        {
            TestStats* stats = (TestStats*)Fluxion_GameObject_GetComponent(scene, copyRoot, StatsType());
            stats->health = 123;
        }
        TEST_CHECK(ctx, Fluxion_Prefab_Apply(prefab, scene, copyRoot));
        TEST_CHECK(ctx, !Fluxion_Prefab_IsOverridden(prefab, scene, copyRoot, StatsType()));

        {
            FluxionEntityHandle later = Fluxion_Prefab_Instantiate(prefab, scene);
            const TestStats* stats = (const TestStats*)Fluxion_GameObject_GetComponent(scene, later, StatsType());
            TEST_CHECK(ctx, stats != nullptr);
            TEST_CHECK(ctx, stats != nullptr && stats->health == 123);
        }

        // An object that came from no prefab is not a copy of this one.
        TEST_CHECK(ctx, !Fluxion_Prefab_IsOverridden(prefab, scene, outside, StatsType()));
        TEST_CHECK(ctx, !Fluxion_Prefab_Revert(prefab, scene, outside));

        Fluxion_Prefab_Destroy(prefab);
        Fluxion_Scene_Destroy(scene);
    }

    // --- References inside a copy point within the copy -------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionEntityHandle root = Fluxion_Scene_CreateGameObject(scene, "pair root");
        FluxionEntityHandle child = Fluxion_Scene_CreateGameObject(scene, "pair child");
        Fluxion_GameObject_SetParent(scene, child, root);

        {
            TestLink seed;
            seed.target = child;
            seed.kept = 7;
            seed.derived = 0;
            Fluxion_GameObject_AddComponent(scene, root, LinkType(), &seed);
        }

        FluxionPrefab* prefab = Fluxion_Prefab_CreateFromObject(scene, root);
        TEST_CHECK(ctx, prefab != nullptr);
        if (prefab == nullptr) return;

        FluxionEntityHandle copyRoot = Fluxion_Prefab_Instantiate(prefab, scene);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, copyRoot));

        {
            const TestLink* link = (const TestLink*)Fluxion_GameObject_GetComponent(scene, copyRoot, LinkType());
            const FluxionEntityHandle copyChild = Fluxion_GameObject_GetFirstChild(scene, copyRoot);

            TEST_CHECK(ctx, link != nullptr);
            if (link == nullptr) return;

            // The copy's own child, not the original's. A reference left
            // pointing at the original would look right in every other
            // check and be wrong in the one that matters.
            TEST_CHECK(ctx, SameEntity(link->target, copyChild));
            TEST_CHECK(ctx, !SameEntity(link->target, child));
            TEST_CHECK(ctx, link->kept == 7);
        }

        Fluxion_Prefab_Destroy(prefab);
        Fluxion_Scene_Destroy(scene);
    }
}
