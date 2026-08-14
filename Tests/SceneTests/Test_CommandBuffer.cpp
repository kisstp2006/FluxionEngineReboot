#include "TestFramework.h"

#include <Fluxion/Core/Reflection/Reflection.hpp>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Scene/EntityCommandBuffer.h>
#include <Fluxion/Scene/Scene.h>

#include <cstdio>
#include <cstring>

namespace
{

struct TestMark
{
    static constexpr auto Name = "TestMark";
    u32 value;
};

FluxionTypeId MarkType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestMark::Name)); }

void RegisterMark()
{
    static FluxionTypeInfo info;
    info.name = Fluxion_StringView_FromCStr(TestMark::Name);
    info.id = MarkType();
    info.kind = FLUXION_TYPE_KIND_STRUCT;
    info.size = sizeof(TestMark);
    info.version = 1;
    info.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
    info.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));
    Fluxion_Reflection_RegisterType(&info);
}

bool SameObject(FluxionGameObjectHandle a, FluxionGameObjectHandle b)
{
    return a.index == b.index && a.generation == b.generation;
}

} // namespace

void Test_CommandBuffer_Run(TestContext& ctx)
{
    Fluxion_Reflection_Init();
    RegisterMark();

    // --- Nothing happens until playback ---------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle standing = Fluxion_Scene_CreateGameObject(scene, "standing");

        FluxionEntityCommandBuffer* buffer = Fluxion_EntityCommandBuffer_Create();
        TEST_CHECK(ctx, buffer != nullptr);
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == 0);

        FluxionUUID madeId = Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "made");
        TEST_CHECK(ctx, !Fluxion_UUID_IsNil(madeId));
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_DestroyGameObject(buffer, Fluxion_EntityTarget_Existing(standing)));
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == 2);

        // The scene is exactly as it was: the object recorded is not
        // there, the object recorded for destruction still is.
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 1);
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_FindByUUID(scene, madeId)));
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, standing));

        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Playback(buffer, scene) == 0);
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == 0);

        TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(scene, standing));
        FluxionGameObjectHandle made = Fluxion_Scene_FindByUUID(scene, madeId);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, made));
        TEST_CHECK(ctx, std::strcmp(Fluxion_GameObject_GetName(scene, made), "made") == 0);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 1);

        // Playing an emptied buffer back again does nothing at all --
        // otherwise every turn would replay the turn before it.
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Playback(buffer, scene) == 0);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 1);

        Fluxion_EntityCommandBuffer_Destroy(buffer);
        Fluxion_Scene_Destroy(scene);
    }

    // --- Naming an object the same buffer has yet to make ---------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionEntityCommandBuffer* buffer = Fluxion_EntityCommandBuffer_Create();

        FluxionUUID parentId = Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "parent");
        FluxionUUID childId = Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "child");

        TestMark seed;
        seed.value = 9u;
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_AddComponent(buffer, Fluxion_EntityTarget_Pending(childId), MarkType(), &seed, sizeof(seed)));
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_SetParent(buffer, Fluxion_EntityTarget_Pending(childId), Fluxion_EntityTarget_Pending(parentId)));

        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Playback(buffer, scene) == 0);

        FluxionGameObjectHandle parent = Fluxion_Scene_FindByUUID(scene, parentId);
        FluxionGameObjectHandle child = Fluxion_Scene_FindByUUID(scene, childId);
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, parent));
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, child));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetParent(scene, child), parent));

        const TestMark* mark = (const TestMark*)Fluxion_GameObject_GetComponent(scene, child, MarkType());
        TEST_CHECK(ctx, mark != nullptr && mark->value == 9u);

        // The component landed on the child and on nothing else.
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, MarkType()) == 1);
        TEST_CHECK(ctx, !Fluxion_GameObject_HasComponent(scene, parent, MarkType()));

        Fluxion_EntityCommandBuffer_Destroy(buffer);
        Fluxion_Scene_Destroy(scene);
    }

    // --- Order, and what happens when a command cannot land -------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionGameObjectHandle target = Fluxion_Scene_CreateGameObject(scene, "target");
        FluxionEntityCommandBuffer* buffer = Fluxion_EntityCommandBuffer_Create();

        // Destroyed first, then asked to carry a component. The second
        // cannot land, and says so, but must not stop the third.
        TestMark seed;
        seed.value = 1u;
        Fluxion_EntityCommandBuffer_DestroyGameObject(buffer, Fluxion_EntityTarget_Existing(target));
        Fluxion_EntityCommandBuffer_AddComponent(buffer, Fluxion_EntityTarget_Existing(target), MarkType(), &seed, sizeof(seed));
        FluxionUUID survivorId = Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "survivor");

        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Playback(buffer, scene) == 1);
        TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(scene, target));
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, MarkType()) == 0);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_FindByUUID(scene, survivorId)));

        Fluxion_EntityCommandBuffer_Destroy(buffer);
        Fluxion_Scene_Destroy(scene);
    }

    // --- What is refused at the moment of recording ---------------------

    {
        FluxionEntityCommandBuffer* buffer = Fluxion_EntityCommandBuffer_Create();
        FluxionGameObjectHandle nobody = Fluxion_GameObject_InvalidHandle();

        TestMark seed;
        seed.value = 3u;

        // A size that does not match the registered type would be a copy
        // of the wrong length into somebody's component, carried out
        // somewhere the caller is no longer standing. Refused here.
        TEST_CHECK(ctx, !Fluxion_EntityCommandBuffer_AddComponent(buffer, Fluxion_EntityTarget_Existing(nobody), MarkType(), &seed, sizeof(seed) + 4));
        TEST_CHECK(ctx, !Fluxion_EntityCommandBuffer_AddComponent(buffer, Fluxion_EntityTarget_Existing(nobody), MarkType(), &seed, 1));

        // As is a type the registry has never heard of.
        TEST_CHECK(ctx, !Fluxion_EntityCommandBuffer_AddComponent(buffer, Fluxion_EntityTarget_Existing(nobody),
            Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr("TestNeverRegistered")), &seed, sizeof(seed)));

        TEST_CHECK(ctx, !Fluxion_EntityCommandBuffer_RemoveComponent(buffer, Fluxion_EntityTarget_Existing(nobody), FLUXION_TYPE_ID_INVALID));

        // None of those were written down.
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == 0);

        Fluxion_EntityCommandBuffer_Destroy(buffer);
    }

    // --- A buffer never carries commands into a scene they were not for -

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionEntityCommandBuffer* buffer = Fluxion_EntityCommandBuffer_Create();

        Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "orphan");
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == 1);

        Fluxion_Scene_Destroy(scene);

        // The scene is gone, so the command cannot land -- and it is
        // thrown away rather than kept for whatever scene comes next.
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Playback(buffer, scene) == 1);
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == 0);

        FluxionSceneHandle next = Fluxion_Scene_Create();
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Playback(buffer, next) == 0);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(next) == 0);

        // Clearing throws commands away without carrying any of them out.
        Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "discarded");
        Fluxion_EntityCommandBuffer_Clear(buffer);
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == 0);
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Playback(buffer, next) == 0);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(next) == 0);

        Fluxion_EntityCommandBuffer_Destroy(buffer);
        Fluxion_Scene_Destroy(next);
    }

    // --- Enough commands that the storage has to grow --------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionEntityCommandBuffer* buffer = Fluxion_EntityCommandBuffer_Create();

        // Well past the first run of bytes, so what is already written
        // down has to survive being moved. A grow that dropped or
        // shortened the earlier commands would show up as the wrong count
        // or the wrong names here, not as a crash.
        constexpr u32 kCount = 200;
        static FluxionUUID ids[kCount];
        for (u32 i = 0; i < kCount; ++i)
        {
            char name[FLUXION_SCENE_MAX_NAME_LENGTH];
            std::snprintf(name, sizeof(name), "object %u", i);
            ids[i] = Fluxion_EntityCommandBuffer_CreateGameObject(buffer, name);
        }
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == kCount);

        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Playback(buffer, scene) == 0);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == kCount);

        u32 found = 0;
        for (u32 i = 0; i < kCount; ++i)
        {
            FluxionGameObjectHandle object = Fluxion_Scene_FindByUUID(scene, ids[i]);
            if (!Fluxion_GameObject_IsValid(scene, object)) continue;

            char expected[FLUXION_SCENE_MAX_NAME_LENGTH];
            std::snprintf(expected, sizeof(expected), "object %u", i);
            if (std::strcmp(Fluxion_GameObject_GetName(scene, object), expected) == 0) ++found;
        }
        TEST_CHECK(ctx, found == kCount);

        Fluxion_EntityCommandBuffer_Destroy(buffer);
        Fluxion_Scene_Destroy(scene);
    }

    // --- The scene's own buffer, played back at the end of a turn -------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionEntityCommandBuffer* buffer = Fluxion_Scene_GetCommandBuffer(scene);
        TEST_CHECK(ctx, buffer != nullptr);

        // Asked for twice, the same one comes back: a scene has one, not
        // one per caller.
        TEST_CHECK(ctx, Fluxion_Scene_GetCommandBuffer(scene) == buffer);
        TEST_CHECK(ctx, Fluxion_Scene_GetCommandBuffer(Fluxion_Scene_InvalidHandle()) == nullptr);

        FluxionUUID id = Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "deferred");
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 0);

        // No scripting runtime is attached here, and the turn still has to
        // carry out what was written down during it.
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 1);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_FindByUUID(scene, id)));
        TEST_CHECK(ctx, Fluxion_EntityCommandBuffer_Count(buffer) == 0);

        // A scene that is never asked for one still ticks.
        FluxionSceneHandle plain = Fluxion_Scene_Create();
        Fluxion_Scene_Tick(plain, 0.016f);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(plain) == 0);

        Fluxion_Scene_Destroy(plain);
        Fluxion_Scene_Destroy(scene);
    }

    // --- What the deferral is for: changing a set while walking it ------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        for (u32 i = 0; i < 8; ++i)
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "walked");
            TestMark seed;
            seed.value = i;
            Fluxion_GameObject_AddComponent(scene, object, MarkType(), &seed);
        }

        FluxionEntityCommandBuffer* buffer = Fluxion_Scene_GetCommandBuffer(scene);

        u32 count = 0;
        const TestMark* marks = (const TestMark*)Fluxion_Scene_GetComponentArray(scene, MarkType(), &count);
        const FluxionGameObjectHandle* owners = Fluxion_Scene_GetComponentOwners(scene, MarkType(), &count);
        TEST_CHECK(ctx, count == 8);

        // Every even one is asked to go while the walk is under way. The
        // walk sees all eight, because nothing has actually changed yet --
        // which is the whole point.
        u32 seen = 0;
        for (u32 row = 0; row < count; ++row)
        {
            ++seen;
            if ((marks[row].value % 2u) == 0u)
            {
                Fluxion_EntityCommandBuffer_RemoveComponent(buffer, Fluxion_EntityTarget_Existing(owners[row]), MarkType());
            }
        }
        TEST_CHECK(ctx, seen == 8);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, MarkType()) == 8);

        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, Fluxion_Scene_ComponentCount(scene, MarkType()) == 4);

        // And the four left are the odd ones, each still its own.
        u32 after = 0;
        const TestMark* remaining = (const TestMark*)Fluxion_Scene_GetComponentArray(scene, MarkType(), &after);
        u32 oddCount = 0;
        for (u32 row = 0; row < after; ++row)
        {
            if ((remaining[row].value % 2u) == 1u) ++oddCount;
        }
        TEST_CHECK(ctx, oddCount == 4);

        Fluxion_Scene_Destroy(scene);
    }

    Fluxion_Reflection_Shutdown();
}
