#pragma once

#include <Fluxion/Foundation/Memory/Arena.h>

#ifdef __cplusplus
extern "C" {
#endif

// A scratch allocator is an arena intended to be reset once per frame (or
// once per well-defined scope) rather than being torn down and rebuilt.
// Kept as a distinct name/API from FluxionArena per the architecture plan
// (Foundation section: Arena, Linear Allocator, and Scratch Allocator are
// listed as separate concepts even though scratch reuses arena mechanics).
typedef FluxionArena FluxionScratchAllocator;

void  Fluxion_ScratchAllocator_Init(FluxionScratchAllocator* scratch, FluxionAllocator* backingAllocator, usize capacity);
void  Fluxion_ScratchAllocator_Destroy(FluxionScratchAllocator* scratch);
void* Fluxion_ScratchAllocator_Alloc(FluxionScratchAllocator* scratch, usize size, usize alignment);
void  Fluxion_ScratchAllocator_Reset(FluxionScratchAllocator* scratch);

#ifdef __cplusplus
}
#endif
