// Prefabs: an object and everything below it, kept so it can be put into
// a scene more than once.
//
// The whole of it rests on one choice, and it is worth saying why. What a
// copy differs from its prefab in is not RECORDED as it happens -- it is
// worked out by comparing the two whenever anyone asks. Recording is the
// obvious alternative and it cannot be done: a component is written
// through a plain pointer into the storage, so there is no moment at
// which anything could notice.
//
// Comparing needs the prefab's objects to exist, which they do not: the
// prefab is bytes. So the three operations that compare open the prefab
// into a scene of its own for as long as they take, and close it again.
// That costs a scene slot for the length of one call, and it means the
// comparison uses exactly the same code everything else uses to read
// components -- rather than a second reader of the same bytes, which
// could disagree with the first.

#include <Fluxion/Scene/Prefab.h>

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Scene/SceneSerialization.h>

#include <string.h>

static const char* const kLogChannel = "Scene.Prefab";

struct FluxionPrefab
{
    FluxionUUID id;
    u8* bytes;
    usize size;
    usize capacity;
};

// --- Making one and taking it apart --------------------------------------

FluxionPrefab* Fluxion_Prefab_CreateFromObject(FluxionSceneHandle scene, FluxionGameObjectHandle root)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    FluxionPrefab* prefab;
    usize capacity = 4096;

    if (!Fluxion_GameObject_IsValid(scene, root)) return NULL;

    prefab = (FluxionPrefab*)Fluxion_Allocator_Alloc(allocator, sizeof(FluxionPrefab), FLUXION_DEFAULT_ALIGNMENT);
    if (prefab == NULL) return NULL;
    memset(prefab, 0, sizeof(*prefab));
    prefab->id = Fluxion_UUID_Generate();

    // Written into a buffer that grows until it fits, the same way a
    // whole scene is: how much a subtree comes to is not something a
    // caller can work out beforehand.
    for (;;)
    {
        u8* buffer = (u8*)Fluxion_Allocator_Alloc(allocator, capacity, FLUXION_DEFAULT_ALIGNMENT);
        FluxionStream stream;

        if (buffer == NULL) break;

        Fluxion_MemoryStream_InitWriter(&stream, buffer, capacity);
        if (Fluxion_SceneInternal_SaveSubtree(scene, root, &stream))
        {
            prefab->bytes = buffer;
            prefab->size = stream.position;
            prefab->capacity = capacity;
            return prefab;
        }

        Fluxion_Allocator_Free(allocator, buffer, capacity);
        if (!Fluxion_Stream_HasOverflowed(&stream)) break;
        capacity *= 2;
    }

    Fluxion_Allocator_Free(allocator, prefab, sizeof(FluxionPrefab));
    return NULL;
}

void Fluxion_Prefab_Destroy(FluxionPrefab* prefab)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    if (prefab == NULL) return;

    if (prefab->bytes != NULL) Fluxion_Allocator_Free(allocator, prefab->bytes, prefab->capacity);
    Fluxion_Allocator_Free(allocator, prefab, sizeof(FluxionPrefab));
}

FluxionUUID Fluxion_Prefab_GetId(const FluxionPrefab* prefab)
{
    FluxionUUID nil;
    if (prefab != NULL) return prefab->id;
    memset(&nil, 0, sizeof(nil));
    return nil;
}

FluxionEntityHandle Fluxion_Prefab_Instantiate(const FluxionPrefab* prefab, FluxionSceneHandle scene)
{
    FluxionStream reader;
    FluxionGameObjectHandle root = Fluxion_GameObject_InvalidHandle();

    if (prefab == NULL || prefab->bytes == NULL) return root;

    Fluxion_MemoryStream_InitReader(&reader, prefab->bytes, prefab->size);
    if (!Fluxion_SceneInternal_InstantiateInto(scene, &reader, prefab->id, &root))
    {
        return Fluxion_GameObject_InvalidHandle();
    }
    return root;
}

// --- Opening one for as long as a comparison takes ------------------------

// The prefab's contents, in a scene of their own. Closed again by the
// caller; a scene slot is held for exactly the length of one operation.
static bool Fluxion_Prefab_Open(const FluxionPrefab* prefab, FluxionSceneHandle* outScene)
{
    FluxionStream reader;
    FluxionSceneHandle opened;

    if (prefab == NULL || prefab->bytes == NULL) return false;

    opened = Fluxion_Scene_Create();
    if (!Fluxion_Scene_IsValid(opened))
    {
        FLUXION_LOG_ERROR(kLogChannel, "there was no room for another scene to open this prefab into");
        return false;
    }

    Fluxion_MemoryStream_InitReader(&reader, prefab->bytes, prefab->size);
    if (!Fluxion_Scene_Load(opened, &reader))
    {
        Fluxion_Scene_Destroy(opened);
        return false;
    }

    *outScene = opened;
    return true;
}

// Which object of the prefab this one was copied from, or an invalid
// handle when it was copied from a different prefab or from none.
static FluxionGameObjectHandle Fluxion_Prefab_SourceOf(const FluxionPrefab* prefab, FluxionSceneHandle scene,
                                                       FluxionGameObjectHandle object, FluxionSceneHandle opened)
{
    const FluxionPrefabLink* link =
        (const FluxionPrefabLink*)Fluxion_GameObject_GetComponent(scene, object, Fluxion_PrefabLink_TypeId());

    if (link == NULL) return Fluxion_GameObject_InvalidHandle();
    if (!Fluxion_UUID_Equals(link->prefab, prefab->id)) return Fluxion_GameObject_InvalidHandle();

    return Fluxion_Scene_FindByUUID(opened, link->sourceEntity);
}

// --- Comparing and copying ------------------------------------------------
//
// Both directions leave alone what cannot be carried between two scenes.
//
// A field naming another object is the case: a handle means something
// only in the scene that handed it out, and the same object in the other
// scene has a different one. Copying its bytes would point the copy at
// whatever happens to sit at that index over there -- which is exactly
// the silent wrongness the whole save format was arranged to avoid.
static bool Fluxion_Prefab_CarriedProperty(const FluxionPropertyInfo* property)
{
    if ((property->flags & FLUXION_PROPERTY_FLAG_TRANSIENT) != 0) return false;
    if (property->type == Fluxion_EntityHandle_TypeId()) return false;
    if (property->accessKind != FLUXION_PROPERTY_ACCESS_OFFSET) return false;
    return true;
}

// Whether these two components of the same type hold the same thing, as
// far as the parts that travel are concerned.
static bool Fluxion_Prefab_ValuesEqual(const FluxionTypeInfo* typeInfo, const void* left, const void* right)
{
    usize i;
    for (i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        if (!Fluxion_Prefab_CarriedProperty(property)) continue;

        if (memcmp((const u8*)left + property->offset, (const u8*)right + property->offset, property->size) != 0)
        {
            return false;
        }
    }
    return true;
}

static void Fluxion_Prefab_CopyValues(const FluxionTypeInfo* typeInfo, void* destination, const void* source)
{
    usize i;
    for (i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        if (!Fluxion_Prefab_CarriedProperty(property)) continue;

        memcpy((u8*)destination + property->offset, (const u8*)source + property->offset, property->size);
    }
}

// The types worth carrying: everything the object holds except the two
// the scene puts on every object for its own reasons, and except the link
// itself -- which says where a copy came from and would be nonsense in
// the prefab.
static bool Fluxion_Prefab_CarriedType(FluxionTypeId type)
{
    if (type == Fluxion_ScriptComponent_TypeId()) return false;
    if (type == Fluxion_PrefabLink_TypeId()) return false;
    return true;
}

bool Fluxion_Prefab_IsOverridden(const FluxionPrefab* prefab, FluxionSceneHandle scene,
                                 FluxionEntityHandle object, FluxionTypeId type)
{
    FluxionSceneHandle opened;
    FluxionGameObjectHandle source;
    const FluxionTypeInfo* typeInfo;
    bool differs = false;

    if (!Fluxion_Prefab_CarriedType(type)) return false;
    if (!Fluxion_Prefab_Open(prefab, &opened)) return false;

    source = Fluxion_Prefab_SourceOf(prefab, scene, object, opened);
    typeInfo = Fluxion_Reflection_FindTypeById(type);

    if (FLUXION_HANDLE_IS_VALID(source) && typeInfo != NULL)
    {
        const void* theirs = Fluxion_GameObject_GetComponent(opened, source, type);
        const void* ours = Fluxion_GameObject_GetComponent(scene, object, type);

        // A component the prefab's object does not carry is not a
        // changed value but an added one, which counts as differing.
        if (ours != NULL && theirs == NULL) differs = true;
        else if (ours != NULL && theirs != NULL) differs = !Fluxion_Prefab_ValuesEqual(typeInfo, ours, theirs);
    }

    Fluxion_Scene_Destroy(opened);
    return differs;
}

bool Fluxion_Prefab_Revert(const FluxionPrefab* prefab, FluxionSceneHandle scene, FluxionEntityHandle object)
{
    FluxionSceneHandle opened;
    FluxionGameObjectHandle source;
    FluxionTypeId types[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    u32 typeCount;
    u32 i;
    bool ok = false;

    if (!Fluxion_Prefab_Open(prefab, &opened)) return false;

    source = Fluxion_Prefab_SourceOf(prefab, scene, object, opened);
    if (FLUXION_HANDLE_IS_VALID(source))
    {
        typeCount = Fluxion_GameObject_GetComponentTypes(opened, source, types, FLUXION_SCENE_MAX_COMPONENT_TYPES);
        for (i = 0; i < typeCount; ++i)
        {
            const FluxionTypeInfo* typeInfo;
            const void* theirs;
            void* ours;

            if (!Fluxion_Prefab_CarriedType(types[i])) continue;

            typeInfo = Fluxion_Reflection_FindTypeById(types[i]);
            theirs = Fluxion_GameObject_GetComponent(opened, source, types[i]);
            if (typeInfo == NULL || theirs == NULL) continue;

            // Attached if it is missing, which is what makes reverting
            // undo a removal as well as a change. Taken again afterwards
            // because attaching moves the object's storage.
            ours = Fluxion_GameObject_AddComponent(scene, object, types[i], NULL);
            if (ours == NULL) continue;

            Fluxion_Prefab_CopyValues(typeInfo, ours, theirs);
        }
        ok = true;
    }

    Fluxion_Scene_Destroy(opened);
    return ok;
}

bool Fluxion_Prefab_Apply(FluxionPrefab* prefab, FluxionSceneHandle scene, FluxionEntityHandle object)
{
    FluxionSceneHandle opened;
    FluxionGameObjectHandle source;
    FluxionTypeId types[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    u32 typeCount;
    u32 i;
    bool ok = false;

    if (prefab == NULL) return false;
    if (!Fluxion_Prefab_Open(prefab, &opened)) return false;

    source = Fluxion_Prefab_SourceOf(prefab, scene, object, opened);
    if (FLUXION_HANDLE_IS_VALID(source))
    {
        typeCount = Fluxion_GameObject_GetComponentTypes(scene, object, types, FLUXION_SCENE_MAX_COMPONENT_TYPES);
        for (i = 0; i < typeCount; ++i)
        {
            const FluxionTypeInfo* typeInfo;
            const void* ours;
            void* theirs;

            if (!Fluxion_Prefab_CarriedType(types[i])) continue;

            typeInfo = Fluxion_Reflection_FindTypeById(types[i]);
            ours = Fluxion_GameObject_GetComponent(scene, object, types[i]);
            if (typeInfo == NULL || ours == NULL) continue;

            theirs = Fluxion_GameObject_AddComponent(opened, source, types[i], NULL);
            if (theirs == NULL) continue;

            Fluxion_Prefab_CopyValues(typeInfo, theirs, ours);
        }

        // Written back out, because the prefab IS its bytes: a change
        // that only reached the opened scene would be gone the moment it
        // closed.
        {
            usize size = 0;
            u8* rewritten = Fluxion_Scene_SaveToBuffer(opened, prefab->capacity, &size);
            if (rewritten != NULL)
            {
                Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), prefab->bytes, prefab->capacity);
                prefab->bytes = rewritten;
                prefab->size = size;
                prefab->capacity = size;
                ok = true;
            }
        }
    }

    Fluxion_Scene_Destroy(opened);
    return ok;
}
