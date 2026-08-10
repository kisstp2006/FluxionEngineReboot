#include "TestFramework.h"

#include <Fluxion/Foundation/Atomic.h>

void Test_Atomic_Run(TestContext* ctx)
{
    FluxionAtomicI32 counter = { 0 };
    Fluxion_AtomicI32_Store(&counter, 10);
    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&counter) == 10);

    TEST_CHECK(ctx, Fluxion_AtomicI32_Increment(&counter) == 11);
    TEST_CHECK(ctx, Fluxion_AtomicI32_Decrement(&counter) == 10);

    i32 expected = 10;
    TEST_CHECK(ctx, Fluxion_AtomicI32_CompareExchange(&counter, &expected, 99) == true);
    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&counter) == 99);

    expected = 10; // stale — the CAS below must fail and write back the actual value
    TEST_CHECK(ctx, Fluxion_AtomicI32_CompareExchange(&counter, &expected, 5) == false);
    TEST_CHECK(ctx, expected == 99);

    FluxionAtomicI64 counter64 = { 0 };
    Fluxion_AtomicI64_Store(&counter64, 1000000000000ll);
    TEST_CHECK(ctx, Fluxion_AtomicI64_Load(&counter64) == 1000000000000ll);
    TEST_CHECK(ctx, Fluxion_AtomicI64_Increment(&counter64) == 1000000000001ll);
}
