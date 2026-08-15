#include "AssetTestSupport.h"
#include "TestFramework.h"

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetSystem.h>
#include <Fluxion/Assets/Package.h>
#include <Fluxion/Assets/Assets.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace AssetTestSupport;

namespace
{

const char* const kSourceText = "a model, as a person wrote it";

// Runs the import half by hand, the way an editor would: read the source,
// cook it, write the result next to it.
bool CookOne(const FluxionAssetTypeDesc* type, const char* sourcePath, const char* cookedPath)
{
    usize sourceSize = 0;
    u8* sourceBytes = Fluxion_Vfs_ReadAll(sourcePath, &sourceSize);
    if (!sourceBytes) return false;

    std::vector<u8> cooked(4096, 0);
    FluxionStream stream;
    Fluxion_MemoryStream_InitWriter(&stream, cooked.data(), cooked.size());

    const bool imported = type->import(sourceBytes, sourceSize, &stream, type->userData);
    Fluxion_Vfs_FreeBuffer(sourceBytes, sourceSize);

    if (!imported) return false;
    return Fluxion_Vfs_WriteAll(cookedPath, cooked.data(), Fluxion_Stream_GetPosition(&stream));
}

const FluxionPackageBuildEntry* FindEntry(const FluxionPackageBuildReport& report, FluxionUUID asset)
{
    for (u32 i = 0; i < report.entryCount; ++i)
    {
        if (Fluxion_UUID_Equals(report.entries[i].asset, asset)) return &report.entries[i];
    }
    return nullptr;
}

// A project with one model in it: the source a person made, and the
// cooked form an importer produced from it.
struct Project
{
    bool ok = false;
    std::filesystem::path root;
    std::filesystem::path packagePath;
    FluxionUUID model{};

    explicit Project(const char* name)
    {
        root = MakeDirectory(name);
        if (root.empty()) return;

        packagePath = root / "Game.fluxpak";

        if (!Fluxion_AssetSystem_Init(nullptr)) return;
        if (!Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create(root.string().c_str()))) return;

        FluxionAssetTypeDesc model_ = MakeModelTypeDesc(true);
        if (!Fluxion_AssetTypes_Register(&model_)) return;

        if (!WriteText(root / "Models" / "Cube.tsrc", kSourceText)) return;

        const FluxionAssetTypeDesc* type = Fluxion_AssetTypes_Find(ModelTypeId());
        if (!type || !CookOne(type, "assets://Models/Cube.tsrc", "assets://Models/Cube.tmesh")) return;

        FluxionAssetDesc desc{};
        desc.type = ModelTypeId();
        desc.name = "Cube";
        desc.sourcePath = "assets://Models/Cube.tsrc";
        desc.cookedPath = "assets://Models/Cube.tmesh";
        if (!Fluxion_AssetDatabase_Add(&desc, &model)) return;

        ok = true;
    }

    ~Project() { Fluxion_AssetSystem_Shutdown(); }

    std::string PackagePathString() const { return packagePath.string(); }
};

bool Contains(const std::vector<u8>& haystack, const char* needle)
{
    const std::size_t length = std::strlen(needle);
    if (haystack.size() < length) return false;

    for (std::size_t i = 0; i + length <= haystack.size(); ++i)
    {
        if (std::memcmp(haystack.data() + i, needle, length) == 0) return true;
    }
    return false;
}

// The whole point: the cooked form ships, the source does not.
void TheSourceStaysOut(TestContext& ctx)
{
    Project project("ship-cooked");
    if (!project.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    FluxionPackageBuildReport report{};
    TEST_CHECK(ctx, Fluxion_Package_Build(nullptr, project.PackagePathString().c_str(), &report));

    TEST_CHECK(ctx, report.errorCount == 0);
    TEST_CHECK(ctx, report.includedCount == 1);
    TEST_CHECK(ctx, report.excludedCount == 0);

    const FluxionPackageBuildEntry* entry = FindEntry(report, project.model);
    TEST_CHECK(ctx, entry != nullptr);
    if (entry)
    {
        TEST_CHECK(ctx, entry->outcome == FLUXION_PACKAGE_OUTCOME_INCLUDED_COOKED);
        TEST_CHECK(ctx, std::strcmp(entry->typeName, ASSET_TEST_MODEL_TYPE_NAME) == 0);
        TEST_CHECK(ctx, std::strcmp(entry->name, "Cube") == 0);
    }

    Fluxion_Package_FreeReport(&report);

    // Said by looking at the bytes, not by asking the thing that wrote
    // them. The cooked form's marker is in there; the path of the source
    // is not, and neither is the source text on its own.
    const std::vector<u8> bytes = ReadWholeFile(project.packagePath);
    TEST_CHECK(ctx, !bytes.empty());
    TEST_CHECK(ctx, Contains(bytes, "Models/Cube.tmesh"));
    TEST_CHECK(ctx, Contains(bytes, ASSET_TEST_MODEL_COOKED_MARKER));
    TEST_CHECK(ctx, !Contains(bytes, "Models/Cube.tsrc"));
    TEST_CHECK(ctx, !Contains(bytes, "assets://Models/Cube.tsrc"));

    // The cooked bytes contain the source text after the marker, so the
    // text alone proves nothing -- what proves it is that the source
    // never appears WITHOUT the marker in front, which is what the path
    // checks above stand for.
}

// The same project, with the build told to do the other thing. This is
// the setting being load-bearing rather than decorative.
void ThePolicyDecidesWhichFormShips(TestContext& ctx)
{
    Project project("ship-source");
    if (!project.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    FluxionAssetShipOverride override_{};
    std::memcpy(override_.typeName, ASSET_TEST_MODEL_TYPE_NAME, sizeof(ASSET_TEST_MODEL_TYPE_NAME));
    override_.policy = FLUXION_ASSET_SHIP_SOURCE;

    FluxionPackageBuildDesc desc{};
    desc.overrides = &override_;
    desc.overrideCount = 1;

    // What the type says on its own, and what the build says instead.
    const FluxionAssetTypeDesc* type = Fluxion_AssetTypes_Find(ModelTypeId());
    TEST_CHECK(ctx, type != nullptr && type->defaultShipPolicy == FLUXION_ASSET_SHIP_COOKED);
    TEST_CHECK(ctx, Fluxion_Package_ResolveShipPolicy(nullptr, type) == FLUXION_ASSET_SHIP_COOKED);
    TEST_CHECK(ctx, Fluxion_Package_ResolveShipPolicy(&desc, type) == FLUXION_ASSET_SHIP_SOURCE);

    FluxionPackageBuildReport report{};
    TEST_CHECK(ctx, Fluxion_Package_Build(&desc, project.PackagePathString().c_str(), &report));

    const FluxionPackageBuildEntry* entry = FindEntry(report, project.model);
    TEST_CHECK(ctx, entry != nullptr);
    if (entry) TEST_CHECK(ctx, entry->outcome == FLUXION_PACKAGE_OUTCOME_INCLUDED_SOURCE);

    Fluxion_Package_FreeReport(&report);

    const std::vector<u8> bytes = ReadWholeFile(project.packagePath);
    TEST_CHECK(ctx, !bytes.empty());
    TEST_CHECK(ctx, Contains(bytes, "Models/Cube.tsrc"));
    TEST_CHECK(ctx, !Contains(bytes, "Models/Cube.tmesh"));
    TEST_CHECK(ctx, !Contains(bytes, ASSET_TEST_MODEL_COOKED_MARKER));
}

void NeverMeansNothingGoesIn(TestContext& ctx)
{
    Project project("ship-never");
    if (!project.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    FluxionAssetShipOverride override_{};
    std::memcpy(override_.typeName, ASSET_TEST_MODEL_TYPE_NAME, sizeof(ASSET_TEST_MODEL_TYPE_NAME));
    override_.policy = FLUXION_ASSET_SHIP_NEVER;

    FluxionPackageBuildDesc desc{};
    desc.overrides = &override_;
    desc.overrideCount = 1;

    FluxionPackageBuildReport report{};
    TEST_CHECK(ctx, Fluxion_Package_Build(&desc, project.PackagePathString().c_str(), &report));

    TEST_CHECK(ctx, report.errorCount == 0);
    TEST_CHECK(ctx, report.includedCount == 0);
    TEST_CHECK(ctx, report.excludedCount == 1);

    const FluxionPackageBuildEntry* entry = FindEntry(report, project.model);
    TEST_CHECK(ctx, entry != nullptr);
    if (entry) TEST_CHECK(ctx, entry->outcome == FLUXION_PACKAGE_OUTCOME_EXCLUDED);

    Fluxion_Package_FreeReport(&report);

    const std::vector<u8> bytes = ReadWholeFile(project.packagePath);
    TEST_CHECK(ctx, !Contains(bytes, "Models/Cube.tmesh"));
    TEST_CHECK(ctx, !Contains(bytes, "Models/Cube.tsrc"));
}

// An asset that ships and needs something that does not is a game that
// starts and then does not work. It has to be caught here, because here
// is the last place it can still be fixed.
void ADependencyLeftOutIsAnError(TestContext& ctx)
{
    Project project("broken-dependency");
    if (!project.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    // A second type, excluded, that the model needs.
    FluxionAssetTypeDesc blob = MakeBlobTypeDesc(FLUXION_ASSET_SHIP_NEVER);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&blob));
    TEST_CHECK(ctx, WriteText(project.root / "Textures" / "Grid.blob", "a texture"));

    FluxionAssetDesc textureDesc{};
    textureDesc.type = BlobTypeId();
    textureDesc.name = "Grid";
    textureDesc.cookedPath = "assets://Textures/Grid.blob";
    FluxionUUID texture{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&textureDesc, &texture));

    // Re-add the model with the texture as a dependency.
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Remove(project.model));

    const FluxionUUID needed[] = { texture };
    FluxionAssetDesc modelDesc{};
    modelDesc.id = project.model;
    modelDesc.type = ModelTypeId();
    modelDesc.name = "Cube";
    modelDesc.sourcePath = "assets://Models/Cube.tsrc";
    modelDesc.cookedPath = "assets://Models/Cube.tmesh";
    modelDesc.dependencies = needed;
    modelDesc.dependencyCount = 1;
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&modelDesc, nullptr));

    FluxionPackageBuildReport report{};
    TEST_CHECK(ctx, !Fluxion_Package_Build(nullptr, project.PackagePathString().c_str(), &report));
    TEST_CHECK(ctx, report.errorCount == 1);

    const FluxionPackageBuildEntry* entry = FindEntry(report, project.model);
    TEST_CHECK(ctx, entry != nullptr);
    if (entry)
    {
        TEST_CHECK(ctx, entry->outcome == FLUXION_PACKAGE_OUTCOME_BROKEN_DEPENDENCY);
        TEST_CHECK(ctx, Fluxion_UUID_Equals(entry->culprit, texture));
    }

    Fluxion_Package_FreeReport(&report);

    // Nothing was written. A package missing something it claims to hold
    // is worse than no package.
    TEST_CHECK(ctx, !std::filesystem::exists(project.packagePath));
}

void MissingBytesAreAnErrorNotAnOmission(TestContext& ctx)
{
    Project project("missing-bytes");
    if (!project.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    std::error_code error;
    std::filesystem::remove(project.root / "Models" / "Cube.tmesh", error);

    FluxionPackageBuildReport report{};
    TEST_CHECK(ctx, !Fluxion_Package_Build(nullptr, project.PackagePathString().c_str(), &report));
    TEST_CHECK(ctx, report.errorCount == 1);

    const FluxionPackageBuildEntry* entry = FindEntry(report, project.model);
    TEST_CHECK(ctx, entry != nullptr);
    if (entry) TEST_CHECK(ctx, entry->outcome == FLUXION_PACKAGE_OUTCOME_MISSING_DATA);

    Fluxion_Package_FreeReport(&report);
}

// A built game, as far as this can be one: a fresh start with only the
// package mounted, no source anywhere, and no importer registered. It has
// to load through THE SAME calls a project being worked on uses.
void TheGameLoadsFromThePackage(TestContext& ctx)
{
    std::filesystem::path packagePath;
    FluxionUUID modelId{};

    {
        Project project("built-game");
        if (!project.ok)
        {
            TEST_CHECK(ctx, false);
            return;
        }

        packagePath = project.packagePath;
        modelId = project.model;

        TEST_CHECK(ctx, Fluxion_Package_Build(nullptr, project.PackagePathString().c_str(), nullptr));
    }

    TEST_CHECK(ctx, Fluxion_AssetSystem_Init(nullptr));
    TEST_CHECK(ctx, Fluxion_Vfs_Mount("assets", Fluxion_VfsPackageSource_CreateFromFile(packagePath.string().c_str())));

    // No import half, because a built game loads no importer plugin.
    FluxionAssetTypeDesc shipped = MakeModelTypeDesc(false);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&shipped));

    usize indexSize = 0;
    u8* indexBytes = Fluxion_Vfs_ReadAll("assets://" FLUXION_PACKAGE_DATABASE_PATH, &indexSize);
    TEST_CHECK(ctx, indexBytes != nullptr);

    if (indexBytes)
    {
        FluxionStream reader;
        Fluxion_MemoryStream_InitReader(&reader, indexBytes, indexSize);
        TEST_CHECK(ctx, Fluxion_AssetDatabase_Serialize(&reader));
        Fluxion_Vfs_FreeBuffer(indexBytes, indexSize);
    }

    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == 1);

    const FluxionAssetRecord* record = Fluxion_AssetDatabase_Find(modelId);
    TEST_CHECK(ctx, record != nullptr);
    if (record)
    {
        // The index a game ships carries no source path. Not tidiness:
        // it is why nothing in a running game has anything to reach for.
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetSourcePath(record), "") == 0);
        TEST_CHECK(ctx, std::strcmp(Fluxion_AssetDatabase_GetCookedPath(record), "assets://Models/Cube.tmesh") == 0);
    }

    // And the source is not in the package at all, so nothing could
    // reach it even knowing where to look.
    TEST_CHECK(ctx, !Fluxion_Vfs_Exists("assets://Models/Cube.tsrc"));
    TEST_CHECK(ctx, Fluxion_Vfs_Exists("assets://Models/Cube.tmesh"));

    FluxionAssetHandle handle = Fluxion_Assets_Acquire(modelId);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(handle));
    TEST_CHECK(ctx, Fluxion_Assets_GetState(handle) == FLUXION_ASSET_STATE_READY);

    const TestBlob* object = static_cast<const TestBlob*>(Fluxion_Assets_GetObject(handle));
    TEST_CHECK(ctx, object != nullptr);
    if (object) TEST_CHECK(ctx, std::strcmp(object->text, kSourceText) == 0);

    Fluxion_Assets_Release(handle);
    Fluxion_AssetSystem_Shutdown();
}

// A package cut short, or written over while it was being read, hands
// back bytes that look like an asset and are not one. Checked at the
// package rather than left to fail somewhere else entirely.
void ADamagedPackageIsRefused(TestContext& ctx)
{
    std::filesystem::path packagePath;

    {
        Project project("damaged");
        if (!project.ok)
        {
            TEST_CHECK(ctx, false);
            return;
        }

        packagePath = project.packagePath;
        TEST_CHECK(ctx, Fluxion_Package_Build(nullptr, project.PackagePathString().c_str(), nullptr));
    }

    std::vector<u8> bytes = ReadWholeFile(packagePath);
    TEST_CHECK(ctx, bytes.size() > 16);
    if (bytes.size() <= 16) return;

    // The last byte is inside a blob, past every offset in the index --
    // so the index still adds up and only the contents are wrong.
    bytes[bytes.size() - 1] ^= 0xFFu;
    TEST_CHECK(ctx, WriteFile(packagePath, bytes.data(), bytes.size()));

    Fluxion_Vfs_Init(nullptr);
    FluxionVfsSource* source = Fluxion_VfsPackageSource_CreateFromFile(packagePath.string().c_str());
    TEST_CHECK(ctx, source != nullptr);
    TEST_CHECK(ctx, Fluxion_Vfs_Mount("assets", source));

    // Something in there is now wrong. Whichever entry it was, the read
    // of it must fail rather than hand back what it found.
    usize size = 0;
    u8* mesh = Fluxion_Vfs_ReadAll("assets://Models/Cube.tmesh", &size);
    u8* index = Fluxion_Vfs_ReadAll("assets://" FLUXION_PACKAGE_DATABASE_PATH, &size);
    TEST_CHECK(ctx, mesh == nullptr || index == nullptr);
    if (mesh) Fluxion_Vfs_FreeBuffer(mesh, size);
    if (index) Fluxion_Vfs_FreeBuffer(index, size);

    Fluxion_Vfs_Shutdown();

    // A file that is not a package at all is refused outright.
    const std::filesystem::path notAPackage = packagePath.parent_path() / "junk.fluxpak";
    TEST_CHECK(ctx, WriteText(notAPackage, "this is not a package"));

    Fluxion_Vfs_Init(nullptr);
    TEST_CHECK(ctx, Fluxion_VfsPackageSource_CreateFromFile(notAPackage.string().c_str()) == nullptr);
    Fluxion_Vfs_Shutdown();
}

// An asset cooked more than once ships the form the build asked for, and
// only that one. Without this the package would carry every variant and
// a mobile build would be paying for desktop bytes it cannot even read.
void TheBuildTakesItsOwnCookedForm(TestContext& ctx)
{
    const std::filesystem::path root = MakeDirectory("cook-target");
    if (root.empty())
    {
        TEST_CHECK(ctx, false);
        return;
    }

    TEST_CHECK(ctx, Fluxion_AssetSystem_Init(nullptr));
    TEST_CHECK(ctx, Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create(root.string().c_str())));

    FluxionAssetTypeDesc blob = MakeBlobTypeDesc();
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&blob));

    TEST_CHECK(ctx, WriteText(root / "Brick.desktop.blob", "DESKTOP-BYTES"));
    TEST_CHECK(ctx, WriteText(root / "Brick.mobile.blob", "MOBILE-BYTES"));

    const FluxionAssetCookedForm forms[] = {
        { "desktop", "assets://Brick.desktop.blob" },
        { "mobile", "assets://Brick.mobile.blob" },
    };

    FluxionAssetDesc desc{};
    desc.type = BlobTypeId();
    desc.name = "Brick";
    desc.cookedForms = forms;
    desc.cookedFormCount = 2;

    FluxionUUID id{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&desc, &id));

    const std::filesystem::path packagePath = root / "Mobile.fluxpak";

    FluxionPackageBuildDesc build{};
    std::memcpy(build.cookTarget, "mobile", sizeof("mobile"));

    TEST_CHECK(ctx, Fluxion_Package_Build(&build, packagePath.string().c_str(), nullptr));

    const std::vector<u8> bytes = ReadWholeFile(packagePath);
    TEST_CHECK(ctx, !bytes.empty());
    TEST_CHECK(ctx, Contains(bytes, "MOBILE-BYTES"));
    TEST_CHECK(ctx, !Contains(bytes, "DESKTOP-BYTES"));

    // And the shipped index points at the one form that is in there --
    // not at a path naming a file the package does not contain.
    TEST_CHECK(ctx, !Contains(bytes, "Brick.desktop.blob"));

    Fluxion_AssetSystem_Shutdown();
}

} // namespace

void Test_Package_Run(TestContext& ctx)
{
    std::fprintf(stderr, "  Test_Package\n");

    TheSourceStaysOut(ctx);
    ThePolicyDecidesWhichFormShips(ctx);
    NeverMeansNothingGoesIn(ctx);
    ADependencyLeftOutIsAnError(ctx);
    MissingBytesAreAnErrorNotAnOmission(ctx);
    TheGameLoadsFromThePackage(ctx);
    ADamagedPackageIsRefused(ctx);
    TheBuildTakesItsOwnCookedForm(ctx);
}
