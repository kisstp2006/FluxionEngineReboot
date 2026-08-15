#include "AssetTestSupport.h"
#include "TestFramework.h"

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <cstring>
#include <vector>

using namespace AssetTestSupport;

namespace
{

FluxionAssetDesc MakeDesc(const char* name, const char* sourcePath, const char* cookedPath)
{
    FluxionAssetDesc desc{};
    desc.type = BlobTypeId();
    desc.name = name;
    desc.sourcePath = sourcePath;
    desc.cookedPath = cookedPath;
    desc.version = 1;
    return desc;
}

void AddFindRemove(TestContext& ctx)
{
    Fluxion_AssetDatabase_Init(nullptr);

    FluxionUUID first{};
    FluxionUUID second{};
    FluxionAssetDesc a = MakeDesc("First", "assets://Src/a.txt", "assets://Cooked/a.blob");
    FluxionAssetDesc b = MakeDesc("Second", nullptr, "assets://Cooked/b.blob");

    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&a, &first));
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&b, &second));
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == 2);
    TEST_CHECK(ctx, !Fluxion_UUID_Equals(first, second));

    const FluxionAssetRecord* recordA = Fluxion_AssetDatabase_Find(first);
    TEST_CHECK(ctx, recordA != nullptr);
    if (recordA)
    {
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetName(recordA), "First") == 0);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetSourcePath(recordA), "assets://Src/a.txt") == 0);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetCookedPath(recordA), "assets://Cooked/a.blob") == 0);
    }

    // Offset zero is a real offset into the text, and it is the empty
    // string -- so "this asset has no source" needs no separate way of
    // being said, and cannot be forgotten at one of the places that set
    // it.
    const FluxionAssetRecord* recordB = Fluxion_AssetDatabase_Find(second);
    TEST_CHECK(ctx, recordB != nullptr);
    if (recordB)
    {
        TEST_CHECK(ctx, recordB->sourcePathOffset == 0);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetSourcePath(recordB), "") == 0);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetCookedPath(recordB), "assets://Cooked/b.blob") == 0);
    }

    // An id given twice is refused; nothing is quietly replaced.
    FluxionAssetDesc duplicate = MakeDesc("Again", nullptr, "assets://Cooked/c.blob");
    duplicate.id = first;
    TEST_CHECK(ctx, !Fluxion_AssetDatabase_Add(&duplicate, nullptr));
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == 2);

    // Removing moves the last record into the freed slot. Asked BOTH
    // ways round: the one that is gone must be gone, and the one that
    // moved must still be found by its own id -- the second is the half
    // a lookup of the removed asset would never notice was missing.
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Remove(first));
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Find(first) == nullptr);
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == 1);

    const FluxionAssetRecord* movedB = Fluxion_AssetDatabase_Find(second);
    TEST_CHECK(ctx, movedB != nullptr);
    if (movedB)
    {
        TEST_CHECK(ctx, Fluxion_UUID_Equals(movedB->id, second));
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetName(movedB), "Second") == 0);
        TEST_CHECK(ctx, movedB == Fluxion_AssetDatabase_GetAt(0));
    }

    TEST_CHECK(ctx, !Fluxion_AssetDatabase_Remove(first));

    Fluxion_AssetDatabase_Shutdown();
}

void Dependencies(TestContext& ctx)
{
    Fluxion_AssetDatabase_Init(nullptr);

    FluxionUUID leafA{};
    FluxionUUID leafB{};
    FluxionAssetDesc a = MakeDesc("LeafA", nullptr, "assets://a.blob");
    FluxionAssetDesc b = MakeDesc("LeafB", nullptr, "assets://b.blob");
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&a, &leafA));
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&b, &leafB));

    const FluxionUUID needed[] = { leafA, leafB };
    FluxionAssetDesc parent = MakeDesc("Parent", nullptr, "assets://parent.blob");
    parent.dependencies = needed;
    parent.dependencyCount = 2;

    FluxionUUID parentId{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&parent, &parentId));

    const FluxionAssetRecord* record = Fluxion_AssetDatabase_Find(parentId);
    TEST_CHECK(ctx, record != nullptr);
    if (record)
    {
        u32 count = 0;
        const FluxionUUID* got = Fluxion_AssetDatabase_GetDependencies(record, &count);
        TEST_CHECK(ctx, count == 2);
        TEST_CHECK(ctx, got != nullptr);
        if (got && count == 2)
        {
            TEST_CHECK(ctx, Fluxion_UUID_Equals(got[0], leafA));
            TEST_CHECK(ctx, Fluxion_UUID_Equals(got[1], leafB));
        }
    }

    const FluxionAssetRecord* leaf = Fluxion_AssetDatabase_Find(leafA);
    u32 leafCount = 1;
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetDependencies(leaf, &leafCount) == nullptr);
    TEST_CHECK(ctx, leafCount == 0);

    Fluxion_AssetDatabase_Shutdown();
}

// Written, read back, and written again. The second bytes have to match
// the first exactly -- a check that catches a field landing in the wrong
// place even when every value read back looked right.
void RoundTripIsByteIdentical(TestContext& ctx)
{
    Fluxion_AssetDatabase_Init(nullptr);

    FluxionUUID leaf{};
    FluxionAssetDesc a = MakeDesc("Leaf", "assets://Src/leaf.tsrc", "assets://Cooked/leaf.blob");
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&a, &leaf));

    const FluxionUUID needed[] = { leaf };
    FluxionAssetDesc parent = MakeDesc("Parent", "assets://Src/parent.tsrc", "assets://Cooked/parent.blob");
    parent.dependencies = needed;
    parent.dependencyCount = 1;
    parent.version = 7;
    FluxionUUID parentId{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&parent, &parentId));

    std::vector<u8> first(64 * 1024, 0);
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, first.data(), first.size());
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Serialize(&writer));
    const usize firstSize = Fluxion_Stream_GetPosition(&writer);

    FluxionStream reader;
    Fluxion_MemoryStream_InitReader(&reader, first.data(), firstSize);
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Serialize(&reader));

    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == 2);

    const FluxionAssetRecord* readParent = Fluxion_AssetDatabase_Find(parentId);
    TEST_CHECK(ctx, readParent != nullptr);
    if (readParent)
    {
        TEST_CHECK(ctx, readParent->version == 7);
        TEST_CHECK(ctx, readParent->type == BlobTypeId());
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetSourcePath(readParent), "assets://Src/parent.tsrc") == 0);

        u32 count = 0;
        const FluxionUUID* got = Fluxion_AssetDatabase_GetDependencies(readParent, &count);
        TEST_CHECK(ctx, count == 1);
        if (got && count == 1) TEST_CHECK(ctx, Fluxion_UUID_Equals(got[0], leaf));
    }

    std::vector<u8> second(64 * 1024, 0);
    FluxionStream rewriter;
    Fluxion_MemoryStream_InitWriter(&rewriter, second.data(), second.size());
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Serialize(&rewriter));
    const usize secondSize = Fluxion_Stream_GetPosition(&rewriter);

    TEST_CHECK(ctx, secondSize == firstSize);
    TEST_CHECK(ctx, std::memcmp(first.data(), second.data(), firstSize) == 0);

    Fluxion_AssetDatabase_Shutdown();
}

void RefusesANewerFormat(TestContext& ctx)
{
    Fluxion_AssetDatabase_Init(nullptr);

    FluxionAssetDesc a = MakeDesc("One", nullptr, "assets://a.blob");
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&a, nullptr));

    std::vector<u8> bytes(4096, 0);
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, bytes.data(), bytes.size());
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Serialize(&writer));
    const usize size = Fluxion_Stream_GetPosition(&writer);

    // Raise the version in place. A file from a build that knew things
    // this one does not must be refused, not read as far as it goes and
    // left quietly missing whatever those were.
    bytes[0] = (u8)(FLUXION_ASSET_DATABASE_VERSION + 1);

    FluxionStream reader;
    Fluxion_MemoryStream_InitReader(&reader, bytes.data(), size);
    TEST_CHECK(ctx, !Fluxion_AssetDatabase_Serialize(&reader));

    Fluxion_AssetDatabase_Shutdown();
}

// An asset can be cooked more than once -- one form per kind of hardware
// that wants different bytes. Everything that was cooked once still works
// unchanged, which is most things.
void CookedFormsPerTarget(TestContext& ctx)
{
    Fluxion_AssetDatabase_Init(nullptr);

    // The shorthand: one form, suits every build.
    FluxionUUID simple{};
    FluxionAssetDesc mesh = MakeDesc("Mesh", nullptr, "assets://Cube.fluxmesh");
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&mesh, &simple));

    const FluxionAssetRecord* meshRecord = Fluxion_AssetDatabase_Find(simple);
    TEST_CHECK(ctx, meshRecord != nullptr);
    if (meshRecord)
    {
        TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCookedFormCount(meshRecord) == 1);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetCookedPath(meshRecord), "assets://Cube.fluxmesh") == 0);

        // Asked for a target it has never heard of, it still answers --
        // an asset cooked once for everything must not have to list every
        // target a project happens to ship to.
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetCookedPathForTarget(meshRecord, "mobile"), "assets://Cube.fluxmesh") == 0);
    }

    // Two forms, one per kind of hardware.
    const FluxionAssetCookedForm forms[] = {
        { "desktop", "assets://Brick.desktop.fluxtex" },
        { "mobile", "assets://Brick.mobile.fluxtex" },
    };

    FluxionAssetDesc texture = MakeDesc("Brick", "assets://Brick.png", nullptr);
    texture.cookedForms = forms;
    texture.cookedFormCount = 2;

    FluxionUUID textureId{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&texture, &textureId));

    const FluxionAssetRecord* textureRecord = Fluxion_AssetDatabase_Find(textureId);
    TEST_CHECK(ctx, textureRecord != nullptr);
    if (textureRecord)
    {
        TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCookedFormCount(textureRecord) == 2);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetCookedPathForTarget(textureRecord, "desktop"), "assets://Brick.desktop.fluxtex") == 0);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetCookedPathForTarget(textureRecord, "mobile"), "assets://Brick.mobile.fluxtex") == 0);

        // No general form and no match: it falls back to the first rather
        // than to nothing, because a build with something to draw beats a
        // build with a hole in it.
        const char* unknown = Fluxion_AssetDatabase_GetCookedPathForTarget(textureRecord, "console");
        TEST_CHECK(ctx, unknown[0] != '\0');
    }

    // Saying it twice, two ways, is refused rather than resolved.
    FluxionAssetDesc both = MakeDesc("Both", nullptr, "assets://both.blob");
    both.cookedForms = forms;
    both.cookedFormCount = 2;
    TEST_CHECK(ctx, !Fluxion_AssetDatabase_Add(&both, nullptr));

    // And so is naming one target twice: which one a build took would
    // otherwise depend on the order they were written in.
    const FluxionAssetCookedForm duplicated[] = {
        { "desktop", "assets://one.fluxtex" },
        { "desktop", "assets://two.fluxtex" },
    };
    FluxionAssetDesc twice = MakeDesc("Twice", nullptr, nullptr);
    twice.cookedForms = duplicated;
    twice.cookedFormCount = 2;
    TEST_CHECK(ctx, !Fluxion_AssetDatabase_Add(&twice, nullptr));

    Fluxion_AssetDatabase_Shutdown();
}

// The database stores import settings and hashes them; it does not read
// them. The hash is what makes re-importing decidable without reading the
// bytes again.
void ImportSettingsAreBytesWithAHash(TestContext& ctx)
{
    Fluxion_AssetDatabase_Init(nullptr);

    struct MadeUpSettings
    {
        u32 usage;
        f32 scale;
        bool flipGreen;
    };

    MadeUpSettings settings{ 3u, 0.5f, true };

    FluxionAssetDesc desc = MakeDesc("Settings", "assets://a.png", "assets://a.fluxtex");
    desc.importSettings = &settings;
    desc.importSettingsSize = (u32)sizeof(settings);

    FluxionUUID id{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&desc, &id));

    const FluxionAssetRecord* record = Fluxion_AssetDatabase_Find(id);
    TEST_CHECK(ctx, record != nullptr);
    if (record)
    {
        u32 size = 0;
        const void* stored = Fluxion_AssetDatabase_GetImportSettings(record, &size);
        TEST_CHECK(ctx, stored != nullptr);
        TEST_CHECK(ctx, size == sizeof(settings));
        if (stored && size == sizeof(settings)) TEST_CHECK(ctx, std::memcmp(stored, &settings, size) == 0);

        TEST_CHECK(ctx, record->importSettingsHash != 0);
    }

    // Different settings, different hash -- which is the whole use of it:
    // a re-import compares hashes rather than reading every blob.
    MadeUpSettings other{ 3u, 0.5f, false };
    FluxionAssetDesc second = MakeDesc("Other", "assets://b.png", "assets://b.fluxtex");
    second.importSettings = &other;
    second.importSettingsSize = (u32)sizeof(other);

    FluxionUUID otherId{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&second, &otherId));

    const FluxionAssetRecord* first = Fluxion_AssetDatabase_Find(id);
    const FluxionAssetRecord* secondRecord = Fluxion_AssetDatabase_Find(otherId);
    if (first && secondRecord) TEST_CHECK(ctx, first->importSettingsHash != secondRecord->importSettingsHash);

    // An asset with none says so rather than handing back somebody
    // else's bytes.
    FluxionAssetDesc bare = MakeDesc("Bare", nullptr, "assets://c.blob");
    FluxionUUID bareId{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&bare, &bareId));

    const FluxionAssetRecord* bareRecord = Fluxion_AssetDatabase_Find(bareId);
    if (bareRecord)
    {
        u32 size = 1;
        TEST_CHECK(ctx, Fluxion_AssetDatabase_GetImportSettings(bareRecord, &size) == nullptr);
        TEST_CHECK(ctx, size == 0);
    }

    // Larger than the database holds is refused, not truncated.
    u8 tooBig[FLUXION_ASSET_MAX_IMPORT_SETTINGS_BYTES + 1]{};
    FluxionAssetDesc huge = MakeDesc("Huge", nullptr, "assets://d.blob");
    huge.importSettings = tooBig;
    huge.importSettingsSize = (u32)sizeof(tooBig);
    TEST_CHECK(ctx, !Fluxion_AssetDatabase_Add(&huge, nullptr));

    Fluxion_AssetDatabase_Shutdown();
}

// The round trip has to survive the new fields too, and byte-identically
// -- a cooked form or a settings blob landing at the wrong offset is
// exactly what every individually-checked value can still look right
// through.
void RoundTripCarriesFormsAndSettings(TestContext& ctx)
{
    Fluxion_AssetDatabase_Init(nullptr);

    const FluxionAssetCookedForm forms[] = {
        { "desktop", "assets://Brick.desktop.fluxtex" },
        { "mobile", "assets://Brick.mobile.fluxtex" },
    };
    const u8 settings[] = { 1, 2, 3, 4, 5, 6, 7 };

    FluxionAssetDesc desc = MakeDesc("Brick", "assets://Brick.png", nullptr);
    desc.cookedForms = forms;
    desc.cookedFormCount = 2;
    desc.importSettings = settings;
    desc.importSettingsSize = (u32)sizeof(settings);

    FluxionUUID id{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&desc, &id));

    std::vector<u8> first(64 * 1024, 0);
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, first.data(), first.size());
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Serialize(&writer));
    const usize firstSize = Fluxion_Stream_GetPosition(&writer);

    FluxionStream reader;
    Fluxion_MemoryStream_InitReader(&reader, first.data(), firstSize);
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Serialize(&reader));

    const FluxionAssetRecord* record = Fluxion_AssetDatabase_Find(id);
    TEST_CHECK(ctx, record != nullptr);
    if (record)
    {
        TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCookedFormCount(record) == 2);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetCookedPathForTarget(record, "mobile"), "assets://Brick.mobile.fluxtex") == 0);

        u32 size = 0;
        const void* stored = Fluxion_AssetDatabase_GetImportSettings(record, &size);
        TEST_CHECK(ctx, size == sizeof(settings));
        if (stored && size == sizeof(settings)) TEST_CHECK(ctx, std::memcmp(stored, settings, size) == 0);
    }

    std::vector<u8> second(64 * 1024, 0);
    FluxionStream rewriter;
    Fluxion_MemoryStream_InitWriter(&rewriter, second.data(), second.size());
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Serialize(&rewriter));

    TEST_CHECK(ctx, Fluxion_Stream_GetPosition(&rewriter) == firstSize);
    TEST_CHECK(ctx, std::memcmp(first.data(), second.data(), firstSize) == 0);

    Fluxion_AssetDatabase_Shutdown();
}

} // namespace

void Test_AssetDatabase_Run(TestContext& ctx)
{
    std::fprintf(stderr, "  Test_AssetDatabase\n");

    AddFindRemove(ctx);
    Dependencies(ctx);
    RoundTripIsByteIdentical(ctx);
    RefusesANewerFormat(ctx);
    CookedFormsPerTarget(ctx);
    ImportSettingsAreBytesWithAHash(ctx);
    RoundTripCarriesFormsAndSettings(ctx);
}
