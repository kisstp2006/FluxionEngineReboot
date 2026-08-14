#include <Fluxion/Scene/Scene.h>

#include "SceneInternal.h"

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

static FluxionSceneRecord s_scenes[FLUXION_SCENE_MAX_SCENES];

FluxionSceneHandle Fluxion_Scene_InvalidHandle(void)
{
    FluxionSceneHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return invalid;
}

FluxionGameObjectHandle Fluxion_GameObject_InvalidHandle(void)
{
    FluxionGameObjectHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    return invalid;
}

FluxionSceneRecord* Fluxion_SceneInternal_Resolve(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record;
    if (scene.index >= FLUXION_SCENE_MAX_SCENES) return NULL;
    record = &s_scenes[scene.index];
    if (!record->alive || record->generation != scene.generation) return NULL;
    return record;
}

FluxionSceneGameObjectRecord* Fluxion_SceneInternal_ResolveObject(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry;
    if (record == NULL || object.index >= FLUXION_SCENE_MAX_GAME_OBJECTS) return NULL;
    entry = &record->objects[object.index];
    if (!entry->alive || entry->generation != object.generation) return NULL;
    return entry;
}

void Fluxion_SceneInternal_SetError(FluxionSceneRecord* record, const char* message)
{
    if (record == NULL) return;
    if (message == NULL)
    {
        record->lastError[0] = '\0';
        return;
    }

    {
        usize length = strlen(message);
        if (length >= sizeof(record->lastError)) length = sizeof(record->lastError) - 1;
        memcpy(record->lastError, message, length);
        record->lastError[length] = '\0';
    }
}

// --- Scenes -------------------------------------------------------------

FluxionSceneHandle Fluxion_Scene_Create(void)
{
    FluxionSceneHandle handle = Fluxion_Scene_InvalidHandle();
    u32 index = FLUXION_SCENE_MAX_SCENES;
    FluxionSceneRecord* record;
    u32 i;

    for (i = 0; i < FLUXION_SCENE_MAX_SCENES; ++i)
    {
        if (!s_scenes[i].alive) { index = i; break; }
    }
    if (index == FLUXION_SCENE_MAX_SCENES) return handle;

    record = &s_scenes[index];
    {
        // The generation is the one thing that must survive being reset,
        // so a handle to the scene that used to live here is refused
        // rather than answered.
        u32 generation = record->generation + 1;
        memset(record, 0, sizeof(*record));
        record->generation = generation;
    }
    record->alive = true;
    record->firstRoot = Fluxion_GameObject_InvalidHandle();

    handle.index = index;
    handle.generation = record->generation;
    record->self = handle;
    return handle;
}

void Fluxion_Scene_Destroy(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == NULL) return;

    while (FLUXION_HANDLE_IS_VALID(record->firstRoot))
    {
        FluxionGameObjectHandle root = record->firstRoot;
        Fluxion_GameObject_Destroy(scene, root);

        // A root that refused to go would spin here forever; nothing can
        // refuse, but the check costs nothing and says so.
        FLUXION_ASSERT_MSG(!(record->firstRoot.index == root.index && record->firstRoot.generation == root.generation),
            "Fluxion: destroying a scene left one of its objects standing");
        if (record->firstRoot.index == root.index && record->firstRoot.generation == root.generation) break;
    }

    Fluxion_SceneComponents_ReleaseScene(record);
    Fluxion_SceneComponentPool_ReleaseScene(record);
    Fluxion_SceneInternal_ReleaseCommandBuffer(record);
    record->alive = false;
}

bool Fluxion_Scene_IsValid(FluxionSceneHandle scene)
{
    return Fluxion_SceneInternal_Resolve(scene) != NULL;
}

u32 Fluxion_Scene_GameObjectCount(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    return record != NULL ? record->objectCount : 0u;
}

FluxionGameObjectHandle Fluxion_Scene_GetFirstRoot(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    return record != NULL ? record->firstRoot : Fluxion_GameObject_InvalidHandle();
}

const char* Fluxion_Scene_GetLastError(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    return record != NULL ? record->lastError : "";
}

// --- Game objects -------------------------------------------------------

static void Fluxion_SceneInternal_CopyName(char* destination, const char* name)
{
    usize length;
    if (name == NULL)
    {
        destination[0] = '\0';
        return;
    }

    length = strlen(name);
    if (length >= FLUXION_SCENE_MAX_NAME_LENGTH) length = FLUXION_SCENE_MAX_NAME_LENGTH - 1;
    memcpy(destination, name, length);
    destination[length] = '\0';
}

// Puts the object at the end of whichever list it belongs in, so children
// and roots come back out in the order they were added.
static void Fluxion_SceneInternal_Link(FluxionSceneRecord* record, FluxionGameObjectHandle object, FluxionGameObjectHandle parent)
{
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionSceneGameObjectRecord* parentEntry = Fluxion_SceneInternal_ResolveObject(record, parent);
    FluxionGameObjectHandle* head;
    FluxionGameObjectHandle cursor;

    if (entry == NULL) return;

    entry->parent = (parentEntry != NULL) ? parent : Fluxion_GameObject_InvalidHandle();
    entry->nextSibling = Fluxion_GameObject_InvalidHandle();

    head = (parentEntry != NULL) ? &parentEntry->firstChild : &record->firstRoot;
    if (!FLUXION_HANDLE_IS_VALID(*head))
    {
        *head = object;
        return;
    }

    cursor = *head;
    for (;;)
    {
        FluxionSceneGameObjectRecord* current = Fluxion_SceneInternal_ResolveObject(record, cursor);
        if (current == NULL) return;
        if (!FLUXION_HANDLE_IS_VALID(current->nextSibling))
        {
            current->nextSibling = object;
            return;
        }
        cursor = current->nextSibling;
    }
}

void Fluxion_SceneInternal_Unlink(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionSceneGameObjectRecord* parentEntry;
    FluxionGameObjectHandle* head;
    FluxionGameObjectHandle cursor;

    if (entry == NULL) return;

    parentEntry = Fluxion_SceneInternal_ResolveObject(record, entry->parent);
    head = (parentEntry != NULL) ? &parentEntry->firstChild : &record->firstRoot;

    if (head->index == object.index && head->generation == object.generation)
    {
        *head = entry->nextSibling;
        entry->nextSibling = Fluxion_GameObject_InvalidHandle();
        entry->parent = Fluxion_GameObject_InvalidHandle();
        return;
    }

    cursor = *head;
    while (FLUXION_HANDLE_IS_VALID(cursor))
    {
        FluxionSceneGameObjectRecord* current = Fluxion_SceneInternal_ResolveObject(record, cursor);
        if (current == NULL) return;
        if (current->nextSibling.index == object.index && current->nextSibling.generation == object.generation)
        {
            current->nextSibling = entry->nextSibling;
            entry->nextSibling = Fluxion_GameObject_InvalidHandle();
            entry->parent = Fluxion_GameObject_InvalidHandle();
            return;
        }
        cursor = current->nextSibling;
    }
}

// Both ways of making an object come through here; they differ only in
// where the id comes from, which the caller has settled by this point.
static FluxionGameObjectHandle Fluxion_SceneInternal_CreateGameObject(FluxionSceneHandle scene, const char* name, FluxionUUID uuid)
{
    FluxionGameObjectHandle handle = Fluxion_GameObject_InvalidHandle();
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry;
    u32 index = FLUXION_SCENE_MAX_GAME_OBJECTS;
    u32 i;

    if (record == NULL) return handle;

    for (i = 0; i < FLUXION_SCENE_MAX_GAME_OBJECTS; ++i)
    {
        if (!record->objects[i].alive) { index = i; break; }
    }
    if (index == FLUXION_SCENE_MAX_GAME_OBJECTS)
    {
        Fluxion_SceneInternal_SetError(record, "this scene already holds as many objects as one may hold");
        return handle;
    }

    entry = &record->objects[index];
    {
        u32 generation = entry->generation + 1;
        memset(entry, 0, sizeof(*entry));
        entry->generation = generation;
    }
    entry->alive = true;
    entry->uuid = uuid;
    Fluxion_SceneInternal_CopyName(entry->name, name);
    entry->parent = Fluxion_GameObject_InvalidHandle();
    entry->firstChild = Fluxion_GameObject_InvalidHandle();
    entry->nextSibling = Fluxion_GameObject_InvalidHandle();
    entry->localPosition.x = 0.0f;
    entry->localPosition.y = 0.0f;
    entry->localPosition.z = 0.0f;
    entry->localRotation = Fluxion_Quat_Identity();
    entry->localScale.x = 1.0f;
    entry->localScale.y = 1.0f;
    entry->localScale.z = 1.0f;
    entry->worldMatrix = Fluxion_Mat4_Identity();
    entry->worldDirty = true;
    entry->firstComponent = FLUXION_SCENE_NO_COMPONENT;

    handle.index = index;
    handle.generation = entry->generation;

    Fluxion_SceneInternal_Link(record, handle, Fluxion_GameObject_InvalidHandle());
    ++record->objectCount;
    return handle;
}

FluxionGameObjectHandle Fluxion_Scene_CreateGameObject(FluxionSceneHandle scene, const char* name)
{
    return Fluxion_SceneInternal_CreateGameObject(scene, name, Fluxion_UUID_Generate());
}

FluxionGameObjectHandle Fluxion_Scene_CreateGameObjectWithUUID(FluxionSceneHandle scene, const char* name, FluxionUUID uuid)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == NULL) return Fluxion_GameObject_InvalidHandle();

    if (Fluxion_UUID_IsNil(uuid))
    {
        Fluxion_SceneInternal_SetError(record, "an object cannot be given the nil id");
        return Fluxion_GameObject_InvalidHandle();
    }

    // Refused rather than allowed and sorted out later: the whole point of
    // the id is that it names one object, and a second object answering to
    // it would make every lookup a coin toss.
    if (FLUXION_HANDLE_IS_VALID(Fluxion_Scene_FindByUUID(scene, uuid)))
    {
        Fluxion_SceneInternal_SetError(record, "this scene already holds an object with that id");
        return Fluxion_GameObject_InvalidHandle();
    }

    return Fluxion_SceneInternal_CreateGameObject(scene, name, uuid);
}

FluxionUUID Fluxion_GameObject_GetUUID(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionUUID nil;

    if (entry != NULL) return entry->uuid;

    memset(&nil, 0, sizeof(nil));
    return nil;
}

FluxionGameObjectHandle Fluxion_Scene_FindByUUID(FluxionSceneHandle scene, FluxionUUID uuid)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    u32 i;

    if (record == NULL || Fluxion_UUID_IsNil(uuid)) return Fluxion_GameObject_InvalidHandle();

    // Walked rather than looked up. The table has a fixed and small upper
    // bound, and an index beside it would be one more thing that has to
    // stay true through every create, destroy and reuse -- worth adding
    // only once a caller is shown to be waiting on this.
    for (i = 0; i < FLUXION_SCENE_MAX_GAME_OBJECTS; ++i)
    {
        FluxionSceneGameObjectRecord* entry = &record->objects[i];
        if (!entry->alive) continue;
        if (!Fluxion_UUID_Equals(entry->uuid, uuid)) continue;

        {
            FluxionGameObjectHandle handle;
            handle.index = i;
            handle.generation = entry->generation;
            return handle;
        }
    }

    return Fluxion_GameObject_InvalidHandle();
}

void Fluxion_SceneInternal_FreeObject(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry;
    if (record == NULL || object.index >= FLUXION_SCENE_MAX_GAME_OBJECTS) return;

    entry = &record->objects[object.index];
    if (!entry->alive || entry->generation != object.generation) return;

    // Here rather than where destruction was asked for: this index is
    // about to be free to hand out again, and a data component still
    // standing on it would be found by whoever gets it next.
    Fluxion_SceneComponentPool_RemoveAllOf(record, object.index);

    entry->alive = false;
    entry->pendingDestroy = false;
    if (record->objectCount != 0) --record->objectCount;
}

// Everything below `object`, deepest first, so a parent is never freed
// while something still points up at it.
static void Fluxion_SceneInternal_DestroySubtree(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL) return;

    while (FLUXION_HANDLE_IS_VALID(entry->firstChild))
    {
        FluxionGameObjectHandle child = entry->firstChild;
        Fluxion_SceneInternal_DestroySubtree(record, child);

        FLUXION_ASSERT_MSG(!(entry->firstChild.index == child.index && entry->firstChild.generation == child.generation),
            "Fluxion: destroying a game object left one of its children standing");
        if (entry->firstChild.index == child.index && entry->firstChild.generation == child.generation) break;
    }

    Fluxion_SceneInternal_Unlink(record, object);
    Fluxion_SceneInternal_FreeObject(record, object);
}

void Fluxion_SceneInternal_MarkSubtreePendingDestroy(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionGameObjectHandle cursor;
    if (entry == NULL) return;

    entry->pendingDestroy = true;

    cursor = entry->firstChild;
    while (FLUXION_HANDLE_IS_VALID(cursor))
    {
        FluxionSceneGameObjectRecord* child = Fluxion_SceneInternal_ResolveObject(record, cursor);
        if (child == NULL) break;
        Fluxion_SceneInternal_MarkSubtreePendingDestroy(record, cursor);
        cursor = child->nextSibling;
    }
}

void Fluxion_SceneInternal_FreePendingObjects(FluxionSceneRecord* record)
{
    u32 i;
    if (record == NULL || !record->objectsPendingDestroy) return;

    for (i = 0; i < FLUXION_SCENE_MAX_GAME_OBJECTS; ++i)
    {
        FluxionGameObjectHandle handle;
        if (!record->objects[i].alive || !record->objects[i].pendingDestroy) continue;

        // Freeing one takes everything below it with it, so a subtree
        // whose top is reached first leaves nothing for the rest of this
        // walk to find.
        handle.index = i;
        handle.generation = record->objects[i].generation;
        Fluxion_SceneInternal_DestroySubtree(record, handle);
    }
    record->objectsPendingDestroy = false;
}

void Fluxion_GameObject_Destroy(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL || entry->pendingDestroy) return;

    // Whatever is standing on any of this is told first, because a
    // component being told it is going may still want to read the object
    // it was on.
    Fluxion_SceneComponents_MarkSubtree(record, object);

    if (record->dispatching)
    {
        Fluxion_SceneInternal_MarkSubtreePendingDestroy(record, object);
        record->objectsPendingDestroy = true;
        return;
    }

    Fluxion_SceneComponents_Flush(record);
    Fluxion_SceneInternal_DestroySubtree(record, object);
}

bool Fluxion_GameObject_IsValid(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    return entry != NULL && !entry->pendingDestroy;
}

const char* Fluxion_GameObject_GetName(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    return entry != NULL ? entry->name : "";
}

void Fluxion_GameObject_SetName(FluxionSceneHandle scene, FluxionGameObjectHandle object, const char* name)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL) return;
    Fluxion_SceneInternal_CopyName(entry->name, name);
}

// --- Hierarchy ----------------------------------------------------------

static bool Fluxion_SceneInternal_IsBelow(FluxionSceneRecord* record, FluxionGameObjectHandle candidate, FluxionGameObjectHandle ancestor)
{
    FluxionGameObjectHandle cursor = candidate;
    while (FLUXION_HANDLE_IS_VALID(cursor))
    {
        FluxionSceneGameObjectRecord* entry;
        if (cursor.index == ancestor.index && cursor.generation == ancestor.generation) return true;
        entry = Fluxion_SceneInternal_ResolveObject(record, cursor);
        if (entry == NULL) return false;
        cursor = entry->parent;
    }
    return false;
}

void Fluxion_GameObject_SetParent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionGameObjectHandle parent)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionSceneGameObjectRecord* parentEntry = Fluxion_SceneInternal_ResolveObject(record, parent);
    if (entry == NULL) return;

    if (parentEntry != NULL && Fluxion_SceneInternal_IsBelow(record, parent, object))
    {
        Fluxion_SceneInternal_SetError(record, "an object cannot be put under itself or under anything already below it");
        return;
    }

    Fluxion_SceneInternal_Unlink(record, object);
    Fluxion_SceneInternal_Link(record, object, parentEntry != NULL ? parent : Fluxion_GameObject_InvalidHandle());
    Fluxion_SceneInternal_MarkWorldDirty(record, object);
}

FluxionGameObjectHandle Fluxion_GameObject_GetParent(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    return entry != NULL ? entry->parent : Fluxion_GameObject_InvalidHandle();
}

FluxionGameObjectHandle Fluxion_GameObject_GetFirstChild(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    return entry != NULL ? entry->firstChild : Fluxion_GameObject_InvalidHandle();
}

FluxionGameObjectHandle Fluxion_GameObject_GetNextSibling(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    return entry != NULL ? entry->nextSibling : Fluxion_GameObject_InvalidHandle();
}

u32 Fluxion_GameObject_GetChildCount(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionGameObjectHandle cursor;
    u32 count = 0;

    if (entry == NULL) return 0;

    cursor = entry->firstChild;
    while (FLUXION_HANDLE_IS_VALID(cursor))
    {
        FluxionSceneGameObjectRecord* child = Fluxion_SceneInternal_ResolveObject(record, cursor);
        if (child == NULL) break;
        ++count;
        cursor = child->nextSibling;
    }
    return count;
}

static FluxionGameObjectHandle Fluxion_SceneInternal_FindAmong(FluxionSceneRecord* record, FluxionGameObjectHandle first,
    const char* name, bool recursive)
{
    FluxionGameObjectHandle cursor = first;
    while (FLUXION_HANDLE_IS_VALID(cursor))
    {
        FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, cursor);
        if (entry == NULL) break;
        if (strcmp(entry->name, name) == 0) return cursor;
        cursor = entry->nextSibling;
    }

    if (!recursive) return Fluxion_GameObject_InvalidHandle();

    // Every name at this level has been tried before any below it, so a
    // shallower match always wins over a deeper one.
    cursor = first;
    while (FLUXION_HANDLE_IS_VALID(cursor))
    {
        FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, cursor);
        FluxionGameObjectHandle found;
        if (entry == NULL) break;

        found = Fluxion_SceneInternal_FindAmong(record, entry->firstChild, name, true);
        if (FLUXION_HANDLE_IS_VALID(found)) return found;
        cursor = entry->nextSibling;
    }
    return Fluxion_GameObject_InvalidHandle();
}

FluxionGameObjectHandle Fluxion_GameObject_FindChild(FluxionSceneHandle scene, FluxionGameObjectHandle object, const char* name)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL || name == NULL) return Fluxion_GameObject_InvalidHandle();
    return Fluxion_SceneInternal_FindAmong(record, entry->firstChild, name, false);
}

FluxionGameObjectHandle Fluxion_GameObject_FindChildRecursive(FluxionSceneHandle scene, FluxionGameObjectHandle object, const char* name)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL || name == NULL) return Fluxion_GameObject_InvalidHandle();
    return Fluxion_SceneInternal_FindAmong(record, entry->firstChild, name, true);
}

FluxionGameObjectHandle Fluxion_Scene_Find(FluxionSceneHandle scene, const char* name)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == NULL || name == NULL) return Fluxion_GameObject_InvalidHandle();
    return Fluxion_SceneInternal_FindAmong(record, record->firstRoot, name, true);
}

// --- Transforms ---------------------------------------------------------

void Fluxion_SceneInternal_MarkWorldDirty(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionGameObjectHandle cursor;
    if (entry == NULL) return;

    entry->worldDirty = true;

    cursor = entry->firstChild;
    while (FLUXION_HANDLE_IS_VALID(cursor))
    {
        FluxionSceneGameObjectRecord* child = Fluxion_SceneInternal_ResolveObject(record, cursor);
        if (child == NULL) break;
        Fluxion_SceneInternal_MarkWorldDirty(record, cursor);
        cursor = child->nextSibling;
    }
}

void Fluxion_GameObject_SetLocalPosition(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionVec3 position)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL) return;
    entry->localPosition = position;
    Fluxion_SceneInternal_MarkWorldDirty(record, object);
}

FluxionVec3 Fluxion_GameObject_GetLocalPosition(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry != NULL) return entry->localPosition;
    {
        FluxionVec3 zero = { 0.0f, 0.0f, 0.0f };
        return zero;
    }
}

void Fluxion_GameObject_SetLocalRotation(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionQuat rotation)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL) return;
    entry->localRotation = rotation;
    Fluxion_SceneInternal_MarkWorldDirty(record, object);
}

FluxionQuat Fluxion_GameObject_GetLocalRotation(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry != NULL) return entry->localRotation;
    return Fluxion_Quat_Identity();
}

void Fluxion_GameObject_SetLocalScale(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionVec3 scale)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL) return;
    entry->localScale = scale;
    Fluxion_SceneInternal_MarkWorldDirty(record, object);
}

FluxionVec3 Fluxion_GameObject_GetLocalScale(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry != NULL) return entry->localScale;
    {
        FluxionVec3 one = { 1.0f, 1.0f, 1.0f };
        return one;
    }
}

void Fluxion_GameObject_Rotate(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionVec3 eulerRadians)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionQuat pitch, yaw, roll, turn;
    if (entry == NULL) return;

    {
        f32 half = eulerRadians.x * 0.5f;
        pitch.x = sinf(half); pitch.y = 0.0f; pitch.z = 0.0f; pitch.w = cosf(half);
    }
    {
        f32 half = eulerRadians.y * 0.5f;
        yaw.x = 0.0f; yaw.y = sinf(half); yaw.z = 0.0f; yaw.w = cosf(half);
    }
    {
        f32 half = eulerRadians.z * 0.5f;
        roll.x = 0.0f; roll.y = 0.0f; roll.z = sinf(half); roll.w = cosf(half);
    }

    turn = Fluxion_Quat_Multiply(yaw, Fluxion_Quat_Multiply(pitch, roll));
    entry->localRotation = Fluxion_Quat_Multiply(entry->localRotation, turn);
    Fluxion_SceneInternal_MarkWorldDirty(record, object);
}

// Rotation and scale in the upper three-by-three, translation in the last
// column: the same row-major arrangement Fluxion_Mat4_Translation writes,
// so the two compose with Fluxion_Mat4_Multiply as they stand.
static FluxionMat4 Fluxion_SceneInternal_LocalMatrix(const FluxionSceneGameObjectRecord* entry)
{
    FluxionMat4 result = Fluxion_Mat4_Identity();
    FluxionQuat q = entry->localRotation;

    f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    result.m[0][0] = (1.0f - 2.0f * (yy + zz)) * entry->localScale.x;
    result.m[1][0] = (2.0f * (xy + wz)) * entry->localScale.x;
    result.m[2][0] = (2.0f * (xz - wy)) * entry->localScale.x;

    result.m[0][1] = (2.0f * (xy - wz)) * entry->localScale.y;
    result.m[1][1] = (1.0f - 2.0f * (xx + zz)) * entry->localScale.y;
    result.m[2][1] = (2.0f * (yz + wx)) * entry->localScale.y;

    result.m[0][2] = (2.0f * (xz + wy)) * entry->localScale.z;
    result.m[1][2] = (2.0f * (yz - wx)) * entry->localScale.z;
    result.m[2][2] = (1.0f - 2.0f * (xx + yy)) * entry->localScale.z;

    result.m[0][3] = entry->localPosition.x;
    result.m[1][3] = entry->localPosition.y;
    result.m[2][3] = entry->localPosition.z;
    return result;
}

FluxionMat4 Fluxion_GameObject_GetLocalMatrix(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL) return Fluxion_Mat4_Identity();
    return Fluxion_SceneInternal_LocalMatrix(entry);
}

static FluxionMat4 Fluxion_SceneInternal_WorldMatrix(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    if (entry == NULL) return Fluxion_Mat4_Identity();
    if (!entry->worldDirty) return entry->worldMatrix;

    if (FLUXION_HANDLE_IS_VALID(entry->parent))
    {
        FluxionMat4 parentWorld = Fluxion_SceneInternal_WorldMatrix(record, entry->parent);
        entry->worldMatrix = Fluxion_Mat4_Multiply(parentWorld, Fluxion_SceneInternal_LocalMatrix(entry));
    }
    else
    {
        entry->worldMatrix = Fluxion_SceneInternal_LocalMatrix(entry);
    }

    entry->worldDirty = false;
    return entry->worldMatrix;
}

FluxionMat4 Fluxion_GameObject_GetWorldMatrix(FluxionSceneHandle scene, FluxionGameObjectHandle object)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == NULL) return Fluxion_Mat4_Identity();
    return Fluxion_SceneInternal_WorldMatrix(record, object);
}
