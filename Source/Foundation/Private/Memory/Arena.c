#include <Fluxion/Foundation/Memory/Arena.h>

#include <Fluxion/Foundation/Assert.h>

#include <stddef.h>

static usize Fluxion_AlignUp(usize value, usize alignment)
{
    return (value + (alignment - 1)) & ~(alignment - 1);
}

void Fluxion_Arena_Init(FluxionArena* arena, FluxionAllocator* backingAllocator, usize capacity)
{
    FLUXION_ASSERT(arena != NULL);

    FluxionAllocator* allocator = backingAllocator ? backingAllocator : Fluxion_DefaultAllocator();

    arena->backingAllocator = allocator;
    arena->base = (u8*)Fluxion_Allocator_Alloc(allocator, capacity, FLUXION_DEFAULT_ALIGNMENT);
    arena->capacity = capacity;
    arena->offset = 0;
}

void Fluxion_Arena_Destroy(FluxionArena* arena)
{
    FLUXION_ASSERT(arena != NULL);

    if (arena->base)
    {
        Fluxion_Allocator_Free(arena->backingAllocator, arena->base, arena->capacity);
    }
    arena->base = NULL;
    arena->capacity = 0;
    arena->offset = 0;
}

void* Fluxion_Arena_Alloc(FluxionArena* arena, usize size, usize alignment)
{
    FLUXION_ASSERT(arena != NULL);

    usize alignedOffset = Fluxion_AlignUp(arena->offset, alignment);
    if (alignedOffset + size > arena->capacity)
    {
        return NULL;
    }

    void* pointer = arena->base + alignedOffset;
    arena->offset = alignedOffset + size;
    return pointer;
}

void Fluxion_Arena_Reset(FluxionArena* arena)
{
    FLUXION_ASSERT(arena != NULL);
    arena->offset = 0;
}
