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
#include <Fluxion/Scene/EntityQuery.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/Transform.h>

#include <cstring>

// What storing entities by which components they carry can get wrong, and
// what none of it looks like from the outside.
//
// Almost nothing here fails loudly when it is broken. An entity handed
// the wrong row still returns a number; a block that lost track of where
// a moved entity went still has all its data. So every check below is
// written to compare two things that a bug would have to break
// consistently to slip past: what an entity says it holds, against what
// the storage says it is holding for that entity.

namespace
{

struct TestAlpha
{
    static constexpr auto Name = "TestArchetypeAlpha";
    u32 value;
};

struct TestBeta
{
    static constexpr auto Name = "TestArchetypeBeta";
    u64 value;
};

struct TestGamma
{
    static constexpr auto Name = "TestArchetypeGamma";
    f32 value;
};

// Deliberately wide, so that few of them fit in a block and a test can
// cross a block boundary with a handful of entities instead of thousands.
// Nothing here depends on how many exactly -- the boundary is found by
// looking, not by working it out from the constants.
struct TestBulk
{
    static constexpr auto Name = "TestArchetypeBulk";
    u32 value;
    u8 padding[1020];
};

FluxionTypeInfo MakeTypeInfo(const char* name, usize size)
{
    FluxionTypeInfo info;
    info.name = Fluxion_StringView_FromCStr(name);
    info.id = Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(name));
    info.kind = FLUXION_TYPE_KIND_STRUCT;
    info.size = size;
    info.version = 1;
    info.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
    info.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));
    return info;
}

void RegisterTypes()
{
    static FluxionTypeInfo alpha = MakeTypeInfo(TestAlpha::Name, sizeof(TestAlpha));
    static FluxionTypeInfo beta = MakeTypeInfo(TestBeta::Name, sizeof(TestBeta));
    static FluxionTypeInfo gamma = MakeTypeInfo(TestGamma::Name, sizeof(TestGamma));
    static FluxionTypeInfo bulk = MakeTypeInfo(TestBulk::Name, sizeof(TestBulk));

    Fluxion_Reflection_RegisterType(&alpha);
    Fluxion_Reflection_RegisterType(&beta);
    Fluxion_Reflection_RegisterType(&gamma);
    Fluxion_Reflection_RegisterType(&bulk);
}

FluxionTypeId AlphaType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestAlpha::Name)); }
FluxionTypeId BetaType()  { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestBeta::Name)); }
FluxionTypeId GammaType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestGamma::Name)); }
FluxionTypeId BulkType()  { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestBulk::Name)); }

// The components every object carries because they are part of it rather
// than attached to it: where it is, and what scripts hang off it. They are
// counted by GetComponentTypes like anything else, so a test about how
// many an object carries has to say so rather than hide it in a number.
constexpr u32 kIntrinsicComponents = 2;

bool SameEntity(FluxionEntityHandle a, FluxionEntityHandle b)
{
    return a.index == b.index && a.generation == b.generation;
}

FluxionEntityQueryDesc Requiring(const FluxionTypeId* types, u32 count)
{
    FluxionEntityQueryDesc desc;
    desc.required = types;
    desc.requiredCount = count;
    desc.excluded = nullptr;
    desc.excludedCount = 0;
    return desc;
}

// How many blocks a query yields, and how many entities in total.
void MeasureQuery(FluxionSceneHandle scene, const FluxionEntityQueryDesc& desc, u32* outBlocks, u32* outEntities)
{
    FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
    FluxionEntityChunkView chunk;

    *outBlocks = 0;
    *outEntities = 0;
    while (Fluxion_EntityQuery_Next(&query, &chunk))
    {
        ++(*outBlocks);
        *outEntities += chunk.count;
    }
}

// Every entity that should be carrying a bulk value still finds its own,
// and every stored row belongs to the entity beside it. Both directions,
// because either one alone can be passed by a storage that hands out
// somebody else's row.
void CheckBulkAgrees(TestContext& ctx, FluxionSceneHandle scene, const FluxionEntityHandle* entities, const u32* expected, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        const TestBulk* found = (const TestBulk*)Fluxion_GameObject_GetComponent(scene, entities[i], BulkType());
        TEST_CHECK(ctx, found != nullptr);
        TEST_CHECK(ctx, found != nullptr && found->value == expected[i]);
    }

    {
        const FluxionTypeId required = BulkType();
        const FluxionEntityQueryDesc desc = Requiring(&required, 1);
        FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
        FluxionEntityChunkView chunk;
        u32 seen = 0;

        while (Fluxion_EntityQuery_Next(&query, &chunk))
        {
            const TestBulk* values = (const TestBulk*)Fluxion_EntityChunk_Column(&chunk, BulkType());
            TEST_CHECK(ctx, values != nullptr);
            if (values == nullptr) continue;

            for (u32 row = 0; row < chunk.count; ++row)
            {
                bool matched = false;
                for (u32 i = 0; i < count; ++i)
                {
                    if (!SameEntity(chunk.entities[row], entities[i])) continue;
                    matched = true;
                    TEST_CHECK(ctx, values[row].value == expected[i]);
                    break;
                }
                TEST_CHECK(ctx, matched);
                ++seen;
            }
        }
        TEST_CHECK(ctx, seen == count);
    }
}

} // namespace

void Test_Archetype_Run(TestContext& ctx)
{
    RegisterTypes();

    // --- The order components were given in must not matter -------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionEntityHandle first = Fluxion_Scene_CreateGameObject(scene, "alpha then beta");
        FluxionEntityHandle second = Fluxion_Scene_CreateGameObject(scene, "beta then alpha");

        {
            TestAlpha a; a.value = 1u;
            TestBeta b;  b.value = 2u;
            Fluxion_GameObject_AddComponent(scene, first, AlphaType(), &a);
            Fluxion_GameObject_AddComponent(scene, first, BetaType(), &b);
        }
        {
            TestBeta b;  b.value = 20u;
            TestAlpha a; a.value = 10u;
            Fluxion_GameObject_AddComponent(scene, second, BetaType(), &b);
            Fluxion_GameObject_AddComponent(scene, second, AlphaType(), &a);
        }

        // Both carry both, with their own values.
        TEST_CHECK(ctx, ((const TestAlpha*)Fluxion_GameObject_GetComponent(scene, first, AlphaType()))->value == 1u);
        TEST_CHECK(ctx, ((const TestBeta*)Fluxion_GameObject_GetComponent(scene, first, BetaType()))->value == 2u);
        TEST_CHECK(ctx, ((const TestAlpha*)Fluxion_GameObject_GetComponent(scene, second, AlphaType()))->value == 10u);
        TEST_CHECK(ctx, ((const TestBeta*)Fluxion_GameObject_GetComponent(scene, second, BetaType()))->value == 20u);

        // And -- this is the part the values alone cannot show -- they were
        // put in the SAME place. Two entities carrying the same set landing
        // in two separate groupings would answer every question above
        // correctly while defeating the entire point of the storage, so it
        // is checked directly: one block, holding both.
        {
            const FluxionTypeId both[] = { AlphaType(), BetaType() };
            const FluxionEntityQueryDesc desc = Requiring(both, 2);
            u32 blocks = 0;
            u32 entities = 0;
            MeasureQuery(scene, desc, &blocks, &entities);
            TEST_CHECK(ctx, entities == 2);
            TEST_CHECK(ctx, blocks == 1);
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- Values survive being moved, again and again --------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionEntityHandle entity = Fluxion_Scene_CreateGameObject(scene, "grows");

        {
            TestAlpha a; a.value = 111u;
            Fluxion_GameObject_AddComponent(scene, entity, AlphaType(), &a);
        }

        // Each of these moves everything the entity carries somewhere
        // else. The earlier values have to arrive intact, which is the one
        // thing this storage does that the previous one never had to.
        {
            TestBeta b; b.value = 222u;
            Fluxion_GameObject_AddComponent(scene, entity, BetaType(), &b);
            TEST_CHECK(ctx, ((const TestAlpha*)Fluxion_GameObject_GetComponent(scene, entity, AlphaType()))->value == 111u);
        }
        {
            TestGamma g; g.value = 3.5f;
            Fluxion_GameObject_AddComponent(scene, entity, GammaType(), &g);
            TEST_CHECK(ctx, ((const TestAlpha*)Fluxion_GameObject_GetComponent(scene, entity, AlphaType()))->value == 111u);
            TEST_CHECK(ctx, ((const TestBeta*)Fluxion_GameObject_GetComponent(scene, entity, BetaType()))->value == 222u);
        }

        // More than the three attached: the ones every entity carries as
        // part of itself are counted here like anything else.
        TEST_CHECK(ctx, Fluxion_GameObject_GetComponentTypes(scene, entity, nullptr, 0) == kIntrinsicComponents + 3);

        // Taking one away moves everything again, and the two that stay
        // must keep their values -- including the one that was NOT next to
        // the one removed.
        TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, entity, BetaType()));
        TEST_CHECK(ctx, !Fluxion_GameObject_HasComponent(scene, entity, BetaType()));
        TEST_CHECK(ctx, ((const TestAlpha*)Fluxion_GameObject_GetComponent(scene, entity, AlphaType()))->value == 111u);
        TEST_CHECK(ctx, ((const TestGamma*)Fluxion_GameObject_GetComponent(scene, entity, GammaType()))->value == 3.5f);

        // Down to nothing that was attached, and back up. What is left is
        // the transform, which is part of the entity rather than attached
        // to it -- so "carrying nothing" is one type, not zero, and that
        // must not be a special case anywhere.
        TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, entity, AlphaType()));
        TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, entity, GammaType()));
        TEST_CHECK(ctx, Fluxion_GameObject_GetComponentTypes(scene, entity, nullptr, 0) == kIntrinsicComponents);
        TEST_CHECK(ctx, Fluxion_GameObject_HasComponent(scene, entity, Fluxion_Transform_TypeId()));
        TEST_CHECK(ctx, Fluxion_GameObject_HasComponent(scene, entity, Fluxion_ScriptComponent_TypeId()));
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, entity));

        // And neither can be taken away, however it is asked for.
        TEST_CHECK(ctx, !Fluxion_GameObject_RemoveComponent(scene, entity, Fluxion_Transform_TypeId()));
        TEST_CHECK(ctx, Fluxion_GameObject_HasComponent(scene, entity, Fluxion_Transform_TypeId()));
        TEST_CHECK(ctx, !Fluxion_GameObject_RemoveComponent(scene, entity, Fluxion_ScriptComponent_TypeId()));
        TEST_CHECK(ctx, Fluxion_GameObject_HasComponent(scene, entity, Fluxion_ScriptComponent_TypeId()));

        {
            TestAlpha a; a.value = 999u;
            TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, entity, AlphaType(), &a) != nullptr);
            TEST_CHECK(ctx, ((const TestAlpha*)Fluxion_GameObject_GetComponent(scene, entity, AlphaType()))->value == 999u);
        }

        // Asking with a buffer too small answers with what fits rather
        // than writing past it.
        {
            TestBeta b; b.value = 7u;
            Fluxion_GameObject_AddComponent(scene, entity, BetaType(), &b);

            FluxionTypeId one[1];
            const u32 written = Fluxion_GameObject_GetComponentTypes(scene, entity, one, 1);
            TEST_CHECK(ctx, written == 1);
            TEST_CHECK(ctx, Fluxion_GameObject_GetComponentTypes(scene, entity, nullptr, 0) == kIntrinsicComponents + 2);
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- Crossing a block boundary --------------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        // Entities are added one at a time until the storage needs a
        // second block, so the boundary is found by looking rather than
        // worked out from the constants -- a test that computed it would
        // start agreeing with a broken layout the moment the layout
        // changed.
        constexpr u32 kMax = 200;
        static FluxionEntityHandle entities[kMax];
        static u32 values[kMax];
        u32 count = 0;
        u32 capacity = 0;

        while (count < kMax)
        {
            FluxionEntityHandle entity = Fluxion_Scene_CreateGameObject(scene, "bulk");
            TestBulk seed;
            std::memset(&seed, 0, sizeof(seed));
            seed.value = count + 1u;

            TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, entity, BulkType(), &seed) != nullptr);
            entities[count] = entity;
            values[count] = seed.value;
            ++count;

            {
                const FluxionTypeId required = BulkType();
                const FluxionEntityQueryDesc desc = Requiring(&required, 1);
                u32 blocks = 0;
                u32 total = 0;
                MeasureQuery(scene, desc, &blocks, &total);
                TEST_CHECK(ctx, total == count);
                if (blocks > 1)
                {
                    capacity = count - 1u;
                    break;
                }
            }
        }

        // The boundary was actually reached, and more than one entity fits
        // in a block -- otherwise the checks below would be testing a
        // degenerate layout rather than the real one.
        TEST_CHECK(ctx, capacity > 1);
        TEST_CHECK(ctx, count == capacity + 1u);

        // Enough to fill a third block, so that a removal from the first
        // one has to reach past a whole block to find its replacement row.
        while (count < capacity * 2u + 1u && count < kMax)
        {
            FluxionEntityHandle entity = Fluxion_Scene_CreateGameObject(scene, "bulk");
            TestBulk seed;
            std::memset(&seed, 0, sizeof(seed));
            seed.value = count + 1u;
            TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, entity, BulkType(), &seed) != nullptr);
            entities[count] = entity;
            values[count] = seed.value;
            ++count;
        }

        {
            const FluxionTypeId required = BulkType();
            const FluxionEntityQueryDesc desc = Requiring(&required, 1);
            u32 blocks = 0;
            u32 total = 0;
            MeasureQuery(scene, desc, &blocks, &total);
            TEST_CHECK(ctx, total == count);
            TEST_CHECK(ctx, blocks == 3);
        }

        CheckBulkAgrees(ctx, scene, entities, values, count);

        // Taken from the middle of the FIRST block, while the last block
        // is elsewhere entirely. The row that fills the hole comes from
        // the far end, and the entity it belonged to has to be told where
        // it went -- the half of a swap that is easy to leave out, and
        // that nothing but the reverse reading below would catch.
        {
            const u32 victim = capacity / 2u;
            TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, entities[victim], BulkType()));
            TEST_CHECK(ctx, !Fluxion_GameObject_HasComponent(scene, entities[victim], BulkType()));

            entities[victim] = entities[count - 1u];
            values[victim] = values[count - 1u];
            --count;

            CheckBulkAgrees(ctx, scene, entities, values, count);
        }

        // And by destroying an entity rather than by taking its component
        // away: a different path into the same closing-up.
        {
            const u32 victim = 0;
            Fluxion_GameObject_Destroy(scene, entities[victim]);

            entities[victim] = entities[count - 1u];
            values[victim] = values[count - 1u];
            --count;

            CheckBulkAgrees(ctx, scene, entities, values, count);
        }

        // Emptied back down to one block, then filled again, so that a
        // block given back when it drained has to be taken again.
        while (count > 1)
        {
            Fluxion_GameObject_Destroy(scene, entities[count - 1u]);
            --count;
        }
        CheckBulkAgrees(ctx, scene, entities, values, count);

        {
            FluxionEntityHandle entity = Fluxion_Scene_CreateGameObject(scene, "after draining");
            TestBulk seed;
            std::memset(&seed, 0, sizeof(seed));
            seed.value = 4242u;
            TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, entity, BulkType(), &seed) != nullptr);
            entities[count] = entity;
            values[count] = seed.value;
            ++count;
            CheckBulkAgrees(ctx, scene, entities, values, count);
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- What a query does and does not match ---------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionEntityHandle bare = Fluxion_Scene_CreateGameObject(scene, "bare");
        FluxionEntityHandle onlyAlpha = Fluxion_Scene_CreateGameObject(scene, "alpha");
        FluxionEntityHandle alphaBeta = Fluxion_Scene_CreateGameObject(scene, "alpha beta");
        FluxionEntityHandle allThree = Fluxion_Scene_CreateGameObject(scene, "all three");
        (void)bare;

        Fluxion_GameObject_AddComponent(scene, onlyAlpha, AlphaType(), nullptr);

        Fluxion_GameObject_AddComponent(scene, alphaBeta, AlphaType(), nullptr);
        Fluxion_GameObject_AddComponent(scene, alphaBeta, BetaType(), nullptr);

        Fluxion_GameObject_AddComponent(scene, allThree, AlphaType(), nullptr);
        Fluxion_GameObject_AddComponent(scene, allThree, BetaType(), nullptr);
        Fluxion_GameObject_AddComponent(scene, allThree, GammaType(), nullptr);

        // Asking for one type finds everything carrying it, whatever else
        // it carries. A query says what it needs, not what an entity is
        // allowed to be -- otherwise a pass over positions would stop
        // seeing an entity the moment something gave it a colour.
        {
            const FluxionTypeId required = AlphaType();
            const FluxionEntityQueryDesc desc = Requiring(&required, 1);
            u32 blocks = 0;
            u32 total = 0;
            MeasureQuery(scene, desc, &blocks, &total);
            TEST_CHECK(ctx, total == 3);

            // Three different sets, so three different groupings, so three
            // blocks -- which is also what says the sets really are being
            // kept apart.
            TEST_CHECK(ctx, blocks == 3);
        }

        // Two types finds only what carries both.
        {
            const FluxionTypeId required[] = { AlphaType(), BetaType() };
            const FluxionEntityQueryDesc desc = Requiring(required, 2);
            u32 blocks = 0;
            u32 total = 0;
            MeasureQuery(scene, desc, &blocks, &total);
            TEST_CHECK(ctx, total == 2);
        }

        // Excluding narrows it back down.
        {
            const FluxionTypeId required = AlphaType();
            const FluxionTypeId excluded = BetaType();
            FluxionEntityQueryDesc desc = Requiring(&required, 1);
            desc.excluded = &excluded;
            desc.excludedCount = 1;

            FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
            FluxionEntityChunkView chunk;
            u32 total = 0;
            bool sawOnlyAlpha = false;

            while (Fluxion_EntityQuery_Next(&query, &chunk))
            {
                for (u32 row = 0; row < chunk.count; ++row)
                {
                    ++total;
                    if (SameEntity(chunk.entities[row], onlyAlpha)) sawOnlyAlpha = true;
                }
            }
            TEST_CHECK(ctx, total == 1);
            TEST_CHECK(ctx, sawOnlyAlpha);
        }

        // Excluding something nothing carries changes nothing; excluding
        // something everything matched carries leaves none.
        {
            const FluxionTypeId required = AlphaType();
            const FluxionTypeId excludedBulk = BulkType();
            FluxionEntityQueryDesc desc = Requiring(&required, 1);
            desc.excluded = &excludedBulk;
            desc.excludedCount = 1;
            TEST_CHECK(ctx, Fluxion_Scene_CountMatching(scene, &desc) == 3);

            const FluxionTypeId excludedAlpha = AlphaType();
            desc.excluded = &excludedAlpha;
            TEST_CHECK(ctx, Fluxion_Scene_CountMatching(scene, &desc) == 0);
        }

        // Requiring nothing matches every entity in the scene, including
        // the one carrying nothing at all. That entity has to be somewhere
        // for this to work, which is why a new entity is placed rather
        // than left without a home.
        {
            const FluxionEntityQueryDesc desc = Requiring(nullptr, 0);
            u32 blocks = 0;
            u32 total = 0;
            MeasureQuery(scene, desc, &blocks, &total);
            TEST_CHECK(ctx, total == Fluxion_Scene_GameObjectCount(scene));
            TEST_CHECK(ctx, total == 4);
        }

        // A column the block does not have is null rather than a pointer
        // into the wrong place.
        {
            const FluxionTypeId required = GammaType();
            const FluxionEntityQueryDesc desc = Requiring(&required, 1);
            FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
            FluxionEntityChunkView chunk;

            while (Fluxion_EntityQuery_Next(&query, &chunk))
            {
                TEST_CHECK(ctx, Fluxion_EntityChunk_Column(&chunk, GammaType()) != nullptr);
                TEST_CHECK(ctx, Fluxion_EntityChunk_Column(&chunk, BulkType()) == nullptr);
            }
        }

        // A query against a scene that is not live yields nothing, rather
        // than yielding everything -- which is what an empty requirement
        // means and would be a dangerous thing to get by accident.
        {
            FluxionSceneHandle dead = Fluxion_Scene_InvalidHandle();
            const FluxionEntityQueryDesc desc = Requiring(nullptr, 0);
            u32 blocks = 0;
            u32 total = 0;
            MeasureQuery(dead, desc, &blocks, &total);
            TEST_CHECK(ctx, blocks == 0 && total == 0);
        }

        Fluxion_Scene_Destroy(scene);
    }

    // --- One scene's groupings are not another's ------------------------

    {
        FluxionSceneHandle first = Fluxion_Scene_Create();
        FluxionSceneHandle second = Fluxion_Scene_Create();

        FluxionEntityHandle inFirst = Fluxion_Scene_CreateGameObject(first, "in first");
        TestAlpha a; a.value = 5u;
        Fluxion_GameObject_AddComponent(first, inFirst, AlphaType(), &a);

        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(first, AlphaType()) == 1);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(second, AlphaType()) == 0);

        // Destroying one scene leaves the other's storage alone, and the
        // slot it frees starts empty for whoever gets it next.
        Fluxion_Scene_Destroy(first);

        FluxionEntityHandle inSecond = Fluxion_Scene_CreateGameObject(second, "in second");
        TestAlpha b; b.value = 6u;
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(second, inSecond, AlphaType(), &b) != nullptr);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(second, AlphaType()) == 1);

        FluxionSceneHandle third = Fluxion_Scene_Create();
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(third, AlphaType()) == 0);
        {
            const FluxionEntityQueryDesc desc = Requiring(nullptr, 0);
            TEST_CHECK(ctx, Fluxion_Scene_CountMatching(third, &desc) == 0);
        }

        Fluxion_Scene_Destroy(second);
        Fluxion_Scene_Destroy(third);
    }
}
