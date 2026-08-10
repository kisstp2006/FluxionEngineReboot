#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// FNV-1a. Not cryptographic — for hash maps, asset name hashing, etc.
u32 Fluxion_HashBytes32(const void* data, usize length);
u64 Fluxion_HashBytes64(const void* data, usize length);

u32 Fluxion_HashString32(const char* str);
u64 Fluxion_HashString64(const char* str);

// Byte-wise equality helper, usable directly as a FluxionHashMapEqualsFn
// for fixed-size keys.
bool Fluxion_BytesEqual(const void* a, const void* b, usize size);

#ifdef __cplusplus
}
#endif
