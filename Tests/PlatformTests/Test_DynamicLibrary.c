#include "TestFramework.h"

#include <Fluxion/Platform/DynamicLibrary.h>

// FLUXION_TEST_PLUGIN_PATH is injected by CMake via
// $<TARGET_FILE:PlatformTestPlugin> — see Tests/PlatformTests/CMakeLists.txt.
typedef int (*TestPluginMagicNumberFn)(void);

void Test_DynamicLibrary_Run(TestContext* ctx)
{
    FluxionDynamicLibrary library;
    TEST_CHECK(ctx, Fluxion_Platform_LoadDynamicLibrary(&library, FLUXION_TEST_PLUGIN_PATH));

    FluxionSymbolAddress symbol = Fluxion_Platform_GetSymbol(&library, "TestPlugin_GetMagicNumber");
    TEST_CHECK(ctx, symbol != NULL);

    if (symbol)
    {
        TestPluginMagicNumberFn getMagicNumber = (TestPluginMagicNumberFn)symbol;
        TEST_CHECK(ctx, getMagicNumber() == 424242);
    }

    Fluxion_Platform_UnloadDynamicLibrary(&library);

    FluxionDynamicLibrary missingLibrary;
    TEST_CHECK(ctx, Fluxion_Platform_LoadDynamicLibrary(&missingLibrary, "this_file_does_not_exist.xyz") == false);
}
