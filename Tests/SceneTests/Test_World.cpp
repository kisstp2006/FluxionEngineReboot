#include "TestFramework.h"

#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Scene/World.hpp>

#include <cstring>
#include <utility>

namespace
{

struct TestVelocity
{
    static constexpr auto Name = "TestVelocity";
    f32 x;
    f32 y;
};

void RegisterVelocity()
{
    static FluxionTypeInfo info;
    info.name = Fluxion_StringView_FromCStr(TestVelocity::Name);
    info.id = Fluxion::Core::TypeIdOf<TestVelocity>();
    info.kind = FLUXION_TYPE_KIND_STRUCT;
    info.size = sizeof(TestVelocity);
    info.version = 1;
    info.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
    info.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));
    Fluxion_Reflection_RegisterType(&info);
}

} // namespace

void Test_World_Run(TestContext& ctx)
{
    using Fluxion::Scene::Entity;
    using Fluxion::Scene::World;

    RegisterVelocity();

    // --- The C++ names reach the same objects as the C ones -------------

    {
        World world;
        TEST_CHECK(ctx, world.IsValid());

        Entity parent = world.Create("parent");
        Entity child = world.Create("child");
        TEST_CHECK(ctx, parent.IsValid());
        TEST_CHECK(ctx, std::strcmp(parent.Name(), "parent") == 0);

        child.SetParent(parent);
        TEST_CHECK(ctx, child.Parent() == parent);
        TEST_CHECK(ctx, parent.FirstChild() == child);
        TEST_CHECK(ctx, parent.ChildCount() == 1);

        // Put back up among the roots by a parent that names nothing.
        child.SetParent(Entity());
        TEST_CHECK(ctx, !child.Parent().IsValid());
        TEST_CHECK(ctx, parent.ChildCount() == 0);

        // The same object, reached the other way.
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(world.Handle(), parent.Handle()));
        TEST_CHECK(ctx, world.Find("parent") == parent);
        TEST_CHECK(ctx, world.FindByUUID(parent.UUID()) == parent);
        TEST_CHECK(ctx, world.EntityCount() == 2);

        // A default-built one names nothing and answers so rather than
        // reaching into a scene it does not have.
        Entity nobody;
        TEST_CHECK(ctx, !nobody.IsValid());
        TEST_CHECK(ctx, !nobody.Has<TestVelocity>());
        TEST_CHECK(ctx, nobody.Get<TestVelocity>() == nullptr);
        TEST_CHECK(ctx, nobody.Add<TestVelocity>() == nullptr);
        TEST_CHECK(ctx, !nobody.Remove<TestVelocity>());

        // Destroyed through the C++ name, gone as far as both are
        // concerned.
        parent.Destroy();
        TEST_CHECK(ctx, !parent.IsValid());
        TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(world.Handle(), parent.Handle()));
    }

    // --- Components named by type ---------------------------------------

    {
        World world;

        Entity first = world.Create("first");
        Entity second = world.Create("second");

        TestVelocity* zeroed = first.Add<TestVelocity>();
        TEST_CHECK(ctx, zeroed != nullptr);
        TEST_CHECK(ctx, zeroed != nullptr && zeroed->x == 0.0f && zeroed->y == 0.0f);

        TestVelocity seeded = { 3.0f, 4.0f };
        TestVelocity* stored = second.Add<TestVelocity>(seeded);
        TEST_CHECK(ctx, stored != nullptr && stored->x == 3.0f && stored->y == 4.0f);

        TEST_CHECK(ctx, first.Has<TestVelocity>());
        TEST_CHECK(ctx, world.ComponentCount<TestVelocity>() == 2);
        TEST_CHECK(ctx, world.CountWith<TestVelocity>() == 2);

        // Walked one entity at a time: every entity seen reaches the same
        // storage the entity itself reports, so the walk and the per-entity
        // lookup cannot be reading two different things.
        {
            u32 seen = 0;
            world.Each<TestVelocity>([&](Entity entity, TestVelocity& velocity)
            {
                ++seen;
                TEST_CHECK(ctx, entity.IsValid());
                TEST_CHECK(ctx, entity.Get<TestVelocity>() == &velocity);
            });
            TEST_CHECK(ctx, seen == 2);
        }

        // Walked a block at a time: the spans line up entry for entry, and
        // writing through one is writing the component.
        {
            u32 seen = 0;
            world.EachChunk<TestVelocity>([&](std::span<const FluxionEntityHandle> entities, std::span<TestVelocity> velocities)
            {
                TEST_CHECK(ctx, entities.size() == velocities.size());
                for (usize i = 0; i < entities.size(); ++i)
                {
                    Entity owner(world.Handle(), entities[i]);
                    TEST_CHECK(ctx, owner.Get<TestVelocity>() == &velocities[i]);
                    velocities[i].x = 11.0f;
                    ++seen;
                }
            });
            TEST_CHECK(ctx, seen == 2);
            TEST_CHECK(ctx, first.Get<TestVelocity>()->x == 11.0f);
            TEST_CHECK(ctx, second.Get<TestVelocity>()->x == 11.0f);
        }

        second.Get<TestVelocity>()->x = 3.0f;

        TEST_CHECK(ctx, first.Remove<TestVelocity>());
        TEST_CHECK(ctx, !first.Has<TestVelocity>());
        TEST_CHECK(ctx, world.ComponentCount<TestVelocity>() == 1);
        TEST_CHECK(ctx, second.Get<TestVelocity>() != nullptr);
        TEST_CHECK(ctx, second.Get<TestVelocity>()->x == 3.0f);

        // A type nothing carries yields no blocks at all, rather than one
        // empty block a caller would have to test for.
        World empty;
        {
            u32 blocks = 0;
            empty.EachChunk<TestVelocity>([&](std::span<const FluxionEntityHandle>, std::span<TestVelocity>) { ++blocks; });
            TEST_CHECK(ctx, blocks == 0);
            TEST_CHECK(ctx, empty.CountWith<TestVelocity>() == 0);
        }
    }

    // --- Who destroys the scene and who does not ------------------------

    {
        FluxionSceneHandle handle;
        {
            World world;
            handle = world.Handle();
            TEST_CHECK(ctx, Fluxion_Scene_IsValid(handle));
        }
        // The one that made it took it away again.
        TEST_CHECK(ctx, !Fluxion_Scene_IsValid(handle));
    }

    {
        FluxionSceneHandle handle = Fluxion_Scene_Create();
        {
            World borrowed = World::Borrow(handle);
            TEST_CHECK(ctx, borrowed.IsValid());
            borrowed.Create("made through the borrowed one");
        }
        // Standing in for a scene is not taking charge of it.
        TEST_CHECK(ctx, Fluxion_Scene_IsValid(handle));
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(handle) == 1);
        Fluxion_Scene_Destroy(handle);
    }

    {
        // Moved from, so the scene is destroyed once and by the one that
        // now holds it -- a second destroy would take away whatever scene
        // had since been given that slot.
        FluxionSceneHandle handle;
        {
            World original;
            handle = original.Handle();
            World moved = std::move(original);
            TEST_CHECK(ctx, moved.Handle().index == handle.index);
            TEST_CHECK(ctx, !original.IsValid());
            TEST_CHECK(ctx, Fluxion_Scene_IsValid(handle));
        }
        TEST_CHECK(ctx, !Fluxion_Scene_IsValid(handle));

        // The same through assignment, and the World assigned over gives
        // up the scene it was holding.
        FluxionSceneHandle replaced;
        FluxionSceneHandle kept;
        {
            World target;
            replaced = target.Handle();

            World source;
            kept = source.Handle();
            TEST_CHECK(ctx, replaced.index != kept.index || replaced.generation != kept.generation);

            target = std::move(source);
            TEST_CHECK(ctx, !Fluxion_Scene_IsValid(replaced));
            TEST_CHECK(ctx, Fluxion_Scene_IsValid(kept));
        }
        TEST_CHECK(ctx, !Fluxion_Scene_IsValid(kept));
    }

    // --- The command buffer, reached through the World ------------------

    {
        World world;
        FluxionEntityCommandBuffer* buffer = world.Commands();
        TEST_CHECK(ctx, buffer != nullptr);

        FluxionUUID id = Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "deferred");
        TEST_CHECK(ctx, world.EntityCount() == 0);

        world.Tick(0.016f);
        TEST_CHECK(ctx, world.EntityCount() == 1);
        TEST_CHECK(ctx, world.FindByUUID(id).IsValid());
    }
}
