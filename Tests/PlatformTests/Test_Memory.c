#include "TestFramework.h"

#include <Fluxion/Platform/Memory.h>

void Test_Memory_Run(TestContext* ctx)
{
    usize pageSize = Fluxion_Platform_GetPageSize();
    TEST_CHECK(ctx, pageSize > 0);

    usize size = pageSize * 4;
    void* address = Fluxion_Platform_ReserveVirtualMemory(size);
    TEST_CHECK(ctx, address != NULL);
    if (!address)
    {
        return;
    }

    TEST_CHECK(ctx, Fluxion_Platform_CommitVirtualMemory(address, size));

    // Touch the committed memory to prove it's actually usable, not just
    // a non-NULL pointer.
    u8* bytes = (u8*)address;
    bytes[0] = 0xAB;
    bytes[size - 1] = 0xCD;
    TEST_CHECK(ctx, bytes[0] == 0xAB && bytes[size - 1] == 0xCD);

    TEST_CHECK(ctx, Fluxion_Platform_DecommitVirtualMemory(address, size));
    Fluxion_Platform_ReleaseVirtualMemory(address, size);
}
