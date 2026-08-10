#include "TestFramework.h"

#include <Fluxion/Platform/Environment.h>

#include <string.h>

void Test_Environment_Run(TestContext* ctx)
{
    char buffer[1024];

    TEST_CHECK(ctx, Fluxion_Platform_GetExecutablePath(buffer, sizeof(buffer)));
    TEST_CHECK(ctx, strlen(buffer) > 0);

    TEST_CHECK(ctx, Fluxion_Platform_GetCurrentWorkingDirectory(buffer, sizeof(buffer)));
    TEST_CHECK(ctx, strlen(buffer) > 0);

    // FLUXION_TEST_ENV_VAR is injected by CTest (see the ENVIRONMENT test
    // property in Tests/PlatformTests/CMakeLists.txt) so this check is
    // deterministic — unlike PATH, whose length varies wildly by machine
    // (e.g. 1425 bytes on this one, comfortably overflowing a "should be
    // plenty" buffer on a dev box with lots of SDKs installed).
    TEST_CHECK(ctx, Fluxion_Platform_GetEnvironmentVariable("FLUXION_TEST_ENV_VAR", buffer, sizeof(buffer)));
    TEST_CHECK(ctx, strcmp(buffer, "FluxionValue123") == 0);

    char tinyBuffer[1];
    TEST_CHECK(ctx, Fluxion_Platform_GetEnvironmentVariable("FLUXION_TEST_ENV_VAR", tinyBuffer, sizeof(tinyBuffer)) == false);

    TEST_CHECK(ctx, Fluxion_Platform_GetEnvironmentVariable("FLUXION_DOES_NOT_EXIST_VAR", buffer, sizeof(buffer)) == false);
}
