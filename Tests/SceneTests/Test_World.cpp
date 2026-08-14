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

    Fluxion_Reflection_Init();
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

        // The typed view and the owners beside it are the same set the
        // objects report one by one, position for position.
        std::span<TestVelocity> view = world.View<TestVelocity>();
        TEST_CHECK(ctx, view.size() == 2);
        for (usize i = 0; i < view.size(); ++i)
        {
            Entity owner = world.OwnerAt<TestVelocity>(i);
            TEST_CHECK(ctx, owner.IsValid());
            TEST_CHECK(ctx, owner.Get<TestVelocity>() == &view[i]);
        }
        TEST_CHECK(ctx, world.Owners<TestVelocity>().size() == 2);

        // Writing through the view is writing the component.
        view[0].x = 11.0f;
        TEST_CHECK(ctx, world.OwnerAt<TestVelocity>(0).Get<TestVelocity>()->x == 11.0f);

        // Past the end names nothing rather than reading off the end.
        TEST_CHECK(ctx, !world.OwnerAt<TestVelocity>(2).IsValid());

        TEST_CHECK(ctx, first.Remove<TestVelocity>());
        TEST_CHECK(ctx, !first.Has<TestVelocity>());
        TEST_CHECK(ctx, world.ComponentCount<TestVelocity>() == 1);
        TEST_CHECK(ctx, second.Get<TestVelocity>() != nullptr);
        TEST_CHECK(ctx, second.Get<TestVelocity>()->x == 3.0f);

        // A type nothing carries is an empty view, not a null one to be
        // checked for.
        World empty;
        TEST_CHECK(ctx, empty.View<TestVelocity>().empty());
        TEST_CHECK(ctx, empty.Owners<TestVelocity>().empty());
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

    Fluxion_Reflection_Shutdown();
}
