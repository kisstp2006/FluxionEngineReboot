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

} // namespace

void Test_AssetDatabase_Run(TestContext& ctx)
{
    std::fprintf(stderr, "  Test_AssetDatabase\n");

    AddFindRemove(ctx);
    Dependencies(ctx);
    RoundTripIsByteIdentical(ctx);
    RefusesANewerFormat(ctx);
}
