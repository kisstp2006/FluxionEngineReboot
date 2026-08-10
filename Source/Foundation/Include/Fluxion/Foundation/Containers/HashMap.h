#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef u64  (*FluxionHashMapHashFn)(const void* key, usize keySize);
typedef bool (*FluxionHashMapEqualsFn)(const void* keyA, const void* keyB, usize keySize);

// Open-addressing (linear probing) hash map with generic, fixed-size byte
// keys/values. Grows automatically past a 0.7 load factor.
typedef struct FluxionHashMap
{
    FluxionAllocator* allocator;
    u8* keys;
    u8* values;
    u8* occupied; // 1 byte per slot: 0 = empty, 1 = occupied, 2 = tombstone
    usize keySize;
    usize valueSize;
    usize capacity; // number of slots; always a power of two once > 0
    usize count;    // occupied slots (excluding tombstones)
    FluxionHashMapHashFn hash;
    FluxionHashMapEqualsFn equals;
} FluxionHashMap;

void Fluxion_HashMap_Init(FluxionHashMap* map, FluxionAllocator* allocator, usize keySize, usize valueSize, FluxionHashMapHashFn hash, FluxionHashMapEqualsFn equals);
void Fluxion_HashMap_Destroy(FluxionHashMap* map);

// Inserts or overwrites the value for `key`. Returns false only if a
// backing allocation failed.
bool Fluxion_HashMap_Set(FluxionHashMap* map, const void* key, const void* value);

// Returns a pointer to the stored value, or NULL if `key` is not present.
void* Fluxion_HashMap_Find(FluxionHashMap* map, const void* key);

bool Fluxion_HashMap_Remove(FluxionHashMap* map, const void* key);

#ifdef __cplusplus
}
#endif
