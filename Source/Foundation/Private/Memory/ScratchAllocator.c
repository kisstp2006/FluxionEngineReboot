#include <Fluxion/Foundation/Memory/ScratchAllocator.h>

void Fluxion_ScratchAllocator_Init(FluxionScratchAllocator* scratch, FluxionAllocator* backingAllocator, usize capacity)
{
    Fluxion_Arena_Init(scratch, backingAllocator, capacity);
}

void Fluxion_ScratchAllocator_Destroy(FluxionScratchAllocator* scratch)
{
    Fluxion_Arena_Destroy(scratch);
}

void* Fluxion_ScratchAllocator_Alloc(FluxionScratchAllocator* scratch, usize size, usize alignment)
{
    return Fluxion_Arena_Alloc(scratch, size, alignment);
}

void Fluxion_ScratchAllocator_Reset(FluxionScratchAllocator* scratch)
{
    Fluxion_Arena_Reset(scratch);
}
