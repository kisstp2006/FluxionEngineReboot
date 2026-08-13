#include "TestFramework.h"

#include <Fluxion/Scene/Scene.h>

#include <cstring>

namespace
{

bool SameObject(FluxionGameObjectHandle a, FluxionGameObjectHandle b)
{
    return a.index == b.index && a.generation == b.generation;
}

} // namespace

void Test_Hierarchy_Run(TestContext& ctx)
{
    // --- A scene starts empty and hands out distinct objects ------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        TEST_CHECK(ctx, Fluxion_Scene_IsValid(scene));
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 0);
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_GetFirstRoot(scene)));

        FluxionGameObjectHandle first = Fluxion_Scene_CreateGameObject(scene, "first");
        FluxionGameObjectHandle second = Fluxion_Scene_CreateGameObject(scene, "second");
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, first));
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, second));
        TEST_CHECK(ctx, !SameObject(first, second));
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 2);

        TEST_CHECK(ctx, std::strcmp(Fluxion_GameObject_GetName(scene, first), "first") == 0);
        Fluxion_GameObject_SetName(scene, first, "renamed");
        TEST_CHECK(ctx, std::strcmp(Fluxion_GameObject_GetName(scene, first), "renamed") == 0);

        // Both are roots, in the order they were made.
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_GetFirstRoot(scene), first));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetNextSibling(scene, first), second));

        Fluxion_Scene_Destroy(scene);
        TEST_CHECK(ctx, !Fluxion_Scene_IsValid(scene));

        // A handle to what used to be there is refused rather than
        // answered with whatever now occupies the record.
        TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(scene, first));
    }

    // --- Parents, children and siblings ---------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionGameObjectHandle parent = Fluxion_Scene_CreateGameObject(scene, "parent");
        FluxionGameObjectHandle a = Fluxion_Scene_CreateGameObject(scene, "a");
        FluxionGameObjectHandle b = Fluxion_Scene_CreateGameObject(scene, "b");
        FluxionGameObjectHandle c = Fluxion_Scene_CreateGameObject(scene, "c");

        Fluxion_GameObject_SetParent(scene, a, parent);
        Fluxion_GameObject_SetParent(scene, b, parent);
        Fluxion_GameObject_SetParent(scene, c, parent);

        TEST_CHECK(ctx, Fluxion_GameObject_GetChildCount(scene, parent) == 3);
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetParent(scene, a), parent));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetFirstChild(scene, parent), a));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetNextSibling(scene, a), b));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetNextSibling(scene, b), c));
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_GetNextSibling(scene, c)));

        // Only the parent is a root now.
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_GetFirstRoot(scene), parent));
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_GetNextSibling(scene, parent)));

        // Taking one out again leaves the rest of the list joined up.
        Fluxion_GameObject_SetParent(scene, b, Fluxion_GameObject_InvalidHandle());
        TEST_CHECK(ctx, Fluxion_GameObject_GetChildCount(scene, parent) == 2);
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetNextSibling(scene, a), c));
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_GetParent(scene, b)));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetNextSibling(scene, parent), b));

        // Nothing may be put under itself or under anything below it.
        Fluxion_GameObject_SetParent(scene, parent, parent);
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_GetParent(scene, parent)));
        Fluxion_GameObject_SetParent(scene, parent, a);
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_GetParent(scene, parent)));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_GetParent(scene, a), parent));

        Fluxion_Scene_Destroy(scene);
    }

    // --- Finding one by name --------------------------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionGameObjectHandle root = Fluxion_Scene_CreateGameObject(scene, "root");
        FluxionGameObjectHandle middle = Fluxion_Scene_CreateGameObject(scene, "middle");
        FluxionGameObjectHandle leaf = Fluxion_Scene_CreateGameObject(scene, "leaf");
        FluxionGameObjectHandle other = Fluxion_Scene_CreateGameObject(scene, "other");

        Fluxion_GameObject_SetParent(scene, middle, root);
        Fluxion_GameObject_SetParent(scene, leaf, middle);
        Fluxion_GameObject_SetParent(scene, other, root);

        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_FindChild(scene, root, "middle"), middle));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_FindChild(scene, root, "other"), other));

        // Directly is directly: a grandchild is not a child.
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_FindChild(scene, root, "leaf")));
        TEST_CHECK(ctx, SameObject(Fluxion_GameObject_FindChildRecursive(scene, root, "leaf"), leaf));
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_FindChildRecursive(scene, root, "nothing")));

        TEST_CHECK(ctx, SameObject(Fluxion_Scene_Find(scene, "leaf"), leaf));
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_Find(scene, "root"), root));
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_Find(scene, "nothing")));

        Fluxion_Scene_Destroy(scene);
    }

    // --- Destroying a parent takes everything below it ------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        FluxionGameObjectHandle keep = Fluxion_Scene_CreateGameObject(scene, "keep");
        FluxionGameObjectHandle root = Fluxion_Scene_CreateGameObject(scene, "root");
        FluxionGameObjectHandle child = Fluxion_Scene_CreateGameObject(scene, "child");
        FluxionGameObjectHandle grandchild = Fluxion_Scene_CreateGameObject(scene, "grandchild");
        FluxionGameObjectHandle sibling = Fluxion_Scene_CreateGameObject(scene, "sibling");

        Fluxion_GameObject_SetParent(scene, child, root);
        Fluxion_GameObject_SetParent(scene, grandchild, child);
        Fluxion_GameObject_SetParent(scene, sibling, root);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 5);

        Fluxion_GameObject_Destroy(scene, root);

        TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(scene, root));
        TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(scene, child));
        TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(scene, grandchild));
        TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(scene, sibling));

        // And nothing outside that subtree was touched.
        TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, keep));
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 1);
        TEST_CHECK(ctx, SameObject(Fluxion_Scene_GetFirstRoot(scene), keep));
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_GameObject_GetNextSibling(scene, keep)));

        Fluxion_Scene_Destroy(scene);
    }
}
