// Writing a scene down and reading it back.
//
// Nothing here decides what a component holds -- the reflection registry
// already says that, for native components and for script classes alike,
// and the property descriptions carry the reading and writing of a value.
// What this file decides is only the three things a registry cannot:
//
//   which objects there are        written as ids, never as handles
//   what is under what             also as ids
//   what points at what            also as ids, resolved in a second pass
//
// The second pass is not an optimisation. Two objects can point at each
// other, so there is no order in which every reference already exists
// when it is read; every object is therefore made first and pointed
// afterwards.

#include <Fluxion/Scene/SceneSerialization.h>

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Core/Serialization/BinarySerializer.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

static const char* const kLogChannel = "Scene.Serialization";

// What a file starts with, so that something which is not one of these is
// refused before anything is read out of it rather than after.
#define FLUXION_SCENE_FILE_MAGIC 0x464C5853u /* "FLXS" as bytes in order */

// --- Small pieces --------------------------------------------------------

static void Fluxion_SceneSerialization_UUID(FluxionStream* stream, FluxionUUID* uuid)
{
    Fluxion_Stream_SerializeBytes(stream, uuid->bytes, sizeof(uuid->bytes));
}

// A name, as a length and that many bytes. Read back into a fixed buffer,
// so a length longer than a name may be is a damaged file rather than
// something to make room for.
static bool Fluxion_SceneSerialization_Name(FluxionStream* stream, char* name)
{
    u32 length = (u32)strlen(name);

    if (Fluxion_Stream_IsWriting(stream))
    {
        Fluxion_Stream_SerializeU32(stream, &length);
        if (length != 0) Fluxion_Stream_SerializeBytes(stream, name, length);
        return true;
    }

    length = 0;
    Fluxion_Stream_SerializeU32(stream, &length);
    if (length >= FLUXION_SCENE_MAX_NAME_LENGTH) return false;

    if (length != 0) Fluxion_Stream_SerializeBytes(stream, name, length);
    name[length] = '\0';
    return true;
}

// --- Writing -------------------------------------------------------------

// A field that names another object cannot be written as it stands: it
// holds a handle, and a handle is where something sits in a table right
// now. Those are left out of the ordinary pass and written afterwards,
// each as the id of what it names.
//
// Recognised by the field's TYPE, not by a flag anybody had to remember:
// a forgotten flag would write the raw index and generation, which after
// a reload name a different object or none, and would do it in silence.
static u32 Fluxion_SceneSerialization_ReferenceCount(const FluxionTypeInfo* typeInfo)
{
    const FluxionTypeId entityType = Fluxion_EntityHandle_TypeId();
    u32 count = 0;
    usize i;

    for (i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        if (property->type == entityType && (property->flags & FLUXION_PROPERTY_FLAG_TRANSIENT) == 0) ++count;
    }
    return count;
}

// Reads or writes one field naming another object. On the way out the
// handle becomes the id of what it names; on the way in the id becomes
// whichever object now carries it, or nothing when it names something
// that is not in the file.
static void Fluxion_SceneSerialization_Reference(FluxionStream* stream, FluxionSceneHandle scene,
                                                 const FluxionPropertyInfo* property, void* value)
{
    FluxionGameObjectHandle* field = (FluxionGameObjectHandle*)((u8*)value + property->offset);
    FluxionUUID id;

    if (Fluxion_Stream_IsWriting(stream))
    {
        id = Fluxion_GameObject_GetUUID(scene, *field);
        Fluxion_SceneSerialization_UUID(stream, &id);
        return;
    }

    memset(&id, 0, sizeof(id));
    Fluxion_SceneSerialization_UUID(stream, &id);
    *field = Fluxion_Scene_FindByUUID(scene, id);
}

// One component's values. The type says what it holds; this only has to
// name the type, hand the description over, and deal with the fields the
// description cannot carry.
static bool Fluxion_SceneSerialization_WriteComponent(FluxionStream* stream, FluxionSceneHandle scene,
                                                      FluxionGameObjectHandle object, FluxionTypeId type)
{
    const FluxionTypeId entityType = Fluxion_EntityHandle_TypeId();
    const FluxionTypeInfo* typeInfo = Fluxion_Reflection_FindTypeById(type);
    void* value = Fluxion_GameObject_GetComponent(scene, object, type);
    u64 id = type;
    u32 referenceCount;
    usize i;

    if (typeInfo == NULL || value == NULL) return false;

    Fluxion_Stream_SerializeU64(stream, &id);
    if (!Fluxion_BinarySerializer_SerializeExcept(stream, typeInfo, value, &entityType, 1)) return false;

    referenceCount = Fluxion_SceneSerialization_ReferenceCount(typeInfo);
    Fluxion_Stream_SerializeU32(stream, &referenceCount);

    for (i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        u32 nameHash;

        if (property->type != entityType) continue;
        if ((property->flags & FLUXION_PROPERTY_FLAG_TRANSIENT) != 0) continue;

        nameHash = Fluxion_HashBytes32(property->name.data, property->name.length);
        Fluxion_Stream_SerializeU32(stream, &nameHash);
        Fluxion_SceneSerialization_Reference(stream, scene, property, value);
    }
    return true;
}

// Whether `object` is `root` or sits anywhere below it. Walked upwards
// rather than downwards, because the question is asked once per object
// and every object knows its parent.
static bool Fluxion_SceneSerialization_InSubtree(FluxionSceneRecord* record, FluxionGameObjectHandle object,
                                                 FluxionGameObjectHandle root)
{
    FluxionGameObjectHandle cursor = object;
    u32 guard = 0;

    if (!FLUXION_HANDLE_IS_VALID(root)) return true; // no root named: the whole scene

    while (FLUXION_HANDLE_IS_VALID(cursor) && guard++ < FLUXION_SCENE_MAX_GAME_OBJECTS)
    {
        FluxionSceneGameObjectRecord* entry;
        if (cursor.index == root.index && cursor.generation == root.generation) return true;

        entry = Fluxion_SceneInternal_ResolveObject(record, cursor);
        if (entry == NULL) break;
        cursor = entry->parent;
    }
    return false;
}

bool Fluxion_SceneInternal_SaveSubtree(FluxionSceneHandle scene, FluxionGameObjectHandle root, FluxionStream* stream)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    u32 magic = FLUXION_SCENE_FILE_MAGIC;
    u32 version = FLUXION_SCENE_FORMAT_VERSION;
    u32 objectCount = 0;
    u32 i;

    if (record == NULL || stream == NULL || !Fluxion_Stream_IsWriting(stream)) return false;

    Fluxion_Stream_SerializeU32(stream, &magic);
    Fluxion_Stream_SerializeU32(stream, &version);

    // Counted before anything is written, because the count goes first and
    // a subtree is not the whole table.
    for (i = 0; i < FLUXION_SCENE_MAX_GAME_OBJECTS; ++i)
    {
        FluxionGameObjectHandle object;
        if (!record->objects[i].alive) continue;
        object.index = i;
        object.generation = record->objects[i].generation;
        if (Fluxion_SceneSerialization_InSubtree(record, object, root)) ++objectCount;
    }
    Fluxion_Stream_SerializeU32(stream, &objectCount);

    for (i = 0; i < FLUXION_SCENE_MAX_GAME_OBJECTS; ++i)
    {
        FluxionSceneGameObjectRecord* entry = &record->objects[i];
        FluxionGameObjectHandle object;
        FluxionUUID parentId;
        FluxionTypeId types[FLUXION_SCENE_MAX_COMPONENT_TYPES];
        u32 typeCount;
        u32 saved = 0;
        u32 t;

        if (!entry->alive) continue;

        object.index = i;
        object.generation = entry->generation;
        if (!Fluxion_SceneSerialization_InSubtree(record, object, root)) continue;

        Fluxion_SceneSerialization_UUID(stream, &entry->uuid);
        (void)Fluxion_SceneSerialization_Name(stream, entry->name);

        // The parent as an id. A root is written as the nil id, which no
        // object carries, so "no parent" needs no separate flag -- and the
        // object a subtree was taken from becomes a root of the file even
        // though it had a parent where it came from.
        parentId = Fluxion_SceneSerialization_InSubtree(record, entry->parent, root)
            ? Fluxion_GameObject_GetUUID(scene, entry->parent)
            : Fluxion_GameObject_GetUUID(scene, Fluxion_GameObject_InvalidHandle());
        Fluxion_SceneSerialization_UUID(stream, &parentId);

        typeCount = Fluxion_GameObject_GetComponentTypes(scene, object, types, FLUXION_SCENE_MAX_COMPONENT_TYPES);

        // Counted before any is written, because the count goes first and
        // a type the registry no longer knows is not written at all.
        for (t = 0; t < typeCount; ++t)
        {
            if (Fluxion_Reflection_FindTypeById(types[t]) != NULL) ++saved;
        }
        Fluxion_Stream_SerializeU32(stream, &saved);

        for (t = 0; t < typeCount; ++t)
        {
            if (Fluxion_Reflection_FindTypeById(types[t]) == NULL) continue;
            if (!Fluxion_SceneSerialization_WriteComponent(stream, scene, object, types[t])) return false;
        }
    }

    return !Fluxion_Stream_HasOverflowed(stream);
}

bool Fluxion_Scene_Save(FluxionSceneHandle scene, FluxionStream* stream)
{
    return Fluxion_SceneInternal_SaveSubtree(scene, Fluxion_GameObject_InvalidHandle(), stream);
}

// --- Reading -------------------------------------------------------------

// Where the second pass finds what the first made. One entry per object
// in the file, in the order they were read.
typedef struct FluxionSceneLoadEntry
{
    // What the file called it, and what it is called here.
    //
    // The two are the same when a scene is loaded: it IS the scene that
    // was written. They differ when a prefab is copied into a scene, and
    // they have to: a copy is a new object, and two objects answering to
    // one id would make every lookup a coin toss.
    FluxionUUID uuid;
    FluxionUUID assigned;
    FluxionUUID parent;
} FluxionSceneLoadEntry;

// A field naming another object, read but not yet pointed anywhere.
//
// Held rather than resolved on the spot because what it names may not
// have been read yet -- and there is no order that avoids it, since two
// objects can name each other.
typedef struct FluxionScenePendingReference
{
    FluxionUUID owner;
    FluxionTypeId componentType;
    u32 propertyNameHash;
    FluxionUUID target;
} FluxionScenePendingReference;

// How many such fields one file may hold. Generous rather than tight:
// running out is refused outright, and a scene refused for a reason the
// author cannot see coming is worse than one that costs a little memory.
#define FLUXION_SCENE_MAX_PENDING_REFERENCES (FLUXION_SCENE_MAX_GAME_OBJECTS * 4)

// Puts one read-back reference where it belongs, now that everything it
// might name exists.
static void Fluxion_SceneSerialization_ApplyReference(FluxionSceneHandle scene, const FluxionScenePendingReference* pending)
{
    const FluxionTypeInfo* typeInfo = Fluxion_Reflection_FindTypeById(pending->componentType);
    FluxionGameObjectHandle owner = Fluxion_Scene_FindByUUID(scene, pending->owner);
    void* value;
    usize i;

    if (typeInfo == NULL || !FLUXION_HANDLE_IS_VALID(owner)) return;

    value = Fluxion_GameObject_GetComponent(scene, owner, pending->componentType);
    if (value == NULL) return;

    for (i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        if (Fluxion_HashBytes32(property->name.data, property->name.length) != pending->propertyNameHash) continue;
        if (property->type != Fluxion_EntityHandle_TypeId()) continue;

        // Nil names nothing, and nothing is what the field is left at --
        // which is also what happens when the file names an object that
        // is not in it.
        *(FluxionGameObjectHandle*)((u8*)value + property->offset) =
            Fluxion_Scene_FindByUUID(scene, pending->target);
        return;
    }
}

// What an id read out of the file is called here. The same id back when
// nothing was renamed, which is what makes the ordinary load fall out of
// the same code as the copy.
static FluxionUUID Fluxion_SceneSerialization_Remap(const FluxionSceneLoadEntry* entries, u32 count, FluxionUUID id)
{
    u32 i;
    for (i = 0; i < count; ++i)
    {
        if (Fluxion_UUID_Equals(entries[i].uuid, id)) return entries[i].assigned;
    }
    return id;
}

static void Fluxion_SceneSerialization_Clear(FluxionSceneHandle scene, FluxionSceneRecord* record)
{
    while (FLUXION_HANDLE_IS_VALID(record->firstRoot))
    {
        const FluxionGameObjectHandle root = record->firstRoot;
        Fluxion_GameObject_Destroy(scene, root);
        if (record->firstRoot.index == root.index && record->firstRoot.generation == root.generation) break;
    }
}

// Everything the two ways of reading differ in.
//
// Loading a scene replaces what was there and keeps every id. Copying a
// prefab adds to what is there, gives everything new ids, and records on
// each object which of the prefab's objects it came from.
typedef struct FluxionSceneLoadOptions
{
    bool replaceContents;
    bool assignNewIds;

    // Written onto every object made, when copying a prefab. Nil
    // otherwise, and then no link is attached at all.
    FluxionUUID prefab;
} FluxionSceneLoadOptions;

static bool Fluxion_SceneSerialization_LoadInternal(FluxionSceneHandle scene, FluxionStream* stream,
                                                    const FluxionSceneLoadOptions* options,
                                                    FluxionGameObjectHandle* outRoot)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    static FluxionSceneLoadEntry entries[FLUXION_SCENE_MAX_GAME_OBJECTS];
    static FluxionScenePendingReference pending[FLUXION_SCENE_MAX_PENDING_REFERENCES];
    u32 pendingCount = 0;
    u32 magic = 0;
    u32 version = 0;
    u32 objectCount = 0;
    u32 i;

    if (record == NULL || stream == NULL || !Fluxion_Stream_IsReading(stream)) return false;

    Fluxion_Stream_SerializeU32(stream, &magic);
    Fluxion_Stream_SerializeU32(stream, &version);
    if (Fluxion_Stream_HasOverflowed(stream) || magic != FLUXION_SCENE_FILE_MAGIC)
    {
        FLUXION_LOG_ERROR(kLogChannel, "this is not a scene written by this engine");
        return false;
    }
    if (version > FLUXION_SCENE_FORMAT_VERSION)
    {
        // Refused rather than read as far as it goes: it was written by
        // something that knew things this build does not, and a partial
        // read would be a scene quietly missing whatever those were.
        FLUXION_LOG_ERROR(kLogChannel,
            "this scene was written by a newer build (format %u, this one reads up to %u)",
            version, (u32)FLUXION_SCENE_FORMAT_VERSION);
        return false;
    }

    Fluxion_Stream_SerializeU32(stream, &objectCount);
    if (Fluxion_Stream_HasOverflowed(stream) || objectCount > FLUXION_SCENE_MAX_GAME_OBJECTS) return false;

    if (options->replaceContents) Fluxion_SceneSerialization_Clear(scene, record);

    // First pass: every object made, with the id it had. Nothing is
    // pointed anywhere yet, because what it would point at may not exist
    // until later in the file.
    for (i = 0; i < objectCount; ++i)
    {
        char name[FLUXION_SCENE_MAX_NAME_LENGTH];
        FluxionGameObjectHandle object;
        u32 componentCount = 0;
        u32 c;

        name[0] = '\0';
        Fluxion_SceneSerialization_UUID(stream, &entries[i].uuid);
        if (!Fluxion_SceneSerialization_Name(stream, name)) return false;
        Fluxion_SceneSerialization_UUID(stream, &entries[i].parent);

        // Kept or replaced, depending on which of the two readings this
        // is. A copy must not answer to the id of what it was copied from.
        entries[i].assigned = options->assignNewIds ? Fluxion_UUID_Generate() : entries[i].uuid;

        object = Fluxion_Scene_CreateGameObjectWithUUID(scene, name, entries[i].assigned);
        if (!FLUXION_HANDLE_IS_VALID(object)) return false;

        // The first object of the file with no parent inside it is the one
        // a caller copying a prefab gets back.
        if (outRoot != NULL && Fluxion_UUID_IsNil(entries[i].parent) && !FLUXION_HANDLE_IS_VALID(*outRoot))
        {
            *outRoot = object;
        }

        if (!Fluxion_UUID_IsNil(options->prefab))
        {
            FluxionPrefabLink link;
            link.prefab = options->prefab;
            link.sourceEntity = entries[i].uuid;
            if (Fluxion_GameObject_AddComponent(scene, object, Fluxion_PrefabLink_TypeId(), &link) == NULL) return false;

            // Attaching it moved the object's storage, so anything taken
            // from it before now names somebody else's row.
        }

        Fluxion_Stream_SerializeU32(stream, &componentCount);
        if (Fluxion_Stream_HasOverflowed(stream)) return false;

        for (c = 0; c < componentCount; ++c)
        {
            u64 typeId = 0;
            const FluxionTypeInfo* typeInfo;
            void* value;

            Fluxion_Stream_SerializeU64(stream, &typeId);
            if (Fluxion_Stream_HasOverflowed(stream)) return false;

            typeInfo = Fluxion_Reflection_FindTypeById((FluxionTypeId)typeId);
            if (typeInfo == NULL)
            {
                // A component of a type this build has never heard of --
                // a plugin that is not loaded, a script class that is
                // gone. There is no way to know how long its record is
                // without the description, so the file cannot be read
                // past it.
                FLUXION_LOG_ERROR(kLogChannel, "this scene holds a component of a type this build does not know");
                return false;
            }

            // Attached before its values are read, so that the values
            // have somewhere to go. A type already on the object -- the
            // transform, the script link -- is handed back rather than
            // added twice.
            value = Fluxion_GameObject_AddComponent(scene, object, (FluxionTypeId)typeId, NULL);
            if (value == NULL) return false;

            {
                const FluxionTypeId entityType = Fluxion_EntityHandle_TypeId();
                u32 referenceCount = 0;
                u32 r;

                if (!Fluxion_BinarySerializer_SerializeExcept(stream, typeInfo, value, &entityType, 1)) return false;

                Fluxion_Stream_SerializeU32(stream, &referenceCount);
                if (Fluxion_Stream_HasOverflowed(stream)) return false;

                for (r = 0; r < referenceCount; ++r)
                {
                    FluxionScenePendingReference item;

                    item.owner = entries[i].assigned;
                    item.componentType = (FluxionTypeId)typeId;
                    item.propertyNameHash = 0;
                    Fluxion_Stream_SerializeU32(stream, &item.propertyNameHash);
                    Fluxion_SceneSerialization_UUID(stream, &item.target);
                    if (Fluxion_Stream_HasOverflowed(stream)) return false;

                    if (pendingCount >= FLUXION_SCENE_MAX_PENDING_REFERENCES)
                    {
                        FLUXION_LOG_ERROR(kLogChannel, "this scene holds more references between objects than one may hold");
                        return false;
                    }
                    pending[pendingCount++] = item;
                }
            }

            // No value pointer is held across a loop turn: the NEXT
            // component of this object moves the object's storage, so one
            // taken now would name somebody else's row by then.
        }
    }

    // Second pass: now that every object exists, what points at what.
    //
    // Every id read out of the file is what the file called it, so each
    // has to be turned into what it is called here before it names
    // anything. When ids were kept the two are equal and this is a
    // no-op; when they were not, it is the whole of the remapping.
    for (i = 0; i < objectCount; ++i)
    {
        FluxionGameObjectHandle object;
        FluxionGameObjectHandle parent;

        if (Fluxion_UUID_IsNil(entries[i].parent)) continue;

        object = Fluxion_Scene_FindByUUID(scene, entries[i].assigned);
        parent = Fluxion_Scene_FindByUUID(scene,
            Fluxion_SceneSerialization_Remap(entries, objectCount, entries[i].parent));
        if (!FLUXION_HANDLE_IS_VALID(object) || !FLUXION_HANDLE_IS_VALID(parent)) continue;

        Fluxion_GameObject_SetParent(scene, object, parent);
    }

    for (i = 0; i < pendingCount; ++i)
    {
        pending[i].target = Fluxion_SceneSerialization_Remap(entries, objectCount, pending[i].target);
        Fluxion_SceneSerialization_ApplyReference(scene, &pending[i]);
    }

    return !Fluxion_Stream_HasOverflowed(stream);
}

bool Fluxion_Scene_Load(FluxionSceneHandle scene, FluxionStream* stream)
{
    FluxionSceneLoadOptions options;
    options.replaceContents = true;
    options.assignNewIds = false;
    memset(&options.prefab, 0, sizeof(options.prefab));
    return Fluxion_SceneSerialization_LoadInternal(scene, stream, &options, NULL);
}

bool Fluxion_SceneInternal_InstantiateInto(FluxionSceneHandle scene, FluxionStream* stream, FluxionUUID prefab,
                                            FluxionGameObjectHandle* outRoot)
{
    FluxionSceneLoadOptions options;
    options.replaceContents = false;
    options.assignNewIds = true;
    options.prefab = prefab;
    if (outRoot != NULL) *outRoot = Fluxion_GameObject_InvalidHandle();
    return Fluxion_SceneSerialization_LoadInternal(scene, stream, &options, outRoot);
}

// --- Into memory the caller owns -----------------------------------------

u8* Fluxion_Scene_SaveToBuffer(FluxionSceneHandle scene, usize capacity, usize* outSize)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();

    if (outSize != NULL) *outSize = 0;
    if (capacity == 0) capacity = 4096;

    // Tried, and tried again bigger if it did not fit. The stream reports
    // running out of room rather than writing past the end, which is
    // exactly what makes this safe to do by attempt -- and it saves a
    // caller from having to work out beforehand how large a scene is,
    // which it cannot.
    for (;;)
    {
        u8* buffer = (u8*)Fluxion_Allocator_Alloc(allocator, capacity, FLUXION_DEFAULT_ALIGNMENT);
        FluxionStream stream;

        if (buffer == NULL) return NULL;

        Fluxion_MemoryStream_InitWriter(&stream, buffer, capacity);
        if (Fluxion_Scene_Save(scene, &stream))
        {
            // Copied into a buffer of exactly the right size before it is
            // handed over. Without that, growing would leave the caller
            // holding a buffer whose real size it was never told -- and
            // giving it back with the wrong size is the kind of mistake an
            // allocator that ignores the size hides until one that does
            // not is used.
            u8* exact = (u8*)Fluxion_Allocator_Alloc(allocator, stream.position, FLUXION_DEFAULT_ALIGNMENT);
            if (exact == NULL)
            {
                Fluxion_Allocator_Free(allocator, buffer, capacity);
                return NULL;
            }

            memcpy(exact, buffer, stream.position);
            if (outSize != NULL) *outSize = stream.position;
            Fluxion_Allocator_Free(allocator, buffer, capacity);
            return exact;
        }

        Fluxion_Allocator_Free(allocator, buffer, capacity);
        if (!Fluxion_Stream_HasOverflowed(&stream)) return NULL; // refused for some other reason

        capacity *= 2;
    }
}

void Fluxion_Scene_FreeBuffer(u8* buffer, usize size)
{
    if (buffer == NULL) return;
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), buffer, size);
}
