#include <Fluxion/Foundation/Containers/HashMap.h>

#include <Fluxion/Foundation/Assert.h>

#include <stddef.h>
#include <string.h>

#define FLUXION_HASHMAP_EMPTY     0u
#define FLUXION_HASHMAP_OCCUPIED  1u
#define FLUXION_HASHMAP_TOMBSTONE 2u

void Fluxion_HashMap_Init(FluxionHashMap* map, FluxionAllocator* allocator, usize keySize, usize valueSize, FluxionHashMapHashFn hash, FluxionHashMapEqualsFn equals)
{
    FLUXION_ASSERT(map != NULL);
    FLUXION_ASSERT(hash != NULL);
    FLUXION_ASSERT(equals != NULL);

    map->allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    map->keys = NULL;
    map->values = NULL;
    map->occupied = NULL;
    map->keySize = keySize;
    map->valueSize = valueSize;
    map->capacity = 0;
    map->count = 0;
    map->hash = hash;
    map->equals = equals;
}

void Fluxion_HashMap_Destroy(FluxionHashMap* map)
{
    FLUXION_ASSERT(map != NULL);

    if (map->keys)     Fluxion_Allocator_Free(map->allocator, map->keys, map->capacity * map->keySize);
    if (map->values)   Fluxion_Allocator_Free(map->allocator, map->values, map->capacity * map->valueSize);
    if (map->occupied) Fluxion_Allocator_Free(map->allocator, map->occupied, map->capacity * sizeof(u8));

    map->keys = NULL;
    map->values = NULL;
    map->occupied = NULL;
    map->capacity = 0;
    map->count = 0;
}

static usize Fluxion_HashMap_SlotOf(const FluxionHashMap* map, const void* key, bool* outFound)
{
    usize mask = map->capacity - 1;
    usize index = (usize)(map->hash(key, map->keySize) & (u64)mask);
    usize firstTombstone = (usize)-1;

    for (usize probe = 0; probe < map->capacity; ++probe)
    {
        usize slot = (index + probe) & mask;
        u8 state = map->occupied[slot];

        if (state == FLUXION_HASHMAP_EMPTY)
        {
            *outFound = false;
            return (firstTombstone != (usize)-1) ? firstTombstone : slot;
        }

        if (state == FLUXION_HASHMAP_TOMBSTONE)
        {
            if (firstTombstone == (usize)-1)
            {
                firstTombstone = slot;
            }
            continue;
        }

        const void* storedKey = map->keys + slot * map->keySize;
        if (map->equals(storedKey, key, map->keySize))
        {
            *outFound = true;
            return slot;
        }
    }

    *outFound = false;
    return firstTombstone;
}

static bool Fluxion_HashMap_Grow(FluxionHashMap* map, usize newCapacity)
{
    FluxionHashMap newMap = *map;
    newMap.capacity = newCapacity;
    newMap.count = 0;
    newMap.keys = (u8*)Fluxion_Allocator_Alloc(map->allocator, newCapacity * map->keySize, alignof(max_align_t));
    newMap.values = (u8*)Fluxion_Allocator_Alloc(map->allocator, newCapacity * map->valueSize, alignof(max_align_t));
    newMap.occupied = (u8*)Fluxion_Allocator_Alloc(map->allocator, newCapacity * sizeof(u8), alignof(max_align_t));

    if (!newMap.keys || !newMap.values || !newMap.occupied)
    {
        if (newMap.keys)     Fluxion_Allocator_Free(map->allocator, newMap.keys, newCapacity * map->keySize);
        if (newMap.values)   Fluxion_Allocator_Free(map->allocator, newMap.values, newCapacity * map->valueSize);
        if (newMap.occupied) Fluxion_Allocator_Free(map->allocator, newMap.occupied, newCapacity * sizeof(u8));
        return false;
    }

    memset(newMap.occupied, FLUXION_HASHMAP_EMPTY, newCapacity * sizeof(u8));

    for (usize i = 0; i < map->capacity; ++i)
    {
        if (map->occupied[i] == FLUXION_HASHMAP_OCCUPIED)
        {
            bool found = false;
            const void* existingKey = map->keys + i * map->keySize;
            usize slot = Fluxion_HashMap_SlotOf(&newMap, existingKey, &found);

            memcpy(newMap.keys + slot * newMap.keySize, existingKey, map->keySize);
            memcpy(newMap.values + slot * newMap.valueSize, map->values + i * map->valueSize, map->valueSize);
            newMap.occupied[slot] = FLUXION_HASHMAP_OCCUPIED;
            newMap.count += 1;
        }
    }

    if (map->keys)     Fluxion_Allocator_Free(map->allocator, map->keys, map->capacity * map->keySize);
    if (map->values)   Fluxion_Allocator_Free(map->allocator, map->values, map->capacity * map->valueSize);
    if (map->occupied) Fluxion_Allocator_Free(map->allocator, map->occupied, map->capacity * sizeof(u8));

    *map = newMap;
    return true;
}

bool Fluxion_HashMap_Set(FluxionHashMap* map, const void* key, const void* value)
{
    FLUXION_ASSERT(map != NULL);

    if (map->capacity == 0)
    {
        if (!Fluxion_HashMap_Grow(map, 8))
        {
            return false;
        }
    }
    else if ((map->count + 1) * 10 >= map->capacity * 7) // load factor 0.7
    {
        if (!Fluxion_HashMap_Grow(map, map->capacity * 2))
        {
            return false;
        }
    }

    bool found = false;
    usize slot = Fluxion_HashMap_SlotOf(map, key, &found);

    memcpy(map->keys + slot * map->keySize, key, map->keySize);
    memcpy(map->values + slot * map->valueSize, value, map->valueSize);

    if (!found)
    {
        map->occupied[slot] = FLUXION_HASHMAP_OCCUPIED;
        map->count += 1;
    }

    return true;
}

void* Fluxion_HashMap_Find(FluxionHashMap* map, const void* key)
{
    FLUXION_ASSERT(map != NULL);

    if (map->capacity == 0)
    {
        return NULL;
    }

    bool found = false;
    usize slot = Fluxion_HashMap_SlotOf(map, key, &found);
    return found ? (map->values + slot * map->valueSize) : NULL;
}

bool Fluxion_HashMap_Remove(FluxionHashMap* map, const void* key)
{
    FLUXION_ASSERT(map != NULL);

    if (map->capacity == 0)
    {
        return false;
    }

    bool found = false;
    usize slot = Fluxion_HashMap_SlotOf(map, key, &found);
    if (!found)
    {
        return false;
    }

    map->occupied[slot] = FLUXION_HASHMAP_TOMBSTONE;
    map->count -= 1;
    return true;
}
