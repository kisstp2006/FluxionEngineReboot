#include "TestFramework.h"

#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Scene/EntityCommandBuffer.h>
#include <Fluxion/Scene/EntityQuery.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/Transform.h>
#include <Fluxion/Scene/World.hpp>

#include <cstring>

namespace
{

// Two component types rather than one, and of different sizes, because a
// single type cannot show whether the columns are actually separate: a
// bug that overlapped them would pass every test written against one
// type.
struct TestPosition
{
    static constexpr auto Name = "TestPosition";
    f32 x;
    f32 y;
    f32 z;
};

struct TestTag
{
    static constexpr auto Name = "TestTag";
    u32 value;
};

// Registered but never attached, to tell "this type has no components in
// this scene" apart from "this type is unknown".
struct TestUnused
{
    static constexpr auto Name = "TestUnused";
    u64 unused;
};

// Deliberately NOT registered. Attaching it must be refused rather than
// guessed at.
struct TestUnregistered
{
    static constexpr auto Name = "TestUnregistered";
    u32 value;
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

// The registry keeps the pointer it is given, so these have to outlive
// every scene in this file.
void RegisterComponentTypes()
{
    static FluxionTypeInfo position = MakeTypeInfo(TestPosition::Name, sizeof(TestPosition));
    static FluxionTypeInfo tag = MakeTypeInfo(TestTag::Name, sizeof(TestTag));
    static FluxionTypeInfo unused = MakeTypeInfo(TestUnused::Name, sizeof(TestUnused));

    Fluxion_Reflection_RegisterType(&position);
    Fluxion_Reflection_RegisterType(&tag);
    Fluxion_Reflection_RegisterType(&unused);
}

FluxionTypeId PositionType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestPosition::Name)); }
FluxionTypeId TagType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestTag::Name)); }

// The components every object carries because they are part of it rather
// than attached to it: where it is, and what scripts hang off it. They are
// counted by GetComponentTypes like anything else, so a test about how
// many an object carries has to say so rather than hide it in a number.
constexpr u32 kIntrinsicComponents = 2;

bool SameObject(FluxionGameObjectHandle a, FluxionGameObjectHandle b)
{
    return a.index == b.index && a.generation == b.generation;
}

// Every object that should be carrying a position still finds its own,
// and the storage agrees with the objects it says the values belong to.
//
// Both directions are checked on purpose. A storage that hands an object
// the wrong row does not crash and does not lose the value -- it returns
// somebody else's, which the object-by-object half can miss entirely when
// the wrong row happens to hold the right number. Only reading it the
// other way round, from the stored rows back to their owners, catches
// that.
void CheckStorageAgrees(TestContext& ctx, FluxionSceneHandle scene, const FluxionGameObjectHandle* objects, const f32* expected, u32 count)
{
    // Asked object by object: each still finds the value it was given.
    for (u32 i = 0; i < count; ++i)
    {
        const TestPosition* found = (const TestPosition*)Fluxion_GameObject_GetComponent(scene, objects[i], PositionType());
        TEST_CHECK(ctx, found != nullptr);
        TEST_CHECK(ctx, found != nullptr && found->x == expected[i]);
    }

    // And read the other way round: every stored row belongs to the entity
    // beside it, and holds that entity's value.
    {
        const FluxionTypeId required = PositionType();
        FluxionEntityQueryDesc desc{};
        desc.required = &required;
        desc.requiredCount = 1;

        FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
        FluxionEntityChunkView chunk;
        u32 seen = 0;

        while (Fluxion_EntityQuery_Next(&query, &chunk))
        {
            const TestPosition* positions = (const TestPosition*)Fluxion_EntityChunk_Column(&chunk, PositionType());
            TEST_CHECK(ctx, positions != nullptr);
            if (positions == nullptr) continue;

            for (u32 row = 0; row < chunk.count; ++row)
            {
                bool matched = false;
                for (u32 i = 0; i < count; ++i)
                {
                    if (!SameObject(chunk.entities[row], objects[i])) continue;
                    matched = true;
                    TEST_CHECK(ctx, positions[row].x == expected[i]);
                    break;
                }
                TEST_CHECK(ctx, matched);
                ++seen;
            }
        }

        TEST_CHECK(ctx, seen == count);
    }

    TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == count);
}

} // namespace

void Test_DataComponents_Run(TestContext& ctx)
{
    RegisterComponentTypes();

    // --- Attaching, reading, and what a fresh component holds -----------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");

        TEST_CHECK(ctx, !Fluxion_GameObject_HasComponent(scene, object, PositionType()));
        TEST_CHECK(ctx, Fluxion_GameObject_GetComponent(scene, object, PositionType()) == nullptr);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 0);

        // With no starting value: defined zero, not whatever was there.
        TestPosition* fresh = (TestPosition*)Fluxion_GameObject_AddComponent(scene, object, PositionType(), nullptr);
        TEST_CHECK(ctx, fresh != nullptr);
        TEST_CHECK(ctx, fresh != nullptr && fresh->x == 0.0f && fresh->y == 0.0f && fresh->z == 0.0f);
        TEST_CHECK(ctx, Fluxion_GameObject_HasComponent(scene, object, PositionType()));
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 1);

        // Asking again hands back the same component rather than a second
        // one.
        TestPosition* again = (TestPosition*)Fluxion_GameObject_AddComponent(scene, object, PositionType(), nullptr);
        TEST_CHECK(ctx, again == fresh);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 1);

        // Written through the pointer, read back through the interface.
        fresh->x = 1.0f;
        fresh->y = 2.0f;
        fresh->z = 3.0f;
        {
            const TestPosition* read = (const TestPosition*)Fluxion_GameObject_GetComponent(scene, object, PositionType());
            TEST_CHECK(ctx, read != nullptr && read->y == 2.0f);
        }

        // A second type on the same object is its own storage, and the
        // value already written survives the object moving to where
        // objects carrying both are kept.
        //
        // The pointers taken above are deliberately NOT reused past this
        // point: adding a component moves everything the object carries,
        // so `fresh` names storage that is no longer this object's. That
        // is the contract, and a test that quietly relied on the old one
        // still working would be testing nothing.
        TestTag seed;
        seed.value = 77u;
        TestTag* tag = (TestTag*)Fluxion_GameObject_AddComponent(scene, object, TagType(), &seed);
        TEST_CHECK(ctx, tag != nullptr && tag->value == 77u);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, TagType()) == 1);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 1);
        {
            const TestPosition* moved = (const TestPosition*)Fluxion_GameObject_GetComponent(scene, object, PositionType());
            TEST_CHECK(ctx, moved != nullptr);
            TEST_CHECK(ctx, moved != nullptr && moved->x == 1.0f && moved->y == 2.0f && moved->z == 3.0f);
        }

        // Both types are reported as this object's -- and so is the
        // transform, which every object carries from the moment it is
        // made and which is counted here like anything else.
        {
            FluxionTypeId carried[FLUXION_SCENE_MAX_COMPONENT_TYPES];
            const u32 carriedCount = Fluxion_GameObject_GetComponentTypes(scene, object, carried, FLUXION_SCENE_MAX_COMPONENT_TYPES);
            bool sawPosition = false;
            bool sawTag = false;
            bool sawTransform = false;

            TEST_CHECK(ctx, carriedCount == kIntrinsicComponents + 2);
            TEST_CHECK(ctx, Fluxion_GameObject_GetComponentTypes(scene, object, nullptr, 0) == kIntrinsicComponents + 2);
            for (u32 i = 0; i < carriedCount; ++i)
            {
                if (carried[i] == PositionType()) sawPosition = true;
                if (carried[i] == TagType()) sawTag = true;
                if (carried[i] == Fluxion_Transform_TypeId()) sawTransform = true;
            }
            TEST_CHECK(ctx, sawPosition && sawTag && sawTransform);
        }

        // A type nothing was ever attached for is carried by nobody,
        // rather than refusing the question.
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestUnused::Name))) == 0);

        // A type the registry has never heard of cannot be attached: its
        // size is not knowable, and guessing one would corrupt whatever
        // sat next to it.
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, object,
            Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestUnregistered::Name)), nullptr) == nullptr);

        // So does a handle naming no object.
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, Fluxion_GameObject_InvalidHandle(), PositionType(), nullptr) == nullptr);

        Fluxion_Scene_Destroy(scene);
    }

    // --- Removing keeps every other component with its own object -------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionGameObjectHandle objects[5];
        f32 values[5];
        for (u32 i = 0; i < 5; ++i)
        {
            objects[i] = Fluxion_Scene_CreateGameObject(scene, "object");
            TestPosition seed;
            seed.x = (f32)(i + 1);
            seed.y = 0.0f;
            seed.z = 0.0f;
            Fluxion_GameObject_AddComponent(scene, objects[i], PositionType(), &seed);
            values[i] = seed.x;
        }
        CheckStorageAgrees(ctx, scene, objects, values, 5);

        // From the middle: the last row moves up into the gap, so the
        // object that owned the last row must be found at its new place.
        TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, objects[2], PositionType()));
        TEST_CHECK(ctx, !Fluxion_GameObject_HasComponent(scene, objects[2], PositionType()));
        {
            FluxionGameObjectHandle rest[4] = { objects[0], objects[1], objects[3], objects[4] };
            f32 restValues[4] = { values[0], values[1], values[3], values[4] };
            CheckStorageAgrees(ctx, scene, rest, restValues, 4);
        }

        // Taking away what is not there is nothing to do, not a mistake,
        // and must not disturb what is.
        TEST_CHECK(ctx, !Fluxion_GameObject_RemoveComponent(scene, objects[2], PositionType()));
        {
            FluxionGameObjectHandle rest[4] = { objects[0], objects[1], objects[3], objects[4] };
            f32 restValues[4] = { values[0], values[1], values[3], values[4] };
            CheckStorageAgrees(ctx, scene, rest, restValues, 4);
        }

        // From the very end: nothing moves, which is the case a swap
        // written without the equality check gets wrong by copying a row
        // onto itself and then losing it.
        TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, objects[4], PositionType()));
        {
            FluxionGameObjectHandle rest[3] = { objects[0], objects[1], objects[3] };
            f32 restValues[3] = { values[0], values[1], values[3] };
            CheckStorageAgrees(ctx, scene, rest, restValues, 3);
        }

        // And from the front.
        TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, objects[0], PositionType()));
        {
            FluxionGameObjectHandle rest[2] = { objects[1], objects[3] };
            f32 restValues[2] = { values[1], values[3] };
            CheckStorageAgrees(ctx, scene, rest, restValues, 2);
        }

        // Down to none, then back up again on the same pool.
        TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, objects[1], PositionType()));
        TEST_CHECK(ctx, Fluxion_GameObject_RemoveComponent(scene, objects[3], PositionType()));
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 0);

        TestPosition seed;
        seed.x = 42.0f;
        seed.y = 0.0f;
        seed.z = 0.0f;
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene, objects[3], PositionType(), &seed) != nullptr);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 1);

        Fluxion_Scene_Destroy(scene);
    }

    // --- An object's components go when the object does ------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionGameObjectHandle parent = Fluxion_Scene_CreateGameObject(scene, "parent");
        FluxionGameObjectHandle child = Fluxion_Scene_CreateGameObject(scene, "child");
        Fluxion_GameObject_SetParent(scene, child, parent);

        TestPosition parentSeed = { 1.0f, 0.0f, 0.0f };
        TestPosition childSeed = { 2.0f, 0.0f, 0.0f };
        Fluxion_GameObject_AddComponent(scene, parent, PositionType(), &parentSeed);
        Fluxion_GameObject_AddComponent(scene, child, PositionType(), &childSeed);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 2);

        // Destroying the parent takes the child too, so both components
        // go -- one of them belonging to an object the caller never named.
        Fluxion_GameObject_Destroy(scene, parent);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 0);

        // The slots are handed out again. A component left behind on a
        // released record would surface here as the new object already
        // carrying one.
        FluxionGameObjectHandle reused = Fluxion_Scene_CreateGameObject(scene, "reused");
        FluxionGameObjectHandle reusedToo = Fluxion_Scene_CreateGameObject(scene, "reused too");
        TEST_CHECK(ctx, !Fluxion_GameObject_HasComponent(scene, reused, PositionType()));
        TEST_CHECK(ctx, !Fluxion_GameObject_HasComponent(scene, reusedToo, PositionType()));
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, PositionType()) == 0);

        Fluxion_Scene_Destroy(scene);
    }

    // --- A scene keeps its components to itself -------------------------

    {
        FluxionSceneHandle first = Fluxion_Scene_Create();
        FluxionSceneHandle second = Fluxion_Scene_Create();

        FluxionGameObjectHandle inFirst = Fluxion_Scene_CreateGameObject(first, "in first");
        TestPosition seed = { 5.0f, 0.0f, 0.0f };
        Fluxion_GameObject_AddComponent(first, inFirst, PositionType(), &seed);

        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(first, PositionType()) == 1);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(second, PositionType()) == 0);

        // The same object index in the other scene is a different object
        // and carries nothing.
        FluxionGameObjectHandle inSecond = Fluxion_Scene_CreateGameObject(second, "in second");
        TEST_CHECK(ctx, !Fluxion_GameObject_HasComponent(second, inSecond, PositionType()));

        Fluxion_Scene_Destroy(first);

        // Destroying one scene leaves the other alone.
        TestPosition otherSeed = { 6.0f, 0.0f, 0.0f };
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(second, inSecond, PositionType(), &otherSeed) != nullptr);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(second, PositionType()) == 1);

        // And the slot the first scene left behind starts clean.
        FluxionSceneHandle third = Fluxion_Scene_Create();
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(third, PositionType()) == 0);

        Fluxion_Scene_Destroy(second);
        Fluxion_Scene_Destroy(third);
    }
}
