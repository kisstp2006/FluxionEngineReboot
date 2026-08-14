#include "SceneFixture.h"

#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Scene/ScriptReflection.h>
#include <Fluxion/Scene/Transform.h>

#include <cstring>

// Whether a script component is described in the same place a native one
// is, and whether that description actually reaches the values.
//
// The point of the whole exercise is that something writing a scene out,
// or showing it, needs to know one way of asking what a thing holds. So
// the checks below deliberately ask through the REGISTRY -- never through
// the scripting runtime's own headers -- and read the values through the
// property descriptions rather than through anything that knows what a
// script is.

namespace
{

const char* const kSource = R"(
class Player : Component
{
    int health;
    float speed;
    bool alive;
    string title;

    void Awake()
    {
        health = 100;
        speed = 2.5;
        alive = true;
        title = "hero";
    }
}

class Marker : Component
{
    int tag;
}
)";

const FluxionPropertyInfo* FindProperty(const FluxionTypeInfo* type, const char* name)
{
    if (type == nullptr) return nullptr;
    for (usize i = 0; i < type->members.count; ++i)
    {
        const auto* property = (const FluxionPropertyInfo*)Fluxion_Span_At(type->members, i);
        if (std::strncmp(property->name.data, name, property->name.length) == 0 &&
            std::strlen(name) == property->name.length)
        {
            return property;
        }
    }
    return nullptr;
}

} // namespace

void Test_ScriptReflection_Run(TestContext& ctx)
{
    // --- A script class is a registered type, like any other -------------

    {
        ScriptedScene scene;
        if (!scene.Start(ctx, "script reflection", kSource)) return;

        FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "player");
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(object));

        const u32 playerClass = Fluxion::Script::FindClass(scene.Machine(), "Player");
        TEST_CHECK(ctx, playerClass != Fluxion::Script::kNoClass);
        Fluxion::Scene::AddComponent(scene.Scene(), object, playerClass);

        // Awake has not run yet, so the fields still hold what the class
        // starts them at. One turn, and they hold what it wrote.
        Fluxion_Scene_Tick(scene.Scene(), 0.016f);

        // Asked entirely through the registry: which script components are
        // on this object, what type each is, and what that type holds.
        FluxionScriptComponentRef refs[4];
        const u32 count = Fluxion_GameObject_GetScriptComponents(scene.Scene(), object, refs, 4);
        TEST_CHECK(ctx, count == 1);
        if (count != 1) return;

        const FluxionTypeInfo* type = Fluxion_Reflection_FindTypeById(refs[0].type);
        TEST_CHECK(ctx, type != nullptr);
        if (type == nullptr) return;

        // Four fields, and the two the class every component is built on
        // holds are NOT among them -- those are handed over afresh
        // whenever an instance is made, so describing them would invite
        // something to write them.
        TEST_CHECK(ctx, type->members.count == 4);

        // A script class has no size, which is what stops one being
        // attached as a data component: the storage takes a component's
        // size from here and refuses a type with none.
        TEST_CHECK(ctx, type->size == 0);
        TEST_CHECK(ctx, Fluxion_GameObject_AddComponent(scene.Scene(), object, refs[0].type, nullptr) == nullptr);

        // --- and the description reaches the values ----------------------

        {
            const FluxionPropertyInfo* health = FindProperty(type, "health");
            const FluxionPropertyInfo* speed = FindProperty(type, "speed");
            const FluxionPropertyInfo* alive = FindProperty(type, "alive");
            const FluxionPropertyInfo* title = FindProperty(type, "title");

            TEST_CHECK(ctx, health != nullptr && speed != nullptr && alive != nullptr && title != nullptr);
            if (health == nullptr || speed == nullptr || alive == nullptr || title == nullptr) return;

            // Every one of them is reached the accessor way, because a
            // script field is a slot in an object of another machine and
            // has no offset in a struct this side could add to a pointer.
            TEST_CHECK(ctx, health->accessKind == FLUXION_PROPERTY_ACCESS_ACCESSOR);
            TEST_CHECK(ctx, health->type == FLUXION_TYPE_ID_OF(i32));
            TEST_CHECK(ctx, speed->type == FLUXION_TYPE_ID_OF(f32));
            TEST_CHECK(ctx, alive->type == FLUXION_TYPE_ID_OF(bool));

            i32 healthValue = 0;
            f32 speedValue = 0.0f;
            bool aliveValue = false;
            const char* titleValue = nullptr;

            health->accessor.getter(&refs[0].instance, &healthValue, health->accessor.context);
            speed->accessor.getter(&refs[0].instance, &speedValue, speed->accessor.context);
            alive->accessor.getter(&refs[0].instance, &aliveValue, alive->accessor.context);
            title->accessor.getter(&refs[0].instance, &titleValue, title->accessor.context);

            TEST_CHECK(ctx, healthValue == 100);
            TEST_CHECK(ctx, speedValue > 2.4f && speedValue < 2.6f);
            TEST_CHECK(ctx, aliveValue);
            TEST_CHECK(ctx, titleValue != nullptr && std::strcmp(titleValue, "hero") == 0);

            // And written back the same way, which is what reading a scene
            // in will do.
            {
                const i32 restored = 42;
                health->accessor.setter(&refs[0].instance, &restored, health->accessor.context);
            }
            healthValue = 0;
            health->accessor.getter(&refs[0].instance, &healthValue, health->accessor.context);
            TEST_CHECK(ctx, healthValue == 42);

            // Two properties of the same type must not be the same
            // property: one pair of functions serves them all, so what
            // tells them apart is the context, and nothing else can.
            TEST_CHECK(ctx, health->accessor.context != speed->accessor.context);
            TEST_CHECK(ctx, health->accessor.getter == speed->accessor.getter);
        }

        // A second class is its own type, with its own fields.
        {
            const u32 markerClass = Fluxion::Script::FindClass(scene.Machine(), "Marker");
            TEST_CHECK(ctx, markerClass != Fluxion::Script::kNoClass);
            Fluxion::Scene::AddComponent(scene.Scene(), object, markerClass);

            FluxionScriptComponentRef both[4];
            const u32 now = Fluxion_GameObject_GetScriptComponents(scene.Scene(), object, both, 4);
            TEST_CHECK(ctx, now == 2);
            TEST_CHECK(ctx, both[0].type != both[1].type);

            const FluxionTypeInfo* marker = Fluxion_Reflection_FindTypeById(
                Fluxion_Scene_ScriptClassTypeId(scene.Scene(), markerClass));
            TEST_CHECK(ctx, marker != nullptr);
            TEST_CHECK(ctx, marker != nullptr && marker->members.count == 1);
            TEST_CHECK(ctx, FindProperty(marker, "tag") != nullptr);
            TEST_CHECK(ctx, FindProperty(marker, "health") == nullptr);
        }

        // Asking for the count alone writes nothing.
        TEST_CHECK(ctx, Fluxion_GameObject_GetScriptComponents(scene.Scene(), object, nullptr, 0) == 2);

        // The native and the script halves answer through the same
        // registry, which is the whole point: one place to ask.
        {
            const FluxionTypeInfo* transform = Fluxion_Reflection_FindTypeById(Fluxion_Transform_TypeId());
            TEST_CHECK(ctx, transform != nullptr);
            TEST_CHECK(ctx, transform != nullptr && transform->members.count == 3);
        }
    }

    // --- Nothing is left describing a module that has gone ---------------

    {
        const FluxionTypeId playerType = Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr("Player"));

        {
            ScriptedScene scene;
            if (!scene.Start(ctx, "script reflection lifetime", kSource)) return;
            TEST_CHECK(ctx, Fluxion_Reflection_FindTypeById(playerType) != nullptr);
        }

        // The scene is gone, and with it the storage the registry was
        // pointing at. A description that outlived it would be a pointer
        // to freed memory that the next thing to walk the registry would
        // follow.
        TEST_CHECK(ctx, Fluxion_Reflection_FindTypeById(playerType) == nullptr);
    }
}
