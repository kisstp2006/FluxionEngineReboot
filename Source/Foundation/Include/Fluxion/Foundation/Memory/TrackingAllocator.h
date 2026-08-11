#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fills *out with a FluxionAllocator whose alloc/realloc/free forward to
// *inner, recording each call against whatever domain is on top of
// Fluxion_MemoryTracker's scope stack at call time (MemoryTracker.h,
// MemoryScope.hpp) -- a decorator over the existing allocator interface,
// not a new allocator implementation. *inner must outlive every use of
// *out.
void Fluxion_TrackingAllocator_Init(FluxionAllocator* out, FluxionAllocator* inner);

#ifdef __cplusplus
}
#endif
