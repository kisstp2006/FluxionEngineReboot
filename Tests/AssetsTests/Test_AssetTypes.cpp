#include "AssetTestSupport.h"
#include "TestFramework.h"

#include <Fluxion/Assets/AssetType.h>

#include <cstring>

using namespace AssetTestSupport;

namespace
{

void RegisterAndFind(TestContext& ctx)
{
    Fluxion_AssetTypes_Init(nullptr);

    FluxionAssetTypeDesc blob = MakeBlobTypeDesc();
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&blob));
    TEST_CHECK(ctx, Fluxion_AssetTypes_GetCount() == 1);

    const FluxionAssetTypeDesc* found = Fluxion_AssetTypes_Find(BlobTypeId());
    TEST_CHECK(ctx, found != nullptr);
    TEST_CHECK(ctx, Fluxion_AssetTypes_FindByName(ASSET_TEST_BLOB_TYPE_NAME) == found);

    // The id comes from the name and from nothing else, because a build
    // setting names a type by that string and has to go on meaning the
    // same thing after other types are registered around it.
    FluxionAssetTypeDesc model = MakeModelTypeDesc(true);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&model));
    TEST_CHECK(ctx, Fluxion_AssetTypes_FindByName(ASSET_TEST_BLOB_TYPE_NAME) != nullptr);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Find(BlobTypeId()) != nullptr);
    TEST_CHECK(ctx, BlobTypeId() != ModelTypeId());

    // The descriptor is copied, not held by pointer -- so the caller's
    // copy going away, or being changed, changes nothing here.
    std::memset(&blob, 0, sizeof(blob));
    const FluxionAssetTypeDesc* afterClobber = Fluxion_AssetTypes_Find(BlobTypeId());
    TEST_CHECK(ctx, afterClobber != nullptr);
    if (afterClobber)
    {
        TEST_CHECK(ctx, std::strcmp(afterClobber->name, ASSET_TEST_BLOB_TYPE_NAME) == 0);
        TEST_CHECK(ctx, afterClobber->load == TestBlob_Load);
    }

    TEST_CHECK(ctx, !Fluxion_AssetTypes_Register(&model)); // already there

    Fluxion_AssetTypes_Shutdown();
}

void RefusesTypesNothingCanLoad(TestContext& ctx)
{
    Fluxion_AssetTypes_Init(nullptr);

    FluxionAssetTypeDesc noLoad = MakeBlobTypeDesc();
    noLoad.load = nullptr;
    TEST_CHECK(ctx, !Fluxion_AssetTypes_Register(&noLoad));

    FluxionAssetTypeDesc noUnload = MakeBlobTypeDesc();
    noUnload.unload = nullptr;
    TEST_CHECK(ctx, !Fluxion_AssetTypes_Register(&noUnload));

    FluxionAssetTypeDesc noName = MakeBlobTypeDesc();
    noName.name[0] = '\0';
    TEST_CHECK(ctx, !Fluxion_AssetTypes_Register(&noName));

    // A name filling the whole array has no terminator, so nothing can
    // say where it ends.
    FluxionAssetTypeDesc unterminated = MakeBlobTypeDesc();
    std::memset(unterminated.name, 'x', sizeof(unterminated.name));
    TEST_CHECK(ctx, !Fluxion_AssetTypes_Register(&unterminated));

    TEST_CHECK(ctx, Fluxion_AssetTypes_GetCount() == 0);

    Fluxion_AssetTypes_Shutdown();
}

// A type's import half is what claims a source extension. In a built game
// no plugin registers one, so no extension is claimed -- which is the
// same code saying "there is no importer here".
void SourceExtensionsBelongToTheImportHalf(TestContext& ctx)
{
    Fluxion_AssetTypes_Init(nullptr);

    FluxionAssetTypeDesc withImporter = MakeModelTypeDesc(true);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&withImporter));
    TEST_CHECK(ctx, Fluxion_AssetTypes_FindBySourceExtension("tsrc") == ModelTypeId());
    TEST_CHECK(ctx, Fluxion_AssetTypes_FindBySourceExtension("TSRC") == ModelTypeId());
    TEST_CHECK(ctx, Fluxion_AssetTypes_FindBySourceExtension("nope") == FLUXION_ASSET_TYPE_ID_INVALID);

    const FluxionAssetTypeDesc* found = Fluxion_AssetTypes_Find(ModelTypeId());
    TEST_CHECK(ctx, found != nullptr && found->import != nullptr);

    // The same type, as a shipped game sees it.
    TEST_CHECK(ctx, Fluxion_AssetTypes_Unregister(ModelTypeId()));
    FluxionAssetTypeDesc withoutImporter = MakeModelTypeDesc(false);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&withoutImporter));

    TEST_CHECK(ctx, Fluxion_AssetTypes_FindBySourceExtension("tsrc") == FLUXION_ASSET_TYPE_ID_INVALID);

    found = Fluxion_AssetTypes_Find(ModelTypeId());
    TEST_CHECK(ctx, found != nullptr);
    if (found)
    {
        TEST_CHECK(ctx, found->import == nullptr);

        // And the half that ships is still all there.
        TEST_CHECK(ctx, found->load == TestModel_Load);
    }

    Fluxion_AssetTypes_Shutdown();
}

void UnregisterKeepsTheRestFindable(TestContext& ctx)
{
    Fluxion_AssetTypes_Init(nullptr);

    FluxionAssetTypeDesc blob = MakeBlobTypeDesc();
    FluxionAssetTypeDesc model = MakeModelTypeDesc(true);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&blob));
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&model));

    // Removing the first one moves the last into its place. Whatever
    // moved has to stay findable, and by its own id.
    TEST_CHECK(ctx, Fluxion_AssetTypes_Unregister(BlobTypeId()));
    TEST_CHECK(ctx, Fluxion_AssetTypes_GetCount() == 1);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Find(BlobTypeId()) == nullptr);

    const FluxionAssetTypeDesc* model2 = Fluxion_AssetTypes_Find(ModelTypeId());
    TEST_CHECK(ctx, model2 != nullptr);
    if (model2) TEST_CHECK(ctx, std::strcmp(model2->name, ASSET_TEST_MODEL_TYPE_NAME) == 0);

    // Asked the other way round as well: whatever is in the table has to
    // be reachable by the id the table itself gives for it.
    TEST_CHECK(ctx, Fluxion_AssetTypes_GetIdAt(0) == ModelTypeId());
    TEST_CHECK(ctx, Fluxion_AssetTypes_GetAt(0) == model2);

    TEST_CHECK(ctx, !Fluxion_AssetTypes_Unregister(BlobTypeId()));

    Fluxion_AssetTypes_Shutdown();
}

} // namespace

void Test_AssetTypes_Run(TestContext& ctx)
{
    std::fprintf(stderr, "  Test_AssetTypes\n");

    RegisterAndFind(ctx);
    RefusesTypesNothingCanLoad(ctx);
    SourceExtensionsBelongToTheImportHalf(ctx);
    UnregisterKeepsTheRestFindable(ctx);
}
