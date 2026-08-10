#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Linear/bump allocator over a single fixed-size block. Alloc never frees
// individual allocations — call Reset to reclaim everything at once.
typedef struct FluxionArena
{
    FluxionAllocator* backingAllocator;
    u8* base;
    usize capacity;
    usize offset;
} FluxionArena;

void  Fluxion_Arena_Init(FluxionArena* arena, FluxionAllocator* backingAllocator, usize capacity);
void  Fluxion_Arena_Destroy(FluxionArena* arena);

// Returns NULL if the arena does not have `size` bytes left after aligning.
void* Fluxion_Arena_Alloc(FluxionArena* arena, usize size, usize alignment);

void  Fluxion_Arena_Reset(FluxionArena* arena);

#ifdef __cplusplus
}
#endif
