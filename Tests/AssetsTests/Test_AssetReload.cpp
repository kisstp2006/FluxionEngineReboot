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
#include <Fluxion/Assets/AssetSystem.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Platform/Time.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>

using namespace AssetTestSupport;

// A cooked file that changes while something is holding it.
//
// The promise being checked is narrow and worth stating: THE HANDLE DOES
// NOT CHANGE. Everything holding a reference goes on holding the same
// one, and what it gets back from that reference is the new thing. No
// asset type does anything to make this work, and no holder does either.
//
// The other half is that it is never worse than not reloading at all: a
// file that will not load leaves what was already there in place.

namespace
{

// Atomic for the same reason the loading tests' counters are: the reading
// half runs on a worker thread when one exists.
struct ReloadCounters
{
    std::atomic<int> loads{ 0 };
    std::atomic<int> finalizes{ 0 };
    std::atomic<int> unloads{ 0 };
    bool failFinalize = false;
};

#define ASSET_TEST_RELOAD_TYPE_NAME "TestReloadable"

bool Reloadable_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    ReloadCounters* counters = static_cast<ReloadCounters*>(userData);
    counters->loads.fetch_add(1, std::memory_order_relaxed);

    // Anything starting with this is refused, which is how a case asks
    // for a reload that fails without needing a file the file system
    // itself objects to.
    if (size >= 3 && std::memcmp(bytes, "BAD", 3) == 0) return false;

    return TestBlob_Load(bytes, size, outObject, nullptr);
}

bool Reloadable_Finalize(void* object, void* userData)
{
    ReloadCounters* counters = static_cast<ReloadCounters*>(userData);
    counters->finalizes.fetch_add(1, std::memory_order_relaxed);
    (void)object;
    return !counters->failFinalize;
}

void Reloadable_Unload(void* object, void* userData)
{
    ReloadCounters* counters = static_cast<ReloadCounters*>(userData);
    counters->unloads.fetch_add(1, std::memory_order_relaxed);
    TestBlob_Unload(object, nullptr);
}

FluxionAssetTypeId ReloadableTypeId()
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(ASSET_TEST_RELOAD_TYPE_NAME));
}

FluxionAssetTypeDesc MakeReloadableTypeDesc(ReloadCounters* counters, bool withFinalize)
{
    FluxionAssetTypeDesc desc{};
    std::memcpy(desc.name, ASSET_TEST_RELOAD_TYPE_NAME, sizeof(ASSET_TEST_RELOAD_TYPE_NAME));
    std::memcpy(desc.cookedExtension, "rel", sizeof("rel"));
    desc.load = Reloadable_Load;
    desc.finalize = withFinalize ? Reloadable_Finalize : nullptr;
    desc.unload = Reloadable_Unload;
    desc.userData = counters;
    return desc;
}

struct Fixture
{
    bool ok = false;
    std::filesystem::path root;
    ReloadCounters counters;

    Fixture(const char* name, bool withFinalize)
    {
        root = MakeDirectory(name);
        if (root.empty()) return;

        ok = Fluxion_AssetSystem_Init(nullptr);
        if (!ok) return;

        ok = Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create(root.string().c_str()));
        if (!ok) return;

        FluxionAssetTypeDesc type = MakeReloadableTypeDesc(&counters, withFinalize);
        ok = Fluxion_AssetTypes_Register(&type);
    }

    ~Fixture() { Fluxion_AssetSystem_Shutdown(); }

    bool Write(const char* relative, const char* text) const { return WriteText(root / relative, text); }

    FluxionUUID Add(const char* cookedRelative) const
    {
        const std::string cooked = std::string("assets://") + cookedRelative;

        FluxionAssetDesc desc{};
        desc.type = ReloadableTypeId();
        desc.name = cookedRelative;
        desc.cookedPath = cooked.c_str();

        FluxionUUID id{};
        Fluxion_AssetDatabase_Add(&desc, &id);
        return id;
    }
};

// A poll, then whatever it started brought to a finish.
//
// Two calls rather than one because they are two different things: the
// first notices the file and sets the reading going, the second waits for
// it and puts the result in. Outside a test, the second is simply the
// next frame.
void PollAndSettle(FluxionAssetHandle handle)
{
    // Long enough that the interval below has certainly gone by. The
    // interval is one millisecond, so this is not a race dressed up as a
    // wait -- it is twenty times the thing being waited for.
    Fluxion_Platform_SleepMilliseconds(20);

    Fluxion_Assets_Update();
    Fluxion_Assets_Wait(handle);
}

const TestBlob* ObjectOf(FluxionAssetHandle handle)
{
    return static_cast<const TestBlob*>(Fluxion_Assets_GetObject(handle));
}

// ---------------------------------------------------------------------

void AChangedFileIsReadAgainBehindTheSameHandle(TestContext& ctx)
{
    Fixture fixture("reload-basic", false);
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    TEST_CHECK(ctx, fixture.Write("thing.rel", "first"));
    const FluxionUUID id = fixture.Add("thing.rel");

    Fluxion_Assets_SetWatchInterval(1);

    const FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);
    TEST_CHECK(ctx, Fluxion_Assets_Wait(handle) == FLUXION_ASSET_STATE_READY);

    const TestBlob* before = ObjectOf(handle);
    TEST_CHECK(ctx, before != nullptr && std::strcmp(before->text, "first") == 0);
    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 0);

    // A different length as well as different contents. The file's
    // revision is worked out from when it was written and how large it
    // is, so a rewrite of exactly the same length within the same tick of
    // the clock is the one change this cannot see -- and a test that
    // depended on catching it would be a test that sometimes fails.
    TEST_CHECK(ctx, fixture.Write("thing.rel", "second version"));

    PollAndSettle(handle);

    // The same handle. Not "a handle that compares equal" -- the same
    // one, never given back and never asked for again, which is what lets
    // a component hold a reference and know nothing about any of this.
    const TestBlob* after = ObjectOf(handle);
    TEST_CHECK(ctx, after != nullptr && std::strcmp(after->text, "second version") == 0);
    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 1);

    // Ready throughout. It is never unloaded and loaded again -- that
    // would be a hole, and a frame landing in it would have nothing to
    // draw.
    TEST_CHECK(ctx, Fluxion_Assets_GetState(handle) == FLUXION_ASSET_STATE_READY);

    // Exactly one let go of: the version being replaced, and nothing
    // else. Two would mean the new one was thrown away as well.
    TEST_CHECK(ctx, fixture.counters.unloads.load() == 1);
    TEST_CHECK(ctx, fixture.counters.loads.load() == 2);

    Fluxion_Assets_Release(handle);
}

void AFileThatDidNotChangeIsLeftAlone(TestContext& ctx)
{
    Fixture fixture("reload-unchanged", false);
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    TEST_CHECK(ctx, fixture.Write("thing.rel", "steady"));
    const FluxionUUID id = fixture.Add("thing.rel");

    Fluxion_Assets_SetWatchInterval(1);

    const FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);
    TEST_CHECK(ctx, Fluxion_Assets_Wait(handle) == FLUXION_ASSET_STATE_READY);

    for (int i = 0; i < 5; ++i) PollAndSettle(handle);

    // Five polls, no reads. Watching that reloaded a file nobody touched
    // would be worse than no watching at all: every asset in the game
    // would be rebuilt several times a second.
    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 0);
    TEST_CHECK(ctx, fixture.counters.loads.load() == 1);
    TEST_CHECK(ctx, fixture.counters.unloads.load() == 0);

    Fluxion_Assets_Release(handle);
}

void AReloadThatFailsKeepsWhatWasAlreadyThere(TestContext& ctx)
{
    Fixture fixture("reload-failed", false);
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    TEST_CHECK(ctx, fixture.Write("thing.rel", "good one"));
    const FluxionUUID id = fixture.Add("thing.rel");

    Fluxion_Assets_SetWatchInterval(1);

    const FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);
    TEST_CHECK(ctx, Fluxion_Assets_Wait(handle) == FLUXION_ASSET_STATE_READY);

    // Saved halfway through an edit, or written by a tool that crashed.
    TEST_CHECK(ctx, fixture.Write("thing.rel", "BAD contents that will not load"));

    PollAndSettle(handle);

    // Still the old one, still ready, and nothing let go of. A failed
    // reload marking the asset failed would take a working picture off
    // the screen because a file was caught mid-save.
    const TestBlob* object = ObjectOf(handle);
    TEST_CHECK(ctx, object != nullptr && std::strcmp(object->text, "good one") == 0);
    TEST_CHECK(ctx, Fluxion_Assets_GetState(handle) == FLUXION_ASSET_STATE_READY);
    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 0);
    TEST_CHECK(ctx, fixture.counters.unloads.load() == 0);

    const int attemptsAfterFailure = fixture.counters.loads.load();

    // And it is not tried again and again. The file has not changed since
    // it failed, so there is nothing new to try -- a version that
    // forgot this would read a broken file five times a second and fill
    // the log with the same message for the rest of the run.
    for (int i = 0; i < 3; ++i) PollAndSettle(handle);
    TEST_CHECK(ctx, fixture.counters.loads.load() == attemptsAfterFailure);

    // Fixing the file does bring it back, which is what makes the rule
    // above a pause rather than a giving up.
    TEST_CHECK(ctx, fixture.Write("thing.rel", "repaired"));
    PollAndSettle(handle);

    const TestBlob* repaired = ObjectOf(handle);
    TEST_CHECK(ctx, repaired != nullptr && std::strcmp(repaired->text, "repaired") == 0);
    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 1);

    Fluxion_Assets_Release(handle);
}

void TheDeviceSideStepRunsOnWhatWasJustRead(TestContext& ctx)
{
    Fixture fixture("reload-finalize", true);
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    TEST_CHECK(ctx, fixture.Write("thing.rel", "first"));
    const FluxionUUID id = fixture.Add("thing.rel");

    Fluxion_Assets_SetWatchInterval(1);

    const FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);
    TEST_CHECK(ctx, Fluxion_Assets_Wait(handle) == FLUXION_ASSET_STATE_READY);
    TEST_CHECK(ctx, fixture.counters.finalizes.load() == 1);

    TEST_CHECK(ctx, fixture.Write("thing.rel", "second version"));
    PollAndSettle(handle);

    // The new one went through the device-side step too. Skipping it
    // would hand back a texture whose pixels no device has ever been
    // given -- which is not a state anything downstream is prepared for.
    TEST_CHECK(ctx, fixture.counters.finalizes.load() == 2);
    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 1);

    // A refusal at THAT step is treated the same as a refusal at the
    // reading step: the old one stays.
    fixture.counters.failFinalize = true;
    TEST_CHECK(ctx, fixture.Write("thing.rel", "a third and longer version"));
    PollAndSettle(handle);

    const TestBlob* object = ObjectOf(handle);
    TEST_CHECK(ctx, object != nullptr && std::strcmp(object->text, "second version") == 0);
    TEST_CHECK(ctx, Fluxion_Assets_GetState(handle) == FLUXION_ASSET_STATE_READY);
    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 1);

    // The one that could not be finished was let go of rather than left
    // lying about -- and the one in use was NOT.
    TEST_CHECK(ctx, fixture.counters.unloads.load() == 2);

    Fluxion_Assets_Release(handle);
}

void NothingIsLookedAtWhenWatchingIsOff(TestContext& ctx)
{
    Fixture fixture("reload-off", false);
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    TEST_CHECK(ctx, fixture.Write("thing.rel", "first"));
    const FluxionUUID id = fixture.Add("thing.rel");

    Fluxion_Assets_SetWatchInterval(0);
    TEST_CHECK(ctx, Fluxion_Assets_GetWatchInterval() == 0);

    const FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);
    TEST_CHECK(ctx, Fluxion_Assets_Wait(handle) == FLUXION_ASSET_STATE_READY);

    TEST_CHECK(ctx, fixture.Write("thing.rel", "second version"));
    for (int i = 0; i < 3; ++i) PollAndSettle(handle);

    const TestBlob* object = ObjectOf(handle);
    TEST_CHECK(ctx, object != nullptr && std::strcmp(object->text, "first") == 0);
    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 0);

    Fluxion_Assets_Release(handle);

    // Put back, or every case after this one inherits a switched-off
    // asset system and passes for the wrong reason.
    Fluxion_Assets_SetWatchInterval(200);
}

// ---------------------------------------------------------------------

// A source whose files cannot change, which is what a shipped package is.
//
// Written out here rather than using a real package, because what is
// being checked is the ABSENCE of the revision function -- and the point
// is that a source which does not have one is never asked a second time,
// however much its contents move underneath it.
struct FrozenSource
{
    FluxionVfsSource base;
    std::string contents;
    int existsCalls;
};

bool FrozenSource_Exists(FluxionVfsSource* self, const char* path)
{
    (void)path;
    ++static_cast<FrozenSource*>(static_cast<void*>(self))->existsCalls;
    return true;
}

u8* FrozenSource_ReadAll(FluxionVfsSource* self, const char* path, usize* outSize)
{
    (void)path;
    FrozenSource* source = static_cast<FrozenSource*>(static_cast<void*>(self));

    const usize size = source->contents.size();
    u8* bytes = static_cast<u8*>(Fluxion_Allocator_Alloc(Fluxion_Vfs_GetAllocator(), size, FLUXION_DEFAULT_ALIGNMENT));
    if (!bytes) return nullptr;

    std::memcpy(bytes, source->contents.data(), size);
    if (outSize) *outSize = size;
    return bytes;
}

void FrozenSource_Destroy(FluxionVfsSource* self)
{
    delete static_cast<FrozenSource*>(static_cast<void*>(self));
}

const FluxionVfsSourceVTable kFrozenVTable = {
    FrozenSource_Exists,
    FrozenSource_ReadAll,
    nullptr,
    nullptr,
    FrozenSource_Destroy,

    // The whole point of this source. No revision function means nothing
    // about it can change, so nothing about it is ever looked at again.
    nullptr,
};

void ASourceThatCannotChangeIsNeverLookedAt(TestContext& ctx)
{
    const std::filesystem::path root = MakeDirectory("reload-frozen");
    if (root.empty())
    {
        TEST_CHECK(ctx, false);
        return;
    }

    if (!Fluxion_AssetSystem_Init(nullptr))
    {
        TEST_CHECK(ctx, false);
        return;
    }

    ReloadCounters counters;
    FluxionAssetTypeDesc type = MakeReloadableTypeDesc(&counters, false);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&type));

    FrozenSource* source = new FrozenSource();
    source->base.vtable = &kFrozenVTable;
    source->contents = "sealed";
    source->existsCalls = 0;

    TEST_CHECK(ctx, Fluxion_Vfs_Mount("assets", &source->base));

    FluxionAssetDesc desc{};
    desc.type = ReloadableTypeId();
    desc.name = "sealed";
    desc.cookedPath = "assets://sealed.rel";

    FluxionUUID id{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&desc, &id));

    Fluxion_Assets_SetWatchInterval(1);

    const FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);
    TEST_CHECK(ctx, Fluxion_Assets_Wait(handle) == FLUXION_ASSET_STATE_READY);

    const int existsAfterLoad = source->existsCalls;

    // The contents move, which a real package's never would -- so if
    // anything did poll this source, this is what it would notice.
    source->contents = "something else entirely";

    for (int i = 0; i < 5; ++i) PollAndSettle(handle);

    TEST_CHECK(ctx, Fluxion_Assets_GetReloadCount(handle) == 0);
    TEST_CHECK(ctx, counters.loads.load() == 1);

    // And it was not even ASKED. That is the part that makes watching
    // free in a built game rather than merely harmless: the polling loop
    // skips the asset entirely, so there is no call to answer.
    TEST_CHECK(ctx, source->existsCalls == existsAfterLoad);

    Fluxion_Assets_Release(handle);
    Fluxion_AssetSystem_Shutdown();
}

} // namespace

void Test_AssetReload_Run(TestContext& ctx)
{
    AChangedFileIsReadAgainBehindTheSameHandle(ctx);
    AFileThatDidNotChangeIsLeftAlone(ctx);
    AReloadThatFailsKeepsWhatWasAlreadyThere(ctx);
    TheDeviceSideStepRunsOnWhatWasJustRead(ctx);
    NothingIsLookedAtWhenWatchingIsOff(ctx);
    ASourceThatCannotChangeIsNeverLookedAt(ctx);
}
