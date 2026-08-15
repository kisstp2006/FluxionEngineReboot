// Data components, stored by which set of them an object carries.
//
// The idea in one line: objects that carry the same set of component
// types are kept together, their components side by side in columns, so
// that reading one component of every such object is one straight run
// through memory.
//
// Nothing here is authored. A composition comes into being the moment the
// first object carries that set, and it is nothing but that set: there is
// no list of compositions to maintain, and introducing a new component
// type needs no work in this file at all.
//
// What it costs, and it is the whole trade: giving an object a component
// changes WHICH set it carries, so the object no longer belongs where it
// was. Its components are copied to a block of the new composition and
// the old row is closed up. That is why a pointer into this storage
// survives only until the next structural change, and why anything
// changing objects while walking over them records the changes and lets
// them land afterwards.

#include "SceneInternal.h"

#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Scene/EntityQuery.h>

#include <string.h>

// How many blocks a composition's block array starts at, and what it
// grows by. Blocks themselves never move -- only the array of pointers to
// their bookkeeping does -- so growing it cannot invalidate anything a
// caller holds.
#define FLUXION_SCENE_CHUNK_ARRAY_INITIAL 4

// --- Small helpers -------------------------------------------------------

static usize Fluxion_SceneArchetype_AlignUp(usize value)
{
    const usize alignment = FLUXION_DEFAULT_ALIGNMENT;
    return (value + alignment - 1u) / alignment * alignment;
}

// Where in this composition's type list a type sits, or the count when it
// is not there. The list is sorted, but it is at most
// FLUXION_SCENE_MAX_COMPONENT_TYPES long, so a straight scan beats a
// search that has to be got right.
static u32 Fluxion_SceneArchetype_IndexOfType(const FluxionSceneArchetype* archetype, FluxionTypeId type)
{
    u32 i;
    for (i = 0; i < archetype->typeCount; ++i)
    {
        if (archetype->types[i] == type) return i;
    }
    return archetype->typeCount;
}

static bool Fluxion_SceneArchetype_HasType(const FluxionSceneArchetype* archetype, FluxionTypeId type)
{
    return Fluxion_SceneArchetype_IndexOfType(archetype, type) != archetype->typeCount;
}

// Insertion sort over a list that never exceeds
// FLUXION_SCENE_MAX_COMPONENT_TYPES. Sorting is what makes two objects
// given the same components in different orders end up in the same
// composition rather than in two identical-looking ones.
static void Fluxion_SceneArchetype_SortTypes(FluxionTypeId* types, u32 count)
{
    u32 i;
    for (i = 1; i < count; ++i)
    {
        const FluxionTypeId value = types[i];
        u32 j = i;
        while (j > 0 && types[j - 1] > value)
        {
            types[j] = types[j - 1];
            --j;
        }
        types[j] = value;
    }
}

// --- Finding and making a composition ------------------------------------

// The composition carrying exactly this sorted set, or
// FLUXION_SCENE_NO_ARCHETYPE. Both lists are sorted, so "the same set" is
// "the same sequence".
static u32 Fluxion_SceneArchetype_Find(const FluxionSceneRecord* record, const FluxionTypeId* sortedTypes, u32 typeCount)
{
    u32 i;
    for (i = 0; i < FLUXION_SCENE_MAX_ARCHETYPES; ++i)
    {
        const FluxionSceneArchetype* archetype = &record->archetypes[i];
        if (!archetype->inUse || archetype->typeCount != typeCount) continue;
        if (typeCount == 0 || memcmp(archetype->types, sortedTypes, typeCount * sizeof(FluxionTypeId)) == 0) return i;
    }
    return FLUXION_SCENE_NO_ARCHETYPE;
}

// Works out where each column starts inside a block, and how many rows
// fit. Answers false when one row of this composition is wider than a
// whole block, which is the one arrangement that cannot be stored at all.
static bool Fluxion_SceneArchetype_Layout(FluxionSceneArchetype* archetype)
{
    usize stride = sizeof(FluxionEntityHandle);
    usize offset;
    u32 i;

    for (i = 0; i < archetype->typeCount; ++i) stride += archetype->elementSizes[i];
    if (stride == 0) return false;

    // Each column is padded out to the alignment the storage promises, so
    // the padding has to be paid for once per column rather than once per
    // row. Capacity is whatever survives that.
    {
        const usize columns = (usize)archetype->typeCount + 1u;
        const usize slack = columns * FLUXION_DEFAULT_ALIGNMENT;
        usize usable;

        if (FLUXION_SCENE_CHUNK_BYTES <= slack) return false;
        usable = (usize)FLUXION_SCENE_CHUNK_BYTES - slack;

        archetype->capacity = (u32)(usable / stride);
        if (archetype->capacity == 0) return false;
    }

    // Entities first, so that every composition -- including the one
    // carrying nothing -- has a column 0 that means the same thing.
    offset = 0;
    archetype->entityColumnOffset = offset;
    offset = Fluxion_SceneArchetype_AlignUp(offset + (usize)archetype->capacity * sizeof(FluxionEntityHandle));

    for (i = 0; i < archetype->typeCount; ++i)
    {
        archetype->columnOffsets[i] = offset;
        offset = Fluxion_SceneArchetype_AlignUp(offset + (usize)archetype->capacity * archetype->elementSizes[i]);
    }

    FLUXION_ASSERT_MSG(offset <= FLUXION_SCENE_CHUNK_BYTES,
        "Fluxion: a block's columns were laid out past the end of the block");
    return offset <= FLUXION_SCENE_CHUNK_BYTES;
}

// The composition carrying this set, made if it does not exist yet.
// FLUXION_SCENE_NO_ARCHETYPE when a type is not registered, when the
// scene holds as many compositions as it can, or when one row would not
// fit in a block.
static u32 Fluxion_SceneArchetype_FindOrCreate(FluxionSceneRecord* record, const FluxionTypeId* types, u32 typeCount)
{
    FluxionTypeId sorted[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    FluxionSceneArchetype* archetype;
    u32 index;
    u32 i;

    if (typeCount > FLUXION_SCENE_MAX_COMPONENT_TYPES) return FLUXION_SCENE_NO_ARCHETYPE;

    if (typeCount != 0) memcpy(sorted, types, typeCount * sizeof(FluxionTypeId));
    Fluxion_SceneArchetype_SortTypes(sorted, typeCount);

    index = Fluxion_SceneArchetype_Find(record, sorted, typeCount);
    if (index != FLUXION_SCENE_NO_ARCHETYPE) return index;

    for (index = 0; index < FLUXION_SCENE_MAX_ARCHETYPES; ++index)
    {
        if (!record->archetypes[index].inUse) break;
    }
    if (index == FLUXION_SCENE_MAX_ARCHETYPES) return FLUXION_SCENE_NO_ARCHETYPE;

    archetype = &record->archetypes[index];
    memset(archetype, 0, sizeof(*archetype));
    archetype->typeCount = typeCount;
    if (typeCount != 0) memcpy(archetype->types, sorted, typeCount * sizeof(FluxionTypeId));

    // Sizes are read once, here, and kept. Reading them again later would
    // let a type re-registered at a different size reinterpret storage
    // that was already laid out for the old one.
    for (i = 0; i < typeCount; ++i)
    {
        const FluxionTypeInfo* typeInfo;

        FLUXION_ASSERT_MSG(Fluxion_Reflection_IsInitialized(),
            "Fluxion: a data component was asked for before the reflection registry was brought up");
        if (!Fluxion_Reflection_IsInitialized()) return FLUXION_SCENE_NO_ARCHETYPE;

        typeInfo = Fluxion_Reflection_FindTypeById(archetype->types[i]);
        if (typeInfo == NULL || typeInfo->size == 0) return FLUXION_SCENE_NO_ARCHETYPE;
        archetype->elementSizes[i] = typeInfo->size;
    }

    if (!Fluxion_SceneArchetype_Layout(archetype)) return FLUXION_SCENE_NO_ARCHETYPE;

    archetype->inUse = true;
    return index;
}

// --- Blocks --------------------------------------------------------------

static bool Fluxion_SceneArchetype_ReserveChunkArray(FluxionSceneArchetype* archetype)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    u32 grown;
    FluxionSceneChunk* chunks;

    if (archetype->chunkCount < archetype->chunkCapacity) return true;

    grown = (archetype->chunkCapacity == 0) ? FLUXION_SCENE_CHUNK_ARRAY_INITIAL : archetype->chunkCapacity * 2u;
    chunks = (FluxionSceneChunk*)Fluxion_Allocator_Alloc(allocator, grown * sizeof(FluxionSceneChunk), FLUXION_DEFAULT_ALIGNMENT);
    if (chunks == NULL) return false;

    memset(chunks, 0, grown * sizeof(FluxionSceneChunk));
    if (archetype->chunks != NULL)
    {
        memcpy(chunks, archetype->chunks, archetype->chunkCount * sizeof(FluxionSceneChunk));
        Fluxion_Allocator_Free(allocator, archetype->chunks, archetype->chunkCapacity * sizeof(FluxionSceneChunk));
    }

    archetype->chunks = chunks;
    archetype->chunkCapacity = grown;
    return true;
}

// The last block if it has room, otherwise a new one. Blocks are filled
// in order and only the last is ever partly full, which is what lets a
// removal always take its replacement row from one known place.
static u32 Fluxion_SceneArchetype_ChunkWithRoom(FluxionSceneArchetype* archetype)
{
    FluxionSceneChunk* chunk;

    if (archetype->chunkCount != 0)
    {
        chunk = &archetype->chunks[archetype->chunkCount - 1];
        if (chunk->count < archetype->capacity) return archetype->chunkCount - 1;
    }

    if (!Fluxion_SceneArchetype_ReserveChunkArray(archetype)) return FLUXION_SCENE_NO_ARCHETYPE;

    chunk = &archetype->chunks[archetype->chunkCount];
    chunk->bytes = (u8*)Fluxion_Allocator_Alloc(Fluxion_DefaultAllocator(), FLUXION_SCENE_CHUNK_BYTES, FLUXION_DEFAULT_ALIGNMENT);
    if (chunk->bytes == NULL) return FLUXION_SCENE_NO_ARCHETYPE;

    // Zeroed on the way in, so a component read before anything wrote it
    // is a defined zero rather than whatever the last object in this row
    // left behind.
    memset(chunk->bytes, 0, FLUXION_SCENE_CHUNK_BYTES);
    chunk->count = 0;

    return archetype->chunkCount++;
}

static FluxionEntityHandle* Fluxion_SceneArchetype_EntityColumn(const FluxionSceneArchetype* archetype, u32 chunkIndex)
{
    return (FluxionEntityHandle*)(archetype->chunks[chunkIndex].bytes + archetype->entityColumnOffset);
}

void* Fluxion_SceneArchetype_ColumnAt(const FluxionSceneArchetype* archetype, u32 chunkIndex, FluxionTypeId type)
{
    u32 typeIndex;

    if (archetype == NULL || !archetype->inUse || chunkIndex >= archetype->chunkCount) return NULL;

    typeIndex = Fluxion_SceneArchetype_IndexOfType(archetype, type);
    if (typeIndex == archetype->typeCount) return NULL;

    return archetype->chunks[chunkIndex].bytes + archetype->columnOffsets[typeIndex];
}

// One value of one type, by row.
static void* Fluxion_SceneArchetype_ValueAt(const FluxionSceneArchetype* archetype, u32 chunkIndex, u32 row, u32 typeIndex)
{
    return archetype->chunks[chunkIndex].bytes
        + archetype->columnOffsets[typeIndex]
        + (usize)row * archetype->elementSizes[typeIndex];
}

// --- Adding and taking away rows -----------------------------------------

// Takes a row out of a composition, moving the very last row of the very
// last block into the hole so the blocks stay dense.
//
// The half that is easy to leave out is the second one: the object whose
// row moved has to be told where it went. Forgetting it does not crash --
// it makes that object read somebody else's components from then on.
static void Fluxion_SceneArchetype_ReleaseRow(FluxionSceneRecord* record, u32 archetypeIndex, u32 chunkIndex, u32 row)
{
    FluxionSceneArchetype* archetype = &record->archetypes[archetypeIndex];
    const u32 lastChunkIndex = archetype->chunkCount - 1;
    FluxionSceneChunk* lastChunk = &archetype->chunks[lastChunkIndex];
    const u32 lastRow = lastChunk->count - 1;
    u32 i;

    if (!(chunkIndex == lastChunkIndex && row == lastRow))
    {
        FluxionEntityHandle* entities = Fluxion_SceneArchetype_EntityColumn(archetype, chunkIndex);
        FluxionEntityHandle* lastEntities = Fluxion_SceneArchetype_EntityColumn(archetype, lastChunkIndex);
        const FluxionEntityHandle moved = lastEntities[lastRow];

        entities[row] = moved;
        for (i = 0; i < archetype->typeCount; ++i)
        {
            memcpy(Fluxion_SceneArchetype_ValueAt(archetype, chunkIndex, row, i),
                   Fluxion_SceneArchetype_ValueAt(archetype, lastChunkIndex, lastRow, i),
                   archetype->elementSizes[i]);
        }

        if (moved.index < FLUXION_SCENE_MAX_GAME_OBJECTS)
        {
            FluxionSceneGameObjectRecord* movedRecord = &record->objects[moved.index];
            movedRecord->chunkIndex = chunkIndex;
            movedRecord->rowInChunk = row;
        }
    }

    --lastChunk->count;

    // An emptied block is given back rather than kept: a composition that
    // filled up once and then drained would otherwise hold every block it
    // ever needed for as long as the scene lives. The first block stays,
    // because a composition in use is about to want it again.
    if (lastChunk->count == 0 && archetype->chunkCount > 1)
    {
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), lastChunk->bytes, FLUXION_SCENE_CHUNK_BYTES);
        lastChunk->bytes = NULL;
        --archetype->chunkCount;
    }
}

// Puts an object into a composition and answers which row it got.
static bool Fluxion_SceneArchetype_ClaimRow(FluxionSceneRecord* record, u32 archetypeIndex, FluxionGameObjectHandle object,
                                            u32* outChunkIndex, u32* outRow)
{
    FluxionSceneArchetype* archetype = &record->archetypes[archetypeIndex];
    const u32 chunkIndex = Fluxion_SceneArchetype_ChunkWithRoom(archetype);
    FluxionSceneChunk* chunk;
    u32 row;

    if (chunkIndex == FLUXION_SCENE_NO_ARCHETYPE) return false;

    chunk = &archetype->chunks[chunkIndex];
    row = chunk->count++;
    Fluxion_SceneArchetype_EntityColumn(archetype, chunkIndex)[row] = object;

    *outChunkIndex = chunkIndex;
    *outRow = row;
    return true;
}

// Moves an object from the composition it is in to another one, carrying
// across every component both compositions have. Components only the old
// one had are dropped; components only the new one has are left as the
// zero the block was cleared to.
static bool Fluxion_SceneArchetype_Move(FluxionSceneRecord* record, FluxionSceneGameObjectRecord* entry,
                                        FluxionGameObjectHandle object, u32 toArchetypeIndex)
{
    const u32 fromArchetypeIndex = entry->archetypeIndex;
    const u32 fromChunkIndex = entry->chunkIndex;
    const u32 fromRow = entry->rowInChunk;
    u32 toChunkIndex = 0;
    u32 toRow = 0;
    u32 i;

    if (fromArchetypeIndex == toArchetypeIndex) return true;

    if (!Fluxion_SceneArchetype_ClaimRow(record, toArchetypeIndex, object, &toChunkIndex, &toRow)) return false;

    // Claiming the row may have grown the destination's block array, so
    // both archetypes are taken by index here rather than held across the
    // call above.
    {
        const FluxionSceneArchetype* from = &record->archetypes[fromArchetypeIndex];
        const FluxionSceneArchetype* to = &record->archetypes[toArchetypeIndex];

        for (i = 0; i < from->typeCount; ++i)
        {
            const u32 destination = Fluxion_SceneArchetype_IndexOfType(to, from->types[i]);
            if (destination == to->typeCount) continue;

            memcpy(Fluxion_SceneArchetype_ValueAt(to, toChunkIndex, toRow, destination),
                   Fluxion_SceneArchetype_ValueAt(from, fromChunkIndex, fromRow, i),
                   from->elementSizes[i]);
        }
    }

    // Written before the old row is released: releasing it can move some
    // OTHER object's row, and that object's record must be the only one
    // the release touches.
    entry->archetypeIndex = toArchetypeIndex;
    entry->chunkIndex = toChunkIndex;
    entry->rowInChunk = toRow;

    Fluxion_SceneArchetype_ReleaseRow(record, fromArchetypeIndex, fromChunkIndex, fromRow);
    return true;
}

// --- What the rest of the module calls -----------------------------------

bool Fluxion_SceneArchetype_PlaceNewObject(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry;
    u32 chunkIndex = 0;
    u32 row = 0;

    if (record == NULL || object.index >= FLUXION_SCENE_MAX_GAME_OBJECTS) return false;

    // Every object starts carrying the two things that are part of an
    // object rather than attached to it: where it is, and what scripts
    // hang off it. There is no composition below this one, because there
    // is no object without a place to be.
    if (record->baseArchetype == FLUXION_SCENE_NO_ARCHETYPE)
    {
        const FluxionTypeId intrinsic[2] = { Fluxion_Transform_TypeId(), Fluxion_ScriptComponent_TypeId() };
        if (!Fluxion_SceneTransform_EnsureRegistered()) return false;
        if (!Fluxion_SceneLight_EnsureRegistered()) return false;

        record->baseArchetype = Fluxion_SceneArchetype_FindOrCreate(record, intrinsic, 2);
        if (record->baseArchetype == FLUXION_SCENE_NO_ARCHETYPE) return false;
    }

    if (!Fluxion_SceneArchetype_ClaimRow(record, record->baseArchetype, object, &chunkIndex, &row)) return false;

    entry = &record->objects[object.index];
    entry->archetypeIndex = record->baseArchetype;
    entry->chunkIndex = chunkIndex;
    entry->rowInChunk = row;
    return true;
}

void* Fluxion_SceneArchetype_ValueOf(FluxionSceneRecord* record, const FluxionSceneGameObjectRecord* entry, FluxionTypeId type)
{
    const FluxionSceneArchetype* archetype;
    u32 typeIndex;

    if (record == NULL || entry == NULL || entry->archetypeIndex == FLUXION_SCENE_NO_ARCHETYPE) return NULL;

    archetype = &record->archetypes[entry->archetypeIndex];
    typeIndex = Fluxion_SceneArchetype_IndexOfType(archetype, type);
    if (typeIndex == archetype->typeCount) return NULL;

    return Fluxion_SceneArchetype_ValueAt(archetype, entry->chunkIndex, entry->rowInChunk, typeIndex);
}

void Fluxion_SceneArchetype_RemoveObject(FluxionSceneRecord* record, FluxionGameObjectHandle object)
{
    FluxionSceneGameObjectRecord* entry;

    if (record == NULL || object.index >= FLUXION_SCENE_MAX_GAME_OBJECTS) return;

    entry = &record->objects[object.index];
    if (entry->archetypeIndex == FLUXION_SCENE_NO_ARCHETYPE) return;

    Fluxion_SceneArchetype_ReleaseRow(record, entry->archetypeIndex, entry->chunkIndex, entry->rowInChunk);
    entry->archetypeIndex = FLUXION_SCENE_NO_ARCHETYPE;
    entry->chunkIndex = 0;
    entry->rowInChunk = 0;
}

void Fluxion_SceneArchetype_ReleaseScene(FluxionSceneRecord* record)
{
    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    u32 i;
    u32 c;

    if (record == NULL) return;

    for (i = 0; i < FLUXION_SCENE_MAX_ARCHETYPES; ++i)
    {
        FluxionSceneArchetype* archetype = &record->archetypes[i];
        if (!archetype->inUse) continue;

        for (c = 0; c < archetype->chunkCount; ++c)
        {
            if (archetype->chunks[c].bytes == NULL) continue;
            Fluxion_Allocator_Free(allocator, archetype->chunks[c].bytes, FLUXION_SCENE_CHUNK_BYTES);
        }
        if (archetype->chunks != NULL)
        {
            Fluxion_Allocator_Free(allocator, archetype->chunks, archetype->chunkCapacity * sizeof(FluxionSceneChunk));
        }

        memset(archetype, 0, sizeof(*archetype));
    }

    record->baseArchetype = FLUXION_SCENE_NO_ARCHETYPE;
}

// --- The public per-object interface -------------------------------------

// The one place a system's declaration is held against what it does.
//
// A declaration nothing checks is decoration, and the parallelism rests
// entirely on it being true -- so an omission has to be found where it is
// made rather than as a race on somebody else's machine. In a build with
// assertions compiled out this costs nothing and checks nothing, which is
// why the tests run in a build that has them.
static bool Fluxion_SceneArchetype_AllowedHere(FluxionSceneRecord* record, FluxionTypeId type, bool structural)
{
    if (Fluxion_SceneInternal_SystemMayTouch(record, type, structural)) return true;

    // Both messages name the system and the type, because the mistake is
    // always in one particular declaration and finding which one from a
    // bare "a system did something" would mean reading them all.
    {
        const FluxionTypeInfo* typeInfo = Fluxion_Reflection_IsInitialized() ? Fluxion_Reflection_FindTypeById(type) : NULL;
        const char* typeName = (typeInfo != NULL) ? typeInfo->name.data : "an unregistered type";
        const char* systemName = Fluxion_SceneInternal_RunningSystemName(record);

        if (structural)
        {
            FLUXION_LOG_ERROR("Scene.Systems",
                "system '%s' added or removed '%s' directly -- record it into the scene's command buffer instead, "
                "so it lands once the phase is over and nothing is walking the storage",
                systemName, typeName);
            FLUXION_ASSERT_MSG(false, "Fluxion: a system changed the storage directly -- see the log line above");
        }
        else
        {
            FLUXION_LOG_ERROR("Scene.Systems",
                "system '%s' touched '%s', which it did not declare -- add it to that system's reads or writes",
                systemName, typeName);
            FLUXION_ASSERT_MSG(false, "Fluxion: a system touched an undeclared component -- see the log line above");
        }
    }
    return false;
}


void* Fluxion_GameObject_AddComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type, const void* initialValue)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionTypeId wanted[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    u32 wantedCount;
    u32 target;
    u32 typeIndex;

    if (entry == NULL || type == FLUXION_TYPE_ID_INVALID) return NULL;
    if (entry->archetypeIndex == FLUXION_SCENE_NO_ARCHETYPE) return NULL;
    if (!Fluxion_SceneArchetype_AllowedHere(record, type, true)) return NULL;

    // Already carrying it: the existing one comes back and nothing moves.
    // One of a type per object is what lets the composition be a set
    // rather than a list.
    {
        const FluxionSceneArchetype* current = &record->archetypes[entry->archetypeIndex];
        typeIndex = Fluxion_SceneArchetype_IndexOfType(current, type);
        if (typeIndex != current->typeCount)
        {
            return Fluxion_SceneArchetype_ValueAt(current, entry->chunkIndex, entry->rowInChunk, typeIndex);
        }

        wantedCount = current->typeCount;
        if (wantedCount >= FLUXION_SCENE_MAX_COMPONENT_TYPES) return NULL;
        if (wantedCount != 0) memcpy(wanted, current->types, wantedCount * sizeof(FluxionTypeId));
    }
    wanted[wantedCount++] = type;

    target = Fluxion_SceneArchetype_FindOrCreate(record, wanted, wantedCount);
    if (target == FLUXION_SCENE_NO_ARCHETYPE) return NULL;

    if (!Fluxion_SceneArchetype_Move(record, entry, object, target)) return NULL;

    {
        const FluxionSceneArchetype* archetype = &record->archetypes[target];
        void* value;

        typeIndex = Fluxion_SceneArchetype_IndexOfType(archetype, type);
        FLUXION_ASSERT_MSG(typeIndex != archetype->typeCount,
            "Fluxion: an object was moved to a composition that does not carry the component it was given");
        if (typeIndex == archetype->typeCount) return NULL;

        value = Fluxion_SceneArchetype_ValueAt(archetype, entry->chunkIndex, entry->rowInChunk, typeIndex);

        // The row's other columns were carried across by the move; this
        // one is new, and is either what the caller asked for or zero. It
        // has to be written even when the caller passed nothing, because
        // the row may have been used by an object that has since gone.
        if (initialValue != NULL) memcpy(value, initialValue, archetype->elementSizes[typeIndex]);
        else memset(value, 0, archetype->elementSizes[typeIndex]);

        return value;
    }
}

void* Fluxion_GameObject_GetComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    const FluxionSceneArchetype* archetype;
    u32 typeIndex;

    if (entry == NULL || entry->archetypeIndex == FLUXION_SCENE_NO_ARCHETYPE) return NULL;
    if (!Fluxion_SceneArchetype_AllowedHere(record, type, false)) return NULL;

    archetype = &record->archetypes[entry->archetypeIndex];
    typeIndex = Fluxion_SceneArchetype_IndexOfType(archetype, type);
    if (typeIndex == archetype->typeCount) return NULL;

    return Fluxion_SceneArchetype_ValueAt(archetype, entry->chunkIndex, entry->rowInChunk, typeIndex);
}

bool Fluxion_GameObject_HasComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type)
{
    return Fluxion_GameObject_GetComponent(scene, object, type) != NULL;
}

bool Fluxion_GameObject_RemoveComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    FluxionTypeId wanted[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    u32 wantedCount = 0;
    u32 target;
    u32 i;

    if (entry == NULL || entry->archetypeIndex == FLUXION_SCENE_NO_ARCHETYPE) return false;
    if (!Fluxion_SceneArchetype_AllowedHere(record, type, true)) return false;

    // The two components that are part of an object rather than attached
    // to it. Refused rather than allowed, so that "where is this object"
    // and "what scripts does it have" can never be answered with
    // "nowhere" -- much of the rest of this module is written on the
    // strength of that.
    if (type == Fluxion_Transform_TypeId())
    {
        Fluxion_SceneInternal_SetError(record, "an object's transform is part of it and cannot be taken away");
        return false;
    }
    if (type == Fluxion_ScriptComponent_TypeId())
    {
        Fluxion_SceneInternal_SetError(record, "an object's link to its scripts is part of it and cannot be taken away");
        return false;
    }

    {
        const FluxionSceneArchetype* current = &record->archetypes[entry->archetypeIndex];
        if (!Fluxion_SceneArchetype_HasType(current, type)) return false;

        for (i = 0; i < current->typeCount; ++i)
        {
            if (current->types[i] == type) continue;
            wanted[wantedCount++] = current->types[i];
        }
    }

    target = Fluxion_SceneArchetype_FindOrCreate(record, wanted, wantedCount);
    if (target == FLUXION_SCENE_NO_ARCHETYPE) return false;

    return Fluxion_SceneArchetype_Move(record, entry, object, target);
}

u32 Fluxion_GameObject_GetComponentTypes(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId* outTypes, u32 maxTypes)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneGameObjectRecord* entry = Fluxion_SceneInternal_ResolveObject(record, object);
    const FluxionSceneArchetype* archetype;
    u32 written;

    if (entry == NULL || entry->archetypeIndex == FLUXION_SCENE_NO_ARCHETYPE) return 0;

    archetype = &record->archetypes[entry->archetypeIndex];
    if (outTypes == NULL || maxTypes == 0) return archetype->typeCount;

    written = (archetype->typeCount < maxTypes) ? archetype->typeCount : maxTypes;
    if (written != 0) memcpy(outTypes, archetype->types, written * sizeof(FluxionTypeId));
    return written;
}

u32 Fluxion_Scene_ComponentCount(FluxionSceneHandle scene, FluxionTypeId type)
{
    FluxionEntityQueryDesc desc;
    desc.required = &type;
    desc.requiredCount = 1;
    desc.excluded = NULL;
    desc.excludedCount = 0;
    return Fluxion_Scene_CountMatching(scene, &desc);
}

// --- Queries -------------------------------------------------------------

FluxionEntityQuery Fluxion_Scene_Query(FluxionSceneHandle scene, const FluxionEntityQueryDesc* desc)
{
    FluxionEntityQuery query;
    memset(&query, 0, sizeof(query));

    query.scene = scene;
    query.archetypeIndex = 0;
    query.chunkIndex = 0;
    query.valid = false;

    if (Fluxion_SceneInternal_Resolve(scene) == NULL || desc == NULL) return query;
    if (desc->requiredCount > FLUXION_SCENE_MAX_COMPONENT_TYPES) return query;
    if (desc->excludedCount > FLUXION_SCENE_MAX_COMPONENT_TYPES) return query;

    // Copied rather than pointed at, so the arrays the caller passed need
    // not outlive the walk -- a query is usually made from a list built on
    // the stack right where it is used.
    if (desc->requiredCount != 0)
    {
        if (desc->required == NULL) return query;
        memcpy(query.required, desc->required, desc->requiredCount * sizeof(FluxionTypeId));
    }
    if (desc->excludedCount != 0)
    {
        if (desc->excluded == NULL) return query;
        memcpy(query.excluded, desc->excluded, desc->excludedCount * sizeof(FluxionTypeId));
    }
    query.requiredCount = desc->requiredCount;
    query.excludedCount = desc->excludedCount;
    query.valid = true;
    return query;
}

static bool Fluxion_SceneArchetype_Matches(const FluxionSceneArchetype* archetype, const FluxionEntityQuery* query)
{
    u32 i;

    for (i = 0; i < query->requiredCount; ++i)
    {
        if (!Fluxion_SceneArchetype_HasType(archetype, query->required[i])) return false;
    }
    for (i = 0; i < query->excludedCount; ++i)
    {
        if (Fluxion_SceneArchetype_HasType(archetype, query->excluded[i])) return false;
    }
    return true;
}

bool Fluxion_EntityQuery_Next(FluxionEntityQuery* query, FluxionEntityChunkView* outChunk)
{
    FluxionSceneRecord* record;

    if (query == NULL || outChunk == NULL || !query->valid) return false;

    record = Fluxion_SceneInternal_Resolve(query->scene);
    if (record == NULL) return false;

    while (query->archetypeIndex < FLUXION_SCENE_MAX_ARCHETYPES)
    {
        const FluxionSceneArchetype* archetype = &record->archetypes[query->archetypeIndex];

        if (!archetype->inUse || !Fluxion_SceneArchetype_Matches(archetype, query))
        {
            ++query->archetypeIndex;
            query->chunkIndex = 0;
            continue;
        }

        while (query->chunkIndex < archetype->chunkCount)
        {
            const u32 chunkIndex = query->chunkIndex++;
            const FluxionSceneChunk* chunk = &archetype->chunks[chunkIndex];

            // An empty block is passed over rather than handed out, so a
            // caller never has to test the count before reading.
            if (chunk->count == 0) continue;

            outChunk->entities = Fluxion_SceneArchetype_EntityColumn(archetype, chunkIndex);
            outChunk->count = chunk->count;
            outChunk->scene = query->scene;
            outChunk->archetypeIndex = query->archetypeIndex;
            outChunk->chunkIndex = chunkIndex;
            return true;
        }

        ++query->archetypeIndex;
        query->chunkIndex = 0;
    }

    return false;
}

void* Fluxion_EntityChunk_Column(const FluxionEntityChunkView* chunk, FluxionTypeId type)
{
    FluxionSceneRecord* record;

    if (chunk == NULL) return NULL;

    record = Fluxion_SceneInternal_Resolve(chunk->scene);
    if (record == NULL || chunk->archetypeIndex >= FLUXION_SCENE_MAX_ARCHETYPES) return NULL;
    if (!Fluxion_SceneArchetype_AllowedHere(record, type, false)) return NULL;

    return Fluxion_SceneArchetype_ColumnAt(&record->archetypes[chunk->archetypeIndex], chunk->chunkIndex, type);
}

u32 Fluxion_Scene_CountMatching(FluxionSceneHandle scene, const FluxionEntityQueryDesc* desc)
{
    FluxionEntityQuery query = Fluxion_Scene_Query(scene, desc);
    FluxionEntityChunkView chunk;
    u32 total = 0;

    while (Fluxion_EntityQuery_Next(&query, &chunk)) total += chunk.count;
    return total;
}
