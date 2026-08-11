#include "TestFramework.h"

#include <Fluxion/Application/Window/Clipboard.h>

#include <string.h>

void Test_Clipboard_Run(TestContext* ctx)
{
    const char* text = "Fluxion clipboard round-trip";
    TEST_CHECK(ctx, Fluxion_Clipboard_SetText(text));

    char buffer[256];
    TEST_CHECK(ctx, Fluxion_Clipboard_GetText(buffer, sizeof(buffer)));
    TEST_CHECK(ctx, strcmp(buffer, text) == 0);

    char tinyBuffer[4];
    TEST_CHECK(ctx, Fluxion_Clipboard_GetText(tinyBuffer, sizeof(tinyBuffer)) == false);
}
