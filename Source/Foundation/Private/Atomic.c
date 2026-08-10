#include <Fluxion/Foundation/Atomic.h>

#include <stdatomic.h>

void Fluxion_AtomicI32_Store(FluxionAtomicI32* atomic, i32 value)
{
    atomic_store((_Atomic(i32)*)&atomic->value, value);
}

i32 Fluxion_AtomicI32_Load(const FluxionAtomicI32* atomic)
{
    return atomic_load((const _Atomic(i32)*)&atomic->value);
}

i32 Fluxion_AtomicI32_Increment(FluxionAtomicI32* atomic)
{
    return atomic_fetch_add((_Atomic(i32)*)&atomic->value, 1) + 1;
}

i32 Fluxion_AtomicI32_Decrement(FluxionAtomicI32* atomic)
{
    return atomic_fetch_sub((_Atomic(i32)*)&atomic->value, 1) - 1;
}

bool Fluxion_AtomicI32_CompareExchange(FluxionAtomicI32* atomic, i32* expected, i32 desired)
{
    return atomic_compare_exchange_strong((_Atomic(i32)*)&atomic->value, expected, desired);
}

void Fluxion_AtomicI64_Store(FluxionAtomicI64* atomic, i64 value)
{
    atomic_store((_Atomic(i64)*)&atomic->value, value);
}

i64 Fluxion_AtomicI64_Load(const FluxionAtomicI64* atomic)
{
    return atomic_load((const _Atomic(i64)*)&atomic->value);
}

i64 Fluxion_AtomicI64_Increment(FluxionAtomicI64* atomic)
{
    return atomic_fetch_add((_Atomic(i64)*)&atomic->value, 1) + 1;
}

i64 Fluxion_AtomicI64_Decrement(FluxionAtomicI64* atomic)
{
    return atomic_fetch_sub((_Atomic(i64)*)&atomic->value, 1) - 1;
}

bool Fluxion_AtomicI64_CompareExchange(FluxionAtomicI64* atomic, i64* expected, i64 desired)
{
    return atomic_compare_exchange_strong((_Atomic(i64)*)&atomic->value, expected, desired);
}
