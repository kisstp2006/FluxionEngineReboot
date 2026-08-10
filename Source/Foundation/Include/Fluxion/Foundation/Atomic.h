#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Thin, C-ABI atomic wrappers usable identically from C and C++. Do not
// touch `value` directly — always go through the functions below, which
// perform genuine atomic operations in Atomic.c.
typedef struct FluxionAtomicI32 { i32 value; } FluxionAtomicI32;
typedef struct FluxionAtomicI64 { i64 value; } FluxionAtomicI64;

void Fluxion_AtomicI32_Store(FluxionAtomicI32* atomic, i32 value);
i32  Fluxion_AtomicI32_Load(const FluxionAtomicI32* atomic);
i32  Fluxion_AtomicI32_Increment(FluxionAtomicI32* atomic);
i32  Fluxion_AtomicI32_Decrement(FluxionAtomicI32* atomic);
bool Fluxion_AtomicI32_CompareExchange(FluxionAtomicI32* atomic, i32* expected, i32 desired);

void Fluxion_AtomicI64_Store(FluxionAtomicI64* atomic, i64 value);
i64  Fluxion_AtomicI64_Load(const FluxionAtomicI64* atomic);
i64  Fluxion_AtomicI64_Increment(FluxionAtomicI64* atomic);
i64  Fluxion_AtomicI64_Decrement(FluxionAtomicI64* atomic);
bool Fluxion_AtomicI64_CompareExchange(FluxionAtomicI64* atomic, i64* expected, i64 desired);

#ifdef __cplusplus
}
#endif
