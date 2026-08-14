#include <Fluxion/Scene/EntityCommandBuffer.h>

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

// The commands sit end to end in one run of bytes rather than in a table
// of a fixed-size command type. Two of them carry something of a size not
// known until they are recorded -- a name, a component value -- and a
// fixed-size record would have to either cap those or hold them somewhere
// else, which is two lifetimes to keep straight instead of one.
//
// Every command is a header followed by that command's payload, and the
// header says how long the payload is, so working through them needs to
// understand no command it is passing over. Both the header and the
// payload are copied in and out with memcpy, so nothing in the run has to
// be aligned and the run may be cut anywhere.

#define FLUXION_ECB_INITIAL_CAPACITY 1024

typedef enum FluxionEntityCommandKind
{
    FLUXION_ENTITY_COMMAND_CREATE = 0,
    FLUXION_ENTITY_COMMAND_DESTROY,
    FLUXION_ENTITY_COMMAND_SET_PARENT,
    FLUXION_ENTITY_COMMAND_ADD_COMPONENT,
    FLUXION_ENTITY_COMMAND_REMOVE_COMPONENT
} FluxionEntityCommandKind;

typedef struct FluxionEntityCommandHeader
{
    u32 kind;

    // Bytes following this header that belong to this command: the name
    // for a create, the component value for an add, nothing for the rest.
    u32 payloadSize;

    FluxionEntityTarget target;

    // The parent, for a set-parent. Left naming nothing otherwise.
    FluxionEntityTarget secondary;

    // The component type, for the two component commands. Invalid
    // otherwise.
    FluxionTypeId type;
} FluxionEntityCommandHeader;

struct FluxionEntityCommandBuffer
{
    u8* bytes;
    usize used;
    usize capacity;
    u32 count;
};

FluxionEntityTarget Fluxion_EntityTarget_Existing(FluxionGameObjectHandle object)
{
    FluxionEntityTarget target;
    target.handle = object;
    memset(&target.uuid, 0, sizeof(target.uuid));
    return target;
}

FluxionEntityTarget Fluxion_EntityTarget_Pending(FluxionUUID uuid)
{
    FluxionEntityTarget target;
    target.handle = Fluxion_GameObject_InvalidHandle();
    target.uuid = uuid;
    return target;
}

static FluxionEntityTarget Fluxion_EntityTarget_None(void)
{
    FluxionEntityTarget target;
    target.handle = Fluxion_GameObject_InvalidHandle();
    memset(&target.uuid, 0, sizeof(target.uuid));
    return target;
}

// The object this target names, or an invalid handle when it names none.
// A handle is believed as given -- resolving it is the job of whatever
// carries the command out, which refuses a stale one anyway.
static FluxionGameObjectHandle Fluxion_EntityTarget_Resolve(FluxionEntityTarget target, FluxionSceneHandle scene)
{
    if (FLUXION_HANDLE_IS_VALID(target.handle)) return target.handle;
    return Fluxion_Scene_FindByUUID(scene, target.uuid);
}

static bool Fluxion_EntityCommandBuffer_Reserve(FluxionEntityCommandBuffer* buffer, usize extra)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    usize wanted = buffer->used + extra;
    usize capacity = buffer->capacity;
    u8* grown;

    if (wanted <= capacity) return true;

    if (capacity == 0) capacity = FLUXION_ECB_INITIAL_CAPACITY;
    while (capacity < wanted) capacity *= 2;

    grown = (u8*)Fluxion_Allocator_Alloc(allocator, capacity, FLUXION_DEFAULT_ALIGNMENT);
    if (grown == NULL) return false;

    if (buffer->bytes != NULL)
    {
        memcpy(grown, buffer->bytes, buffer->used);
        Fluxion_Allocator_Free(allocator, buffer->bytes, buffer->capacity);
    }

    buffer->bytes = grown;
    buffer->capacity = capacity;
    return true;
}

static bool Fluxion_EntityCommandBuffer_Record(FluxionEntityCommandBuffer* buffer, const FluxionEntityCommandHeader* header, const void* payload)
{
    if (buffer == NULL) return false;
    if (!Fluxion_EntityCommandBuffer_Reserve(buffer, sizeof(*header) + header->payloadSize)) return false;

    memcpy(buffer->bytes + buffer->used, header, sizeof(*header));
    buffer->used += sizeof(*header);

    if (header->payloadSize != 0)
    {
        // Null with a payload size means "that many zero bytes" -- a
        // component asked for without a starting value.
        if (payload != NULL) memcpy(buffer->bytes + buffer->used, payload, header->payloadSize);
        else memset(buffer->bytes + buffer->used, 0, header->payloadSize);
        buffer->used += header->payloadSize;
    }

    ++buffer->count;
    return true;
}

static FluxionEntityCommandHeader Fluxion_EntityCommandHeader_Make(FluxionEntityCommandKind kind, FluxionEntityTarget target)
{
    FluxionEntityCommandHeader header;
    header.kind = (u32)kind;
    header.payloadSize = 0;
    header.target = target;
    header.secondary = Fluxion_EntityTarget_None();
    header.type = FLUXION_TYPE_ID_INVALID;
    return header;
}

FluxionEntityCommandBuffer* Fluxion_EntityCommandBuffer_Create(void)
{
    FluxionEntityCommandBuffer* buffer = (FluxionEntityCommandBuffer*)Fluxion_Allocator_Alloc(
        Fluxion_DefaultAllocator(), sizeof(FluxionEntityCommandBuffer), FLUXION_DEFAULT_ALIGNMENT);
    if (buffer == NULL) return NULL;

    memset(buffer, 0, sizeof(*buffer));
    return buffer;
}

void Fluxion_EntityCommandBuffer_Destroy(FluxionEntityCommandBuffer* buffer)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    if (buffer == NULL) return;

    if (buffer->bytes != NULL) Fluxion_Allocator_Free(allocator, buffer->bytes, buffer->capacity);
    Fluxion_Allocator_Free(allocator, buffer, sizeof(*buffer));
}

FluxionUUID Fluxion_EntityCommandBuffer_CreateGameObject(FluxionEntityCommandBuffer* buffer, const char* name)
{
    FluxionEntityCommandHeader header;
    FluxionUUID uuid;
    char stored[FLUXION_SCENE_MAX_NAME_LENGTH];

    memset(&uuid, 0, sizeof(uuid));
    if (buffer == NULL) return uuid;

    // Made now rather than at playback, because the caller is handed it
    // now: this id is the only way anything has of naming an object that
    // does not exist yet.
    uuid = Fluxion_UUID_Generate();

    memset(stored, 0, sizeof(stored));
    if (name != NULL)
    {
        usize length = strlen(name);
        if (length > sizeof(stored) - 1) length = sizeof(stored) - 1;
        memcpy(stored, name, length);
    }

    header = Fluxion_EntityCommandHeader_Make(FLUXION_ENTITY_COMMAND_CREATE, Fluxion_EntityTarget_Pending(uuid));
    header.payloadSize = (u32)sizeof(stored);

    if (!Fluxion_EntityCommandBuffer_Record(buffer, &header, stored))
    {
        memset(&uuid, 0, sizeof(uuid));
    }
    return uuid;
}

bool Fluxion_EntityCommandBuffer_DestroyGameObject(FluxionEntityCommandBuffer* buffer, FluxionEntityTarget object)
{
    FluxionEntityCommandHeader header = Fluxion_EntityCommandHeader_Make(FLUXION_ENTITY_COMMAND_DESTROY, object);
    return Fluxion_EntityCommandBuffer_Record(buffer, &header, NULL);
}

bool Fluxion_EntityCommandBuffer_SetParent(FluxionEntityCommandBuffer* buffer, FluxionEntityTarget object, FluxionEntityTarget parent)
{
    FluxionEntityCommandHeader header = Fluxion_EntityCommandHeader_Make(FLUXION_ENTITY_COMMAND_SET_PARENT, object);
    header.secondary = parent;
    return Fluxion_EntityCommandBuffer_Record(buffer, &header, NULL);
}

bool Fluxion_EntityCommandBuffer_AddComponent(FluxionEntityCommandBuffer* buffer, FluxionEntityTarget object, FluxionTypeId type, const void* value, usize valueSize)
{
    FluxionEntityCommandHeader header;
    const FluxionTypeInfo* typeInfo;

    if (buffer == NULL || type == FLUXION_TYPE_ID_INVALID) return false;
    if (!Fluxion_Reflection_IsInitialized()) return false;

    typeInfo = Fluxion_Reflection_FindTypeById(type);
    if (typeInfo == NULL || typeInfo->size == 0) return false;

    // Checked against the registered size here, where the caller can still
    // be told. Playback happens somewhere else entirely, and a mismatch
    // found there would be a copy of the wrong length into somebody's
    // component with nobody left to hand the refusal to.
    if (value != NULL && valueSize != typeInfo->size) return false;

    header = Fluxion_EntityCommandHeader_Make(FLUXION_ENTITY_COMMAND_ADD_COMPONENT, object);
    header.type = type;
    header.payloadSize = (u32)typeInfo->size;

    return Fluxion_EntityCommandBuffer_Record(buffer, &header, value);
}

bool Fluxion_EntityCommandBuffer_RemoveComponent(FluxionEntityCommandBuffer* buffer, FluxionEntityTarget object, FluxionTypeId type)
{
    FluxionEntityCommandHeader header;
    if (type == FLUXION_TYPE_ID_INVALID) return false;

    header = Fluxion_EntityCommandHeader_Make(FLUXION_ENTITY_COMMAND_REMOVE_COMPONENT, object);
    header.type = type;
    return Fluxion_EntityCommandBuffer_Record(buffer, &header, NULL);
}

u32 Fluxion_EntityCommandBuffer_Count(const FluxionEntityCommandBuffer* buffer)
{
    return buffer != NULL ? buffer->count : 0u;
}

void Fluxion_EntityCommandBuffer_Clear(FluxionEntityCommandBuffer* buffer)
{
    if (buffer == NULL) return;

    // The storage stays. A buffer emptied every turn is filled again the
    // next one, and giving the bytes back only to ask for them again is
    // work with nothing to show for it.
    buffer->used = 0;
    buffer->count = 0;
}

// One command. Answers whether it landed; the caller counts the ones that
// did not.
static bool Fluxion_EntityCommandBuffer_Apply(const FluxionEntityCommandHeader* header, const u8* payload, FluxionSceneHandle scene)
{
    FluxionGameObjectHandle object;

    switch ((FluxionEntityCommandKind)header->kind)
    {
    case FLUXION_ENTITY_COMMAND_CREATE:
        // The id is the one handed back when this was recorded, so an
        // object made here answers to exactly what the caller was told.
        object = Fluxion_Scene_CreateGameObjectWithUUID(scene, (const char*)payload, header->target.uuid);
        return FLUXION_HANDLE_IS_VALID(object);

    case FLUXION_ENTITY_COMMAND_DESTROY:
        object = Fluxion_EntityTarget_Resolve(header->target, scene);
        if (!Fluxion_GameObject_IsValid(scene, object)) return false;
        Fluxion_GameObject_Destroy(scene, object);
        return true;

    case FLUXION_ENTITY_COMMAND_SET_PARENT:
        object = Fluxion_EntityTarget_Resolve(header->target, scene);
        if (!Fluxion_GameObject_IsValid(scene, object)) return false;
        Fluxion_GameObject_SetParent(scene, object, Fluxion_EntityTarget_Resolve(header->secondary, scene));
        return true;

    case FLUXION_ENTITY_COMMAND_ADD_COMPONENT:
        object = Fluxion_EntityTarget_Resolve(header->target, scene);
        return Fluxion_GameObject_AddComponent(scene, object, header->type, payload) != NULL;

    case FLUXION_ENTITY_COMMAND_REMOVE_COMPONENT:
        object = Fluxion_EntityTarget_Resolve(header->target, scene);
        return Fluxion_GameObject_RemoveComponent(scene, object, header->type);

    default:
        return false;
    }
}

u32 Fluxion_EntityCommandBuffer_Playback(FluxionEntityCommandBuffer* buffer, FluxionSceneHandle scene)
{
    usize at = 0;
    u32 failed = 0;

    if (buffer == NULL) return 0;

    // Emptied even when the scene is gone. A buffer that kept its commands
    // because there was nothing to run them against would run them at the
    // next scene it was handed instead, which is not what anybody asked
    // for.
    if (!Fluxion_Scene_IsValid(scene))
    {
        failed = buffer->count;
        Fluxion_EntityCommandBuffer_Clear(buffer);
        return failed;
    }

    while (at + sizeof(FluxionEntityCommandHeader) <= buffer->used)
    {
        FluxionEntityCommandHeader header;
        const u8* payload;

        memcpy(&header, buffer->bytes + at, sizeof(header));
        at += sizeof(header);

        if (at + header.payloadSize > buffer->used) break;
        payload = (header.payloadSize != 0) ? buffer->bytes + at : NULL;
        at += header.payloadSize;

        if (!Fluxion_EntityCommandBuffer_Apply(&header, payload, scene)) ++failed;
    }

    Fluxion_EntityCommandBuffer_Clear(buffer);
    return failed;
}

FluxionEntityCommandBuffer* Fluxion_Scene_GetCommandBuffer(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == NULL) return NULL;

    // Made when it is first asked for: a scene that never defers anything
    // never pays for a buffer.
    if (record->commandBuffer == NULL) record->commandBuffer = Fluxion_EntityCommandBuffer_Create();
    return record->commandBuffer;
}

void Fluxion_SceneInternal_PlaybackCommandBuffer(FluxionSceneRecord* record)
{
    if (record == NULL || record->commandBuffer == NULL) return;
    (void)Fluxion_EntityCommandBuffer_Playback(record->commandBuffer, record->self);
}

void Fluxion_SceneInternal_ReleaseCommandBuffer(FluxionSceneRecord* record)
{
    if (record == NULL || record->commandBuffer == NULL) return;

    Fluxion_EntityCommandBuffer_Destroy(record->commandBuffer);
    record->commandBuffer = NULL;
}
