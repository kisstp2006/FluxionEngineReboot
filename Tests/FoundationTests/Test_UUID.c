#include "TestFramework.h"

#include <Fluxion/Foundation/UUID.h>

#include <string.h>

void Test_UUID_Run(TestContext* ctx)
{
    FluxionUUID a = Fluxion_UUID_Generate();
    FluxionUUID b = Fluxion_UUID_Generate();

    TEST_CHECK(ctx, !Fluxion_UUID_IsNil(a));
    TEST_CHECK(ctx, Fluxion_UUID_Equals(a, a));
    TEST_CHECK(ctx, !Fluxion_UUID_Equals(a, b));

    // RFC 4122 version 4 / variant bits.
    TEST_CHECK(ctx, (a.bytes[6] & 0xF0u) == 0x40u);
    TEST_CHECK(ctx, (a.bytes[8] & 0xC0u) == 0x80u);

    char buffer[37];
    Fluxion_UUID_ToString(a, buffer);
    TEST_CHECK(ctx, strlen(buffer) == 36);
    TEST_CHECK(ctx, buffer[8] == '-' && buffer[13] == '-' && buffer[18] == '-' && buffer[23] == '-');

    FluxionUUID nilId;
    memset(nilId.bytes, 0, sizeof(nilId.bytes));
    TEST_CHECK(ctx, Fluxion_UUID_IsNil(nilId));
}
