// Native data components: the plain-data kind, stored by value, told
// apart from the script components that already live in this module.
//
// A script component is an object in the scripting runtime with a
// lifecycle; this is the opposite -- no behaviour, no lifecycle, just a
// struct that belongs to an object and that a future query walks in bulk.
// Both hang off the same game object, and neither knows about the other.
//
// Per component type:
//
//   dense[]   the component values, packed with no holes
//   owners[]  which object each dense row belongs to, same order
//   rowOf[]   object index -> dense row, or NONE
//
// Packed-with-owners is the shape a bulk read wants: working through every
// component of a type is a straight run over `dense` with no gaps to test
// for and nothing else on the cache line, and the object each belongs to
// is one aligned read away in the array beside it. The cost is paid on the
// other side, in rowOf, which is one number per object whether that object
// carries this type or not.
//
// Removal swaps the last row into the freed slot rather than shifting the
// rest down. That keeps removal constant-time and the array packed, at
// the cost of the order not being meaningful -- which it never was: a
// caller that wants a particular order asks the hierarchy, not the pool.

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

// Storage is taken the first time a type is actually used, not when a
// scene is made: a scene that never adds a LightComponent pays nothing
// for lights. Every pool is sized for the whole object table, so a row
// never has to move because the pool grew.
static bool Fluxion_SceneComponentPool_Reserve(FluxionSceneComponentPool* pool, FluxionTypeId type, usize elementSize)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    u32 i;

    pool->dense = (u8*)Fluxion_Allocator_Alloc(allocator, elementSize * FLUXION_SCENE_MAX_GAME_OBJECTS, FLUXION_DEFAULT_ALIGNMENT);
    if (pool->dense == NULL) return false;

    pool->owners = (FluxionGameObjectHandle*)Fluxion_Allocator_Alloc(allocator,
        sizeof(FluxionGameObjectHandle) * FLUXION_SCENE_MAX_GAME_OBJECTS, FLUXION_DEFAULT_ALIGNMENT);
    if (pool->owners == NULL)
    {
        Fluxion_Allocator_Free(allocator, pool->dense, elementSize * FLUXION_SCENE_MAX_GAME_OBJECTS);
        pool->dense = NULL;
        return false;
    }

    pool->rowOf = (u32*)Fluxion_Allocator_Alloc(allocator, sizeof(u32) * FLUXION_SCENE_MAX_GAME_OBJECTS, FLUXION_DEFAULT_ALIGNMENT);
    if (pool->rowOf == NULL)
    {
        Fluxion_Allocator_Free(allocator, pool->owners, sizeof(FluxionGameObjectHandle) * FLUXION_SCENE_MAX_GAME_OBJECTS);
        Fluxion_Allocator_Free(allocator, pool->dense, elementSize * FLUXION_SCENE_MAX_GAME_OBJECTS);
        pool->dense = NULL;
        pool->owners = NULL;
        return false;
    }

    for (i = 0; i < FLUXION_SCENE_MAX_GAME_OBJECTS; ++i) pool->rowOf[i] = FLUXION_SCENE_NO_COMPONENT;

    pool->type = type;
    pool->elementSize = elementSize;
    pool->count = 0;
    return true;
}

// The pool for `type`, made if it does not exist yet and `create` says
// so. NULL means either "not there and not asked for" or "no room" --
// both of which a caller answers the same way, by failing the operation.
static FluxionSceneComponentPool* Fluxion_SceneComponentPool_Find(FluxionSceneRecord* record, FluxionTypeId type, bool create)
{
    u32 i;
    const FluxionTypeInfo* typeInfo;
    FluxionSceneComponentPool* freeSlot = NULL;

    if (record == NULL || type == FLUXION_TYPE_ID_INVALID) return NULL;

    for (i = 0; i < FLUXION_SCENE_MAX_COMPONENT_TYPES; ++i)
    {
        if (record->componentPools[i].dense != NULL && record->componentPools[i].type == type) return &record->componentPools[i];
        if (record->componentPools[i].dense == NULL && freeSlot == NULL) freeSlot = &record->componentPools[i];
    }

    if (!create || freeSlot == NULL) return NULL;

    // The size comes from the one metadata system this engine has, not
    // from a second table kept beside it -- a component type is just a
    // registered reflected type, and an unregistered one is a caller
    // mistake rather than a size this code should guess.
    //
    // Asked first rather than found out by tripping the assert inside:
    // "the registry was never brought up" is a different mistake from "the
    // type was never registered", and saying so here is the only place it
    // can still be told apart.
    FLUXION_ASSERT_MSG(Fluxion_Reflection_IsInitialized(),
        "Fluxion: a data component was asked for before the reflection registry was brought up");
    if (!Fluxion_Reflection_IsInitialized()) return NULL;

    typeInfo = Fluxion_Reflection_FindTypeById(type);
    if (typeInfo == NULL || typeInfo->size == 0) return NULL;

    if (!Fluxion_SceneComponentPool_Reserve(freeSlot, type, typeInfo->size)) return NULL;
    return freeSlot;
}

void* Fluxion_GameObject_AddComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type, const void* initialValue)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionSceneComponentPool* pool;
    u32 row;

    if (entry == NULL) return NULL;

    pool = Fluxion_SceneComponentPool_Find(record, type, true);
    if (pool == NULL) return NULL;

    // Already there: the existing one is handed back rather than a second
    // being made. One object holds at most one component of a type --
    // that is what lets rowOf be a single number per object.
    row = pool->rowOf[object.index];
    if (row != FLUXION_SCENE_NO_COMPONENT) return pool->dense + (usize)row * pool->elementSize;

    if (pool->count >= FLUXION_SCENE_MAX_GAME_OBJECTS) return NULL;

    row = pool->count++;
    pool->owners[row] = object;
    pool->rowOf[object.index] = row;

    // Zeroed unless the caller supplied a value: a component read before
    // anything wrote it should be a defined zero, not whatever the last
    // owner of that row left behind.
    if (initialValue != NULL) memcpy(pool->dense + (usize)row * pool->elementSize, initialValue, pool->elementSize);
    else memset(pool->dense + (usize)row * pool->elementSize, 0, pool->elementSize);

    return pool->dense + (usize)row * pool->elementSize;
}

void* Fluxion_GameObject_GetComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionSceneComponentPool* pool;
    u32 row;

    if (entry == NULL) return NULL;

    pool = Fluxion_SceneComponentPool_Find(record, type, false);
    if (pool == NULL) return NULL;

    row = pool->rowOf[object.index];
    if (row == FLUXION_SCENE_NO_COMPONENT) return NULL;
    return pool->dense + (usize)row * pool->elementSize;
}

bool Fluxion_GameObject_HasComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type)
{
    return Fluxion_GameObject_GetComponent(scene, object, type) != NULL;
}

bool Fluxion_GameObject_RemoveComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionSceneComponentPool* pool;

    if (entry == NULL) return false;

    pool = Fluxion_SceneComponentPool_Find(record, type, false);
    if (pool == NULL) return false;

    return Fluxion_SceneComponentPool_RemoveByIndex(pool, object.index);
}

bool Fluxion_SceneComponentPool_RemoveByIndex(FluxionSceneComponentPool* pool, u32 objectIndex)
{
    u32 row, lastRow;

    if (pool == NULL || pool->dense == NULL || objectIndex >= FLUXION_SCENE_MAX_GAME_OBJECTS) return false;

    row = pool->rowOf[objectIndex];
    if (row == FLUXION_SCENE_NO_COMPONENT) return false;

    lastRow = pool->count - 1;
    if (row != lastRow)
    {
        // The last row moves into the freed one, and its owner's entry
        // has to follow it. Forgetting this second half is the classic
        // way a packed array quietly starts handing out the wrong
        // component: nothing crashes, the data is simply somebody
        // else's.
        memcpy(pool->dense + (usize)row * pool->elementSize,
               pool->dense + (usize)lastRow * pool->elementSize,
               pool->elementSize);
        pool->owners[row] = pool->owners[lastRow];
        pool->rowOf[pool->owners[row].index] = row;
    }

    pool->rowOf[objectIndex] = FLUXION_SCENE_NO_COMPONENT;
    --pool->count;
    return true;
}

void Fluxion_SceneComponentPool_RemoveAllOf(FluxionSceneRecord* record, u32 objectIndex)
{
    u32 i;
    if (record == NULL) return;
    for (i = 0; i < FLUXION_SCENE_MAX_COMPONENT_TYPES; ++i)
    {
        if (record->componentPools[i].dense == NULL) continue;
        Fluxion_SceneComponentPool_RemoveByIndex(&record->componentPools[i], objectIndex);
    }
}

void Fluxion_SceneComponentPool_ReleaseScene(FluxionSceneRecord* record)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    u32 i;

    if (record == NULL) return;
    for (i = 0; i < FLUXION_SCENE_MAX_COMPONENT_TYPES; ++i)
    {
        FluxionSceneComponentPool* pool = &record->componentPools[i];
        if (pool->dense == NULL) continue;

        Fluxion_Allocator_Free(allocator, pool->rowOf, sizeof(u32) * FLUXION_SCENE_MAX_GAME_OBJECTS);
        Fluxion_Allocator_Free(allocator, pool->owners, sizeof(FluxionGameObjectHandle) * FLUXION_SCENE_MAX_GAME_OBJECTS);
        Fluxion_Allocator_Free(allocator, pool->dense, pool->elementSize * FLUXION_SCENE_MAX_GAME_OBJECTS);

        memset(pool, 0, sizeof(*pool));
    }
}

void* Fluxion_Scene_GetComponentArray(FluxionSceneHandle scene, FluxionTypeId type, u32* outCount)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneComponentPool* pool = Fluxion_SceneComponentPool_Find(record, type, false);

    if (outCount != NULL) *outCount = (pool != NULL) ? pool->count : 0u;
    return (pool != NULL) ? (void*)pool->dense : NULL;
}

const FluxionGameObjectHandle* Fluxion_Scene_GetComponentOwners(FluxionSceneHandle scene, FluxionTypeId type, u32* outCount)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneComponentPool* pool = Fluxion_SceneComponentPool_Find(record, type, false);

    if (outCount != NULL) *outCount = (pool != NULL) ? pool->count : 0u;
    return (pool != NULL) ? pool->owners : NULL;
}

u32 Fluxion_Scene_ComponentCount(FluxionSceneHandle scene, FluxionTypeId type)
{
    u32 count = 0;
    (void)Fluxion_Scene_GetComponentArray(scene, type, &count);
    return count;
}
