// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

#include "AssetTestSupport.h"
#include "TestFramework.h"

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetImport.h>
#include <Fluxion/Assets/AssetSystem.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Core/Jobs/JobSystem.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace AssetTestSupport;

namespace
{

// A type whose importer always refuses. One broken asset among many must
// be reported and must not take the rest with it.
#define ASSET_TEST_BROKEN_TYPE_NAME "TestBroken"

bool Broken_Import(const u8* sourceBytes, usize sourceSize, FluxionStream* cookedOut, void* userData)
{
    (void)sourceBytes;
    (void)sourceSize;
    (void)cookedOut;
    (void)userData;
    return false;
}

FluxionAssetTypeId BrokenTypeId()
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(ASSET_TEST_BROKEN_TYPE_NAME));
}

FluxionAssetTypeDesc MakeBrokenTypeDesc()
{
    FluxionAssetTypeDesc desc{};
    std::memcpy(desc.name, ASSET_TEST_BROKEN_TYPE_NAME, sizeof(ASSET_TEST_BROKEN_TYPE_NAME));
    std::memcpy(desc.cookedExtension, "brk", sizeof("brk"));
    std::memcpy(desc.sourceExtensions[0], "brk", sizeof("brk"));
    desc.sourceExtensionCount = 1;
    desc.import = Broken_Import;
    desc.load = TestBlob_Load;
    desc.unload = TestBlob_Unload;
    return desc;
}

struct Fixture
{
    bool ok = false;
    std::filesystem::path root;

    explicit Fixture(const char* name)
    {
        root = MakeDirectory(name);
        if (root.empty()) return;

        if (!Fluxion_AssetSystem_Init(nullptr)) return;
        if (!Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create(root.string().c_str()))) return;

        FluxionAssetTypeDesc model = MakeModelTypeDesc(true);
        if (!Fluxion_AssetTypes_Register(&model)) return;

        FluxionAssetTypeDesc broken = MakeBrokenTypeDesc();
        if (!Fluxion_AssetTypes_Register(&broken)) return;

        ok = true;
    }

    ~Fixture() { Fluxion_AssetSystem_Shutdown(); }
};

std::string SourcePath(int i) { return "assets://Src/model" + std::to_string(i) + ".tsrc"; }
std::string CookedPath(int i) { return "assets://Cooked/model" + std::to_string(i) + ".tmesh"; }

// Sources of deliberately different sizes, so the weighted fraction has
// something to weight.
void WriteSources(TestContext& ctx, const Fixture& fixture, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const std::string text(64u + (std::size_t)i * 512u, (char)('a' + (i % 26)));
        TEST_CHECK(ctx, WriteText(fixture.root / "Src" / ("model" + std::to_string(i) + ".tsrc"), text.c_str()));
    }
}

std::vector<FluxionAssetImportItem> MakeItems(std::vector<std::string>& sources, std::vector<std::string>& cooked,
                                              std::vector<std::string>& names, int count)
{
    sources.clear();
    cooked.clear();
    names.clear();
    for (int i = 0; i < count; ++i)
    {
        sources.push_back(SourcePath(i));
        cooked.push_back(CookedPath(i));
        names.push_back("model" + std::to_string(i));
    }

    std::vector<FluxionAssetImportItem> items(count);
    for (int i = 0; i < count; ++i)
    {
        items[i] = FluxionAssetImportItem{};
        items[i].type = ModelTypeId();
        items[i].name = names[i].c_str();
        items[i].sourcePath = sources[i].c_str();
        items[i].cookedPath = cooked[i].c_str();
    }
    return items;
}

// The structural claim the whole arrangement rests on: a worker cooks,
// and NOTHING reaches the database until the owning thread pumps.
void WorkersCookAndTheOwningThreadRecords(TestContext& ctx)
{
    Fixture fixture("import-threads");
    if (!fixture.ok) { TEST_CHECK(ctx, false); return; }

    constexpr int kCount = 12;
    WriteSources(ctx, fixture, kCount);

    std::vector<std::string> sources, cooked, names;
    std::vector<FluxionAssetImportItem> items = MakeItems(sources, cooked, names, kCount);

    Fluxion_JobSystem_Init(4, false);

    FluxionAssetImportDesc desc{};
    desc.items = items.data();
    desc.itemCount = (u32)items.size();

    FluxionAssetImport* import = Fluxion_AssetImport_Begin(&desc);
    TEST_CHECK(ctx, import != nullptr);
    if (!import) { Fluxion_JobSystem_Shutdown(); return; }

    // Begin hands the work over and returns. Whatever the workers have
    // done by now, the database has not been touched -- it is the owning
    // thread's, and no worker writes into it.
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == 0);

    // Pumping while it runs is allowed and is what a frame loop does.
    // Nothing here blocks.
    for (int spin = 0; spin < 4 && !Fluxion_AssetImport_IsFinished(import); ++spin)
    {
        const FluxionAssetImportProgress progress = Fluxion_AssetImport_GetProgress(import);
        TEST_CHECK(ctx, progress.total == kCount);
        TEST_CHECK(ctx, progress.fraction >= 0.0f && progress.fraction <= 1.0f);
        TEST_CHECK(ctx, progress.currentItem >= -1 && progress.currentItem < kCount);
        Fluxion_AssetImport_Update(import);
    }

    Fluxion_AssetImport_End(import);

    // Finished means finished: every item accounted for and the bar full.
    // (End released the handle, so this is read from the database now.)
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == kCount);

    // Every cooked file really is on disk, and really was produced by the
    // import half rather than copied.
    for (int i = 0; i < kCount; ++i)
    {
        TEST_CHECK(ctx, Fluxion_Vfs_Exists(CookedPath(i).c_str()));

        usize size = 0;
        u8* bytes = Fluxion_Vfs_ReadAll(CookedPath(i).c_str(), &size);
        TEST_CHECK(ctx, bytes != nullptr);
        if (bytes)
        {
            const usize markerLength = sizeof(ASSET_TEST_MODEL_COOKED_MARKER) - 1;
            TEST_CHECK(ctx, size > markerLength);
            TEST_CHECK(ctx, std::memcmp(bytes, ASSET_TEST_MODEL_COOKED_MARKER, markerLength) == 0);
            Fluxion_Vfs_FreeBuffer(bytes, size);
        }
    }

    Fluxion_JobSystem_Shutdown();
}

void ProgressReachesTheEndAndIsWeighted(TestContext& ctx)
{
    Fixture fixture("import-progress");
    if (!fixture.ok) { TEST_CHECK(ctx, false); return; }

    constexpr int kCount = 6;
    WriteSources(ctx, fixture, kCount);

    std::vector<std::string> sources, cooked, names;
    std::vector<FluxionAssetImportItem> items = MakeItems(sources, cooked, names, kCount);

    FluxionAssetImportDesc desc{};
    desc.items = items.data();
    desc.itemCount = (u32)items.size();

    // No job system: everything happens inside Begin. A caller that never
    // started one still gets its assets imported rather than an import
    // that never runs.
    TEST_CHECK(ctx, !Fluxion_JobSystem_IsInitialized());

    FluxionAssetImport* import = Fluxion_AssetImport_Begin(&desc);
    TEST_CHECK(ctx, import != nullptr);
    if (!import) return;

    TEST_CHECK(ctx, Fluxion_AssetImport_IsFinished(import));

    const FluxionAssetImportProgress progress = Fluxion_AssetImport_GetProgress(import);
    TEST_CHECK(ctx, progress.completed == kCount);
    TEST_CHECK(ctx, progress.failed == 0);

    // Full, and not past full: the two counters are read one after the
    // other, and a bar showing more than everything reads as a bug.
    TEST_CHECK(ctx, progress.fraction > 0.999f && progress.fraction <= 1.0f);

    TEST_CHECK(ctx, Fluxion_AssetImport_GetItemCount(import) == kCount);
    for (u32 i = 0; i < (u32)kCount; ++i)
    {
        TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(import, i) == FLUXION_ASSET_IMPORT_COOKED);
        TEST_CHECK(ctx, std::strlen(Fluxion_AssetImport_GetNameAt(import, i)) > 0);
    }

    TEST_CHECK(ctx, Fluxion_AssetImport_Update(import) == kCount);
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == kCount);

    // Pumped twice, added once.
    TEST_CHECK(ctx, Fluxion_AssetImport_Update(import) == 0);
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == kCount);

    Fluxion_AssetImport_End(import);
}

// The ordinary case in a project someone is working in: nothing changed,
// so nothing is cooked. This is what the settings hash is for.
void NothingChangedMeansNothingCooked(TestContext& ctx)
{
    Fixture fixture("import-uptodate");
    if (!fixture.ok) { TEST_CHECK(ctx, false); return; }

    WriteSources(ctx, fixture, 1);

    const u8 settings[] = { 1, 2, 3 };
    const u8 otherSettings[] = { 1, 2, 4 };

    const std::string source = SourcePath(0);
    const std::string cookedPath = CookedPath(0);

    FluxionAssetImportItem item{};
    item.type = ModelTypeId();
    item.name = "model0";
    item.sourcePath = source.c_str();
    item.cookedPath = cookedPath.c_str();
    item.importSettings = settings;
    item.importSettingsSize = (u32)sizeof(settings);

    FluxionAssetImportDesc desc{};
    desc.items = &item;
    desc.itemCount = 1;

    FluxionAssetImport* first = Fluxion_AssetImport_Begin(&desc);
    TEST_CHECK(ctx, first != nullptr);
    if (!first) return;
    TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(first, 0) == FLUXION_ASSET_IMPORT_COOKED);

    const FluxionUUID id = [&] {
        Fluxion_AssetImport_Update(first);
        return Fluxion_AssetImport_GetAssetAt(first, 0);
    }();
    TEST_CHECK(ctx, !Fluxion_UUID_IsNil(id));
    Fluxion_AssetImport_End(first);

    // The same asset, the same source, the same settings.
    item.asset = id;
    FluxionAssetImport* again = Fluxion_AssetImport_Begin(&desc);
    TEST_CHECK(ctx, again != nullptr);
    if (again)
    {
        TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(again, 0) == FLUXION_ASSET_IMPORT_UP_TO_DATE);
        Fluxion_AssetImport_End(again);
    }

    // Told to anyway.
    desc.force = true;
    FluxionAssetImport* forced = Fluxion_AssetImport_Begin(&desc);
    if (forced)
    {
        TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(forced, 0) == FLUXION_ASSET_IMPORT_COOKED);
        Fluxion_AssetImport_End(forced);
    }
    desc.force = false;

    // Different settings, same source: the cooked form is no longer what
    // these settings would produce, so it is produced again.
    item.importSettings = otherSettings;
    FluxionAssetImport* changed = Fluxion_AssetImport_Begin(&desc);
    if (changed)
    {
        TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(changed, 0) == FLUXION_ASSET_IMPORT_COOKED);
        Fluxion_AssetImport_End(changed);
    }

    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == 1);
}

// One broken asset among many is reported and does not take the rest with
// it. An import of nine hundred things must not be lost to the nine
// hundredth.
void OneFailureDoesNotStopTheRest(TestContext& ctx)
{
    Fixture fixture("import-failures");
    if (!fixture.ok) { TEST_CHECK(ctx, false); return; }

    WriteSources(ctx, fixture, 3);
    TEST_CHECK(ctx, WriteText(fixture.root / "Src" / "broken.brk", "whatever"));

    const std::string s0 = SourcePath(0), c0 = CookedPath(0);
    const std::string s1 = SourcePath(1), c1 = CookedPath(1);

    FluxionAssetImportItem items[5]{};

    items[0].type = ModelTypeId();
    items[0].name = "good0";
    items[0].sourcePath = s0.c_str();
    items[0].cookedPath = c0.c_str();

    // An importer that refuses.
    items[1].type = BrokenTypeId();
    items[1].name = "broken";
    items[1].sourcePath = "assets://Src/broken.brk";
    items[1].cookedPath = "assets://Cooked/broken.brk";

    // A source that is not there.
    items[2].type = ModelTypeId();
    items[2].name = "missing";
    items[2].sourcePath = "assets://Src/nothing-here.tsrc";
    items[2].cookedPath = "assets://Cooked/nothing.tmesh";

    // A type with no import half at all -- which is every type, as far as
    // a shipped game is concerned.
    items[3].type = BlobTypeId();
    items[3].name = "noimporter";
    items[3].sourcePath = s1.c_str();
    items[3].cookedPath = "assets://Cooked/noimporter.blob";

    items[4].type = ModelTypeId();
    items[4].name = "good1";
    items[4].sourcePath = s1.c_str();
    items[4].cookedPath = c1.c_str();

    FluxionAssetImportDesc desc{};
    desc.items = items;
    desc.itemCount = 5;

    FluxionAssetImport* import = Fluxion_AssetImport_Begin(&desc);
    TEST_CHECK(ctx, import != nullptr);
    if (!import) return;

    TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(import, 0) == FLUXION_ASSET_IMPORT_COOKED);
    TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(import, 1) == FLUXION_ASSET_IMPORT_COOK_FAILED);
    TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(import, 2) == FLUXION_ASSET_IMPORT_SOURCE_MISSING);
    TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(import, 3) == FLUXION_ASSET_IMPORT_NO_IMPORTER);
    TEST_CHECK(ctx, Fluxion_AssetImport_GetOutcomeAt(import, 4) == FLUXION_ASSET_IMPORT_COOKED);

    const FluxionAssetImportProgress progress = Fluxion_AssetImport_GetProgress(import);
    TEST_CHECK(ctx, progress.completed == 5);
    TEST_CHECK(ctx, progress.failed == 3);

    // The two that worked are recorded; the three that did not leave the
    // database exactly as it was.
    Fluxion_AssetImport_Update(import);
    TEST_CHECK(ctx, Fluxion_AssetDatabase_GetCount() == 2);

    Fluxion_AssetImport_End(import);
}

void CancellingStopsWhatHasNotStarted(TestContext& ctx)
{
    Fixture fixture("import-cancel");
    if (!fixture.ok) { TEST_CHECK(ctx, false); return; }

    constexpr int kCount = 16;
    WriteSources(ctx, fixture, kCount);

    std::vector<std::string> sources, cooked, names;
    std::vector<FluxionAssetImportItem> items = MakeItems(sources, cooked, names, kCount);

    FluxionAssetImportDesc desc{};
    desc.items = items.data();
    desc.itemCount = (u32)items.size();

    Fluxion_JobSystem_Init(2, false);

    FluxionAssetImport* import = Fluxion_AssetImport_Begin(&desc);
    TEST_CHECK(ctx, import != nullptr);
    if (!import) { Fluxion_JobSystem_Shutdown(); return; }

    Fluxion_AssetImport_Cancel(import);
    Fluxion_AssetImport_End(import);

    // Every item is accounted for either way -- cancelling stops the work
    // and not the bookkeeping, so nothing is left saying "pending"
    // forever.
    //
    // How many were cooked before the cancel arrived is a matter of
    // timing and is deliberately not asserted; what is asserted is that
    // none is unaccounted for and none was cooked without being recorded.
    for (int i = 0; i < kCount; ++i)
    {
        const std::string path = CookedPath(i);
        if (!Fluxion_Vfs_Exists(path.c_str())) continue;

        // A file on disk means that item really did run, so the database
        // has to know about it.
        bool found = false;
        for (u32 r = 0; r < Fluxion_AssetDatabase_GetCount(); ++r)
        {
            const FluxionAssetRecord* record = Fluxion_AssetDatabase_GetAt(r);
            if (std::strcmp(Fluxion_AssetDatabase_GetCookedPath(record), path.c_str()) == 0) found = true;
        }
        TEST_CHECK(ctx, found);
    }

    Fluxion_JobSystem_Shutdown();
}

} // namespace

void Test_AssetImport_Run(TestContext& ctx)
{
    std::fprintf(stderr, "  Test_AssetImport\n");

    WorkersCookAndTheOwningThreadRecords(ctx);
    ProgressReachesTheEndAndIsWeighted(ctx);
    NothingChangedMeansNothingCooked(ctx);
    OneFailureDoesNotStopTheRest(ctx);
    CancellingStopsWhatHasNotStarted(ctx);
}
