#include "AssetTestSupport.h"
#include "TestFramework.h"

#include <Fluxion/Assets/VirtualFileSystem.h>

#include <cstring>
#include <string>

namespace
{

void SplitPaths(TestContext& ctx)
{
    char scheme[FLUXION_VFS_MAX_SCHEME_LENGTH + 1];
    char relative[FLUXION_VFS_MAX_PATH];

    TEST_CHECK(ctx, Fluxion_Vfs_SplitPath("assets://Meshes/Cube.blob", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, std::strcmp(scheme, "assets") == 0);
    TEST_CHECK(ctx, std::strcmp(relative, "Meshes/Cube.blob") == 0);

    // Every one of these is a way out of the mount. A path that can leave
    // the mount it names makes naming data by scheme meaningless -- so
    // these are refused, not cleaned up and accepted.
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("assets://../secret", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("assets://Meshes/../../secret", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("assets:///absolute", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("assets://C:/drive", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("assets://back\\slash", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("assets://double//separator", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("assets://./here", scheme, sizeof(scheme), relative, sizeof(relative)));

    // Not a scheme at all.
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("Meshes/Cube.blob", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("://nothing", scheme, sizeof(scheme), relative, sizeof(relative)));
    TEST_CHECK(ctx, !Fluxion_Vfs_SplitPath("assets://", scheme, sizeof(scheme), relative, sizeof(relative)));
}

// The escape checks above are about strings. This one is about the file
// system: even given a real file one directory up, a path claiming to
// reach it must come back with nothing.
void EscapesCannotReachRealFiles(TestContext& ctx)
{
    const std::filesystem::path root = AssetTestSupport::MakeDirectory("escape");
    if (root.empty())
    {
        TEST_CHECK(ctx, false);
        return;
    }

    const std::filesystem::path outside = root / "outside.txt";
    const std::filesystem::path inside = root / "project" / "inside.txt";

    TEST_CHECK(ctx, AssetTestSupport::WriteText(outside, "not for you"));
    TEST_CHECK(ctx, AssetTestSupport::WriteText(inside, "yours"));

    Fluxion_Vfs_Init(nullptr);
    TEST_CHECK(ctx, Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create((root / "project").string().c_str())));

    TEST_CHECK(ctx, Fluxion_Vfs_Exists("assets://inside.txt"));
    TEST_CHECK(ctx, !Fluxion_Vfs_Exists("assets://../outside.txt"));

    usize size = 0;
    u8* bytes = Fluxion_Vfs_ReadAll("assets://../outside.txt", &size);
    TEST_CHECK(ctx, bytes == nullptr);
    TEST_CHECK(ctx, size == 0);

    Fluxion_Vfs_Shutdown();
}

void ReadsAndWrites(TestContext& ctx)
{
    const std::filesystem::path root = AssetTestSupport::MakeDirectory("readwrite");
    if (root.empty())
    {
        TEST_CHECK(ctx, false);
        return;
    }

    Fluxion_Vfs_Init(nullptr);
    TEST_CHECK(ctx, Fluxion_Vfs_Mount("user", Fluxion_VfsDirectorySource_Create(root.string().c_str())));

    const char* content = "written through a mount";
    TEST_CHECK(ctx, Fluxion_Vfs_WriteAll("user://Saves/slot0.txt", content, std::strlen(content)));
    TEST_CHECK(ctx, Fluxion_Vfs_Exists("user://Saves/slot0.txt"));

    usize size = 0;
    u8* bytes = Fluxion_Vfs_ReadAll("user://Saves/slot0.txt", &size);
    TEST_CHECK(ctx, bytes != nullptr);
    TEST_CHECK(ctx, size == std::strlen(content));
    if (bytes)
    {
        TEST_CHECK(ctx, std::memcmp(bytes, content, size) == 0);
        Fluxion_Vfs_FreeBuffer(bytes, size);
    }

    // Nothing is mounted here, and the answer is "no" rather than a
    // guess at what the caller meant.
    TEST_CHECK(ctx, !Fluxion_Vfs_Exists("assets://Saves/slot0.txt"));
    TEST_CHECK(ctx, Fluxion_Vfs_ReadAll("assets://Saves/slot0.txt", nullptr) == nullptr);

    // A file with no bytes in it is still a file. It must not come back
    // as NULL, which is how "this source does not have it" is said.
    TEST_CHECK(ctx, Fluxion_Vfs_WriteAll("user://empty.bin", nullptr, 0));
    usize emptySize = 1;
    u8* emptyBytes = Fluxion_Vfs_ReadAll("user://empty.bin", &emptySize);
    TEST_CHECK(ctx, emptyBytes != nullptr);
    TEST_CHECK(ctx, emptySize == 0);
    if (emptyBytes) Fluxion_Vfs_FreeBuffer(emptyBytes, emptySize);

    Fluxion_Vfs_Shutdown();
}

// A patch directory over a base directory. The later mount has to win,
// because that is the only order in which a fix can be shipped on top of
// something already released.
void LaterMountShadowsEarlier(TestContext& ctx)
{
    const std::filesystem::path root = AssetTestSupport::MakeDirectory("shadow");
    if (root.empty())
    {
        TEST_CHECK(ctx, false);
        return;
    }

    TEST_CHECK(ctx, AssetTestSupport::WriteText(root / "base" / "value.txt", "base"));
    TEST_CHECK(ctx, AssetTestSupport::WriteText(root / "base" / "only-in-base.txt", "base only"));
    TEST_CHECK(ctx, AssetTestSupport::WriteText(root / "patch" / "value.txt", "patch"));

    Fluxion_Vfs_Init(nullptr);
    TEST_CHECK(ctx, Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create((root / "base").string().c_str())));
    TEST_CHECK(ctx, Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create((root / "patch").string().c_str())));
    TEST_CHECK(ctx, Fluxion_Vfs_GetSourceCount("assets") == 2);

    usize size = 0;
    u8* bytes = Fluxion_Vfs_ReadAll("assets://value.txt", &size);
    TEST_CHECK(ctx, bytes != nullptr);
    if (bytes)
    {
        TEST_CHECK(ctx, size == 5);
        TEST_CHECK(ctx, std::memcmp(bytes, "patch", size) == 0);
        Fluxion_Vfs_FreeBuffer(bytes, size);
    }

    // What the patch does not replace still comes from underneath it.
    TEST_CHECK(ctx, Fluxion_Vfs_Exists("assets://only-in-base.txt"));

    Fluxion_Vfs_Shutdown();
}

// A mount that fails still takes the source: the caller has no other way
// to destroy one, so anything else leaks every time a mount is refused.
void FailedMountDoesNotLeak(TestContext& ctx)
{
    const std::filesystem::path root = AssetTestSupport::MakeDirectory("mountfail");
    if (root.empty())
    {
        TEST_CHECK(ctx, false);
        return;
    }

    Fluxion_Vfs_Init(nullptr);

    for (u32 i = 0; i < FLUXION_VFS_MAX_SOURCES_PER_MOUNT; ++i)
    {
        TEST_CHECK(ctx, Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create(root.string().c_str())));
    }

    // One too many. What matters is not the false -- it is that the
    // leak checker has nothing to say afterwards.
    TEST_CHECK(ctx, !Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create(root.string().c_str())));
    TEST_CHECK(ctx, !Fluxion_Vfs_Mount("bad scheme", Fluxion_VfsDirectorySource_Create(root.string().c_str())));

    TEST_CHECK(ctx, Fluxion_Vfs_UnmountAll("assets"));
    TEST_CHECK(ctx, Fluxion_Vfs_GetSourceCount("assets") == 0);

    Fluxion_Vfs_Shutdown();
}

} // namespace

void Test_VirtualFileSystem_Run(TestContext& ctx)
{
    std::fprintf(stderr, "  Test_VirtualFileSystem\n");

    SplitPaths(ctx);
    EscapesCannotReachRealFiles(ctx);
    ReadsAndWrites(ctx);
    LaterMountShadowsEarlier(ctx);
    FailedMountDoesNotLeak(ctx);
}
