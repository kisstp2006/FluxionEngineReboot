#include <Fluxion/Foundation/Memory/TrackingAllocator.h>

#include <Fluxion/Foundation/Memory/MemoryTracker.h>

static void* Fluxion_TrackingAllocator_Alloc(FluxionAllocator* self, usize size, usize alignment)
{
    FluxionAllocator* inner = (FluxionAllocator*)self->userData;
    void* block = Fluxion_Allocator_Alloc(inner, size, alignment);
    if (block)
    {
        Fluxion_MemoryTracker_RecordAlloc(Fluxion_MemoryTracker_GetCurrentDomain(), size);
    }
    return block;
}

static void* Fluxion_TrackingAllocator_Realloc(FluxionAllocator* self, void* block, usize oldSize, usize newSize, usize alignment)
{
    FluxionAllocator* inner = (FluxionAllocator*)self->userData;
    void* newBlock = Fluxion_Allocator_Realloc(inner, block, oldSize, newSize, alignment);
    if (newBlock)
    {
        FluxionMemoryDomainId domain = Fluxion_MemoryTracker_GetCurrentDomain();
        if (block)
        {
            // A real resize, not a fresh allocation via realloc(NULL, ...)
            // semantics -- only then was oldSize actually tracked before.
            Fluxion_MemoryTracker_RecordFree(domain, oldSize);
        }
        Fluxion_MemoryTracker_RecordAlloc(domain, newSize);
    }
    return newBlock;
}

static void Fluxion_TrackingAllocator_Free(FluxionAllocator* self, void* block, usize size)
{
    FluxionAllocator* inner = (FluxionAllocator*)self->userData;
    Fluxion_Allocator_Free(inner, block, size);
    Fluxion_MemoryTracker_RecordFree(Fluxion_MemoryTracker_GetCurrentDomain(), size);
}

void Fluxion_TrackingAllocator_Init(FluxionAllocator* out, FluxionAllocator* inner)
{
    out->alloc = Fluxion_TrackingAllocator_Alloc;
    out->realloc = Fluxion_TrackingAllocator_Realloc;
    out->free = Fluxion_TrackingAllocator_Free;
    out->userData = inner;
}
