#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionAllocator FluxionAllocator;

typedef void* (*FluxionAllocatorAllocFn)(FluxionAllocator* self, usize size, usize alignment);
typedef void* (*FluxionAllocatorReallocFn)(FluxionAllocator* self, void* block, usize oldSize, usize newSize, usize alignment);
typedef void  (*FluxionAllocatorFreeFn)(FluxionAllocator* self, void* block, usize size);

struct FluxionAllocator
{
    FluxionAllocatorAllocFn   alloc;
    FluxionAllocatorReallocFn realloc;
    FluxionAllocatorFreeFn    free;
    void* userData;
};

void* Fluxion_Allocator_Alloc(FluxionAllocator* allocator, usize size, usize alignment);
void* Fluxion_Allocator_Realloc(FluxionAllocator* allocator, void* block, usize oldSize, usize newSize, usize alignment);
void  Fluxion_Allocator_Free(FluxionAllocator* allocator, void* block, usize size);

// Process-wide default allocator, backed by the CRT allocator
// (_aligned_malloc on Windows, aligned_alloc elsewhere).
FluxionAllocator* Fluxion_DefaultAllocator(void);

#ifdef __cplusplus
}
#endif
