#include "TestFramework.h"

#include <Fluxion/Foundation/UUID.h>
#include <Fluxion/Scene/Scene.h>

#include <cstring>

namespace
{

bool SameObject(FluxionGameObjectHandle a, FluxionGameObjectHandle b)
{
    return a.index == b.index && a.generation == b.generation;
}

FluxionUUID NilUUID()
{
    FluxionUUID nil;
    std::memset(&nil, 0, sizeof(nil));
    return nil;
}

} // namespace

void Test_EntityUUID_Run(TestContext& ctx)
{
    // --- What the generator has to guarantee ----------------------------

    {
        // Enough of them that a generator repeating itself every so often
        // shows up, rather than the two or three a smoke test would take.
        constexpr u32 kCount = 4096;
        static FluxionUUID ids[kCount];

        for (u32 i = 0; i < kCount; ++i) ids[i] = Fluxion_UUID_Generate();

        u32 nilCount = 0;
        u32 duplicateCount = 0;
        for (u32 i = 0; i < kCount; ++i)
        {
            if (Fluxion_UUID_IsNil(ids[i])) ++nilCount;
            for (u32 j = i + 1; j < kCount; ++j)
            {
                if (Fluxion_UUID_Equals(ids[i], ids[j])) ++duplicateCount;
            }
        }
        TEST_CHECK(ctx, nilCount == 0);
        TEST_CHECK(ctx, duplicateCount == 0);

        // The shape is claimed in the header, so it is checked here: an id
        // that did not carry the version and variant bits would still be
        // unique but would not be what was promised.
        for (u32 i = 0; i < kCount; ++i)
        {
            TEST_CHECK(ctx, (ids[i].bytes[6] & 0xF0u) == 0x40u);
            TEST_CHECK(ctx, (ids[i].bytes[8] & 0xC0u) == 0x80u);
        }

        // Every byte position has to vary. A generator that filled some
        // stretch with a constant would still pass the uniqueness check
        // above while giving away far less than sixteen bytes of it.
        for (u32 byteIndex = 0; byteIndex < 16; ++byteIndex)
        {
            bool varies = false;
            for (u32 i = 1; i < kCount && !varies; ++i)
            {
                if (ids[i].bytes[byteIndex] != ids[0].bytes[byteIndex]) varies = true;
            }
            TEST_CHECK(ctx, varies);
        }

        char text[37];
        Fluxion_UUID_ToString(ids[0], text);
        TEST_CHECK(ctx, std::strlen(text) == 36);
        TEST_CHECK(ctx, text[8] == '-' && text[13] == '-' && text[18] == '-' && text[23] == '-');
    }

    // --- Every object gets one, and it names that object ----------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionGameObjectHandle first = Fluxion_Scene_CreateGameObject(scene, "first");
        FluxionGameObjectHandle second = Fluxion_Scene_CreateGameObject(scene, "second");

        FluxionUUID firstId = Fluxion_GameObject_GetUUID(scene, first);
        FluxionUUID secondId = Fluxion_GameObject_GetUUID(scene, second);

        TEST_CHECK(ctx, !Fluxion_UUID_IsNil(firstId));
        TEST_CHECK(ctx, !Fluxion_UUID_IsNil(secondId));
        TEST_CHECK(ctx, !Fluxion_UUID_Equals(firstId, secondId));

        TEST_CHECK(ctx, SameObject(Fluxion_Scene_FindByUUID(scene, firstId), first));
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_FindByUUID(scene, secondId), second));

        // Nothing carries the nil id, so nothing is ever found by it --
        // otherwise an unset id would quietly resolve to whichever object
        // happened to be first.
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_FindByUUID(scene, NilUUID())));
        TEST_CHECK(ctx, Fluxion_UUID_IsNil(Fluxion_GameObject_GetUUID(scene, Fluxion_GameObject_InvalidHandle())));

        // The id belongs to the object, not to the slot: the slot is
        // handed out again, the id is not.
        Fluxion_GameObject_Destroy(scene, first);
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_FindByUUID(scene, firstId)));

        FluxionGameObjectHandle reused = Fluxion_Scene_CreateGameObject(scene, "reused");
        TEST_CHECK(ctx, reused.index == first.index);
        TEST_CHECK(ctx, !Fluxion_UUID_Equals(Fluxion_GameObject_GetUUID(scene, reused), firstId));
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_FindByUUID(scene, firstId)));

        // Renaming does not touch it.
        Fluxion_GameObject_SetName(scene, second, "renamed");
        TEST_CHECK(ctx, Fluxion_UUID_Equals(Fluxion_GameObject_GetUUID(scene, second), secondId));

        Fluxion_Scene_Destroy(scene);
    }

    // --- Making an object with an id already decided --------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionUUID chosen = Fluxion_UUID_Generate();
        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObjectWithUUID(scene, "chosen", chosen);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, object));
        TEST_CHECK(ctx, Fluxion_UUID_Equals(Fluxion_GameObject_GetUUID(scene, object), chosen));
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_FindByUUID(scene, chosen), object));

        // A second object answering to the same id would make every
        // lookup a coin toss, so it is refused and nothing is made.
        const u32 before = Fluxion_Scene_GameObjectCount(scene);
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_CreateGameObjectWithUUID(scene, "clash", chosen)));
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == before);
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_FindByUUID(scene, chosen), object));

        // Nor may an object be given the id that means "none".
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_CreateGameObjectWithUUID(scene, "nil", NilUUID())));
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == before);

        // The same id in another scene is another object: ids are unique
        // where they are looked up, and lookup is per scene.
        FluxionSceneHandle other = Fluxion_Scene_Create();
        FluxionGameObjectHandle elsewhere = Fluxion_Scene_CreateGameObjectWithUUID(other, "elsewhere", chosen);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(other, elsewhere));
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_FindByUUID(other, chosen), elsewhere));
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_FindByUUID(scene, chosen), object));

        // Once the object is gone the id is free again, which is what lets
        // a scene be read back over itself.
        Fluxion_GameObject_Destroy(scene, object);
        FluxionGameObjectHandle again = Fluxion_Scene_CreateGameObjectWithUUID(scene, "again", chosen);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, again));
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_FindByUUID(scene, chosen), again));

        Fluxion_Scene_Destroy(other);
        Fluxion_Scene_Destroy(scene);
    }
}
