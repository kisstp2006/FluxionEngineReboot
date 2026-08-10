#include "TestFramework.h"

#include <Fluxion/Platform/File.h>

#include <string.h>

void Test_File_Run(TestContext* ctx)
{
    const char* path = "fluxion_platform_test_file.tmp";
    const char* content = "Hello, Fluxion Platform!";
    usize contentLength = strlen(content);

    FluxionFile writeFile;
    TEST_CHECK(ctx, Fluxion_Platform_FileOpen(&writeFile, path, FLUXION_FILE_OPEN_WRITE));
    usize written = Fluxion_Platform_FileWrite(&writeFile, content, contentLength);
    TEST_CHECK(ctx, written == contentLength);
    i64 size = Fluxion_Platform_FileSize(&writeFile);
    TEST_CHECK(ctx, size == (i64)contentLength);
    Fluxion_Platform_FileClose(&writeFile);

    TEST_CHECK(ctx, Fluxion_Platform_FileExists(path));

    FluxionFile readFile;
    TEST_CHECK(ctx, Fluxion_Platform_FileOpen(&readFile, path, FLUXION_FILE_OPEN_READ));
    char buffer[128] = { 0 };
    usize readBytes = Fluxion_Platform_FileRead(&readFile, buffer, sizeof(buffer) - 1);
    TEST_CHECK(ctx, readBytes == contentLength);
    TEST_CHECK(ctx, memcmp(buffer, content, contentLength) == 0);
    Fluxion_Platform_FileClose(&readFile);

    const char* moreContent = " More.";
    FluxionFile appendFile;
    TEST_CHECK(ctx, Fluxion_Platform_FileOpen(&appendFile, path, FLUXION_FILE_OPEN_APPEND));
    Fluxion_Platform_FileWrite(&appendFile, moreContent, strlen(moreContent));
    i64 appendedSize = Fluxion_Platform_FileSize(&appendFile);
    TEST_CHECK(ctx, appendedSize == (i64)(contentLength + strlen(moreContent)));
    Fluxion_Platform_FileClose(&appendFile);

    TEST_CHECK(ctx, Fluxion_Platform_FileDelete(path));
    TEST_CHECK(ctx, !Fluxion_Platform_FileExists(path));
}
