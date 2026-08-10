#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Virtual memory reservation, separate from Foundation's FluxionAllocator
// (which sits on top of the CRT heap). Intended for large, page-granular
// reservations — e.g. a future arena that grows without moving.
void* Fluxion_Platform_ReserveVirtualMemory(usize size);
bool  Fluxion_Platform_CommitVirtualMemory(void* address, usize size);
bool  Fluxion_Platform_DecommitVirtualMemory(void* address, usize size);
void  Fluxion_Platform_ReleaseVirtualMemory(void* address, usize size);

usize Fluxion_Platform_GetPageSize(void);

#ifdef __cplusplus
}
#endif
