#include <Fluxion/Foundation/Memory/Allocator.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Defines.h>

#include <stdlib.h>
#include <string.h>

void* Fluxion_Allocator_Alloc(FluxionAllocator* allocator, usize size, usize alignment)
{
    FLUXION_ASSERT(allocator != NULL);
    FLUXION_ASSERT(allocator->alloc != NULL);
    return allocator->alloc(allocator, size, alignment);
}

void* Fluxion_Allocator_Realloc(FluxionAllocator* allocator, void* block, usize oldSize, usize newSize, usize alignment)
{
    FLUXION_ASSERT(allocator != NULL);
    FLUXION_ASSERT(allocator->realloc != NULL);
    return allocator->realloc(allocator, block, oldSize, newSize, alignment);
}

void Fluxion_Allocator_Free(FluxionAllocator* allocator, void* block, usize size)
{
    FLUXION_ASSERT(allocator != NULL);
    FLUXION_ASSERT(allocator->free != NULL);
    allocator->free(allocator, block, size);
}

static usize Fluxion_EffectiveAlignment(usize alignment)
{
    return alignment > sizeof(void*) ? alignment : sizeof(void*);
}

static void* Fluxion_DefaultAlloc(FluxionAllocator* self, usize size, usize alignment)
{
    FLUXION_UNUSED(self);
    usize effectiveAlignment = Fluxion_EffectiveAlignment(alignment);
#if FLUXION_PLATFORM_WINDOWS
    return _aligned_malloc(size, effectiveAlignment);
#else
    // posix_memalign rather than aligned_alloc: the latter is missing
    // from Android's C library below API level 28, and where it does
    // exist under C11 rules it additionally requires the size to be a
    // whole number of alignments -- a rounding step this had to do by
    // hand, and one that quietly asks the allocator for more than the
    // caller wanted. posix_memalign has neither problem and has been in
    // every POSIX C library, Android's included, from the start. Blocks
    // from it are released with plain free(), which is what the matching
    // path below already does.
    {
        void* block = NULL;
        if (posix_memalign(&block, effectiveAlignment, size) != 0) return NULL;
        return block;
    }
#endif
}

static void* Fluxion_DefaultRealloc(FluxionAllocator* self, void* block, usize oldSize, usize newSize, usize alignment)
{
    FLUXION_UNUSED(self);
#if FLUXION_PLATFORM_WINDOWS
    FLUXION_UNUSED(oldSize);
    return _aligned_realloc(block, newSize, Fluxion_EffectiveAlignment(alignment));
#else
    // C11/C23 has no aligned realloc: allocate, copy, free.
    void* newBlock = Fluxion_DefaultAlloc(self, newSize, alignment);
    if (newBlock && block)
    {
        memcpy(newBlock, block, oldSize < newSize ? oldSize : newSize);
    }
    if (block)
    {
        free(block);
    }
    return newBlock;
#endif
}

static void Fluxion_DefaultFree(FluxionAllocator* self, void* block, usize size)
{
    FLUXION_UNUSED(self);
    FLUXION_UNUSED(size);
#if FLUXION_PLATFORM_WINDOWS
    _aligned_free(block);
#else
    free(block);
#endif
}

static FluxionAllocator s_defaultAllocator =
{
    .alloc = Fluxion_DefaultAlloc,
    .realloc = Fluxion_DefaultRealloc,
    .free = Fluxion_DefaultFree,
    .userData = NULL,
};

FluxionAllocator* Fluxion_DefaultAllocator(void)
{
    return &s_defaultAllocator;
}
