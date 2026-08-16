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
#include <Fluxion/Assets/Assets.hpp>
#include <Fluxion/Core/Jobs/JobSystem.h>

#include <atomic>
#include <cstring>
#include <filesystem>

using namespace AssetTestSupport;

namespace
{

// A type WITH a device-side half, so the states that only such a type
// reaches can be looked at.
#define ASSET_TEST_UPLOADED_TYPE_NAME "TestUploaded"

// Atomic because the loading half runs on worker threads: `loads` is
// incremented from several at once, and a plain int would lose some of
// them. It did -- this check passed on one machine and failed on another
// until the counters were made atomic, which is what a data race looks
// like from the outside.
struct UploadCounters
{
    std::atomic<int> loads{ 0 };
    std::atomic<int> finalizes{ 0 };
    std::atomic<int> unloads{ 0 };
    bool failFinalize = false;
};

struct UploadedObject
{
    TestBlob blob;
    bool uploaded;
};

bool Uploaded_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    UploadCounters* counters = static_cast<UploadCounters*>(userData);
    counters->loads.fetch_add(1, std::memory_order_relaxed);

    if (size >= sizeof(TestBlob::text)) return false;

    UploadedObject* object =
        static_cast<UploadedObject*>(Fluxion_Allocator_Alloc(Fluxion_DefaultAllocator(), sizeof(UploadedObject), FLUXION_DEFAULT_ALIGNMENT));
    if (!object) return false;

    std::memcpy(object->blob.text, bytes, size);
    object->blob.text[size] = '\0';
    object->blob.length = size;
    object->uploaded = false;

    *outObject = object;
    return true;
}

bool Uploaded_Finalize(void* object, void* userData)
{
    UploadCounters* counters = static_cast<UploadCounters*>(userData);
    counters->finalizes.fetch_add(1, std::memory_order_relaxed);

    if (counters->failFinalize) return false;

    static_cast<UploadedObject*>(object)->uploaded = true;
    return true;
}

void Uploaded_Unload(void* object, void* userData)
{
    UploadCounters* counters = static_cast<UploadCounters*>(userData);
    counters->unloads.fetch_add(1, std::memory_order_relaxed);
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), object, sizeof(UploadedObject));
}

FluxionAssetTypeId UploadedTypeId()
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(ASSET_TEST_UPLOADED_TYPE_NAME));
}

FluxionAssetTypeDesc MakeUploadedTypeDesc(UploadCounters* counters)
{
    FluxionAssetTypeDesc desc{};
    std::memcpy(desc.name, ASSET_TEST_UPLOADED_TYPE_NAME, sizeof(ASSET_TEST_UPLOADED_TYPE_NAME));
    std::memcpy(desc.cookedExtension, "up", sizeof("up"));
    desc.load = Uploaded_Load;
    desc.finalize = Uploaded_Finalize;
    desc.unload = Uploaded_Unload;
    desc.userData = counters;
    return desc;
}

// Everything a loading test needs standing up: a directory with files in
// it, mounted, with the types registered.
struct Fixture
{
    bool ok = false;
    std::filesystem::path root;

    explicit Fixture(const char* name)
    {
        root = MakeDirectory(name);
        if (root.empty()) return;

        ok = Fluxion_AssetSystem_Init(nullptr);
        if (!ok) return;

        ok = Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create(root.string().c_str()));
    }

    ~Fixture() { Fluxion_AssetSystem_Shutdown(); }

    bool Write(const char* relative, const char* text) const { return WriteText(root / relative, text); }

    FluxionUUID Add(FluxionAssetTypeId type, const char* cookedRelative) const
    {
        const std::string cooked = std::string("assets://") + cookedRelative;

        FluxionAssetDesc desc{};
        desc.type = type;
        desc.name = cookedRelative;
        desc.cookedPath = cooked.c_str();

        FluxionUUID id{};
        Fluxion_AssetDatabase_Add(&desc, &id);
        return id;
    }
};

void LoadsWithoutAJobSystem(TestContext& ctx)
{
    Fixture fixture("load-inline");
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    FluxionAssetTypeDesc blob = MakeBlobTypeDesc();
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&blob));
    TEST_CHECK(ctx, fixture.Write("hello.blob", "hello"));

    const FluxionUUID id = fixture.Add(BlobTypeId(), "hello.blob");

    // Nothing was ever handed the work, so the caller does it. An asset
    // system used before the job system is started still hands back a
    // loaded asset rather than one stuck partway.
    TEST_CHECK(ctx, !Fluxion_JobSystem_IsInitialized());

    FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(handle));

    // No device-side half, so it never stops at cpu-ready.
    TEST_CHECK(ctx, Fluxion_Assets_GetState(handle) == FLUXION_ASSET_STATE_READY);

    const TestBlob* object = static_cast<const TestBlob*>(Fluxion_Assets_GetObject(handle));
    TEST_CHECK(ctx, object != nullptr);
    if (object) TEST_CHECK(ctx, std::strcmp(object->text, "hello") == 0);

    Fluxion_Assets_Release(handle);
    TEST_CHECK(ctx, Fluxion_Assets_GetLoadedCount() == 0);
}

void RefusesToStartWhatItCannotFind(TestContext& ctx)
{
    Fixture fixture("load-missing");
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    FluxionAssetTypeDesc blob = MakeBlobTypeDesc();
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&blob));

    // Not in the database at all.
    FluxionUUID unknown = Fluxion_UUID_Generate();
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Assets_Acquire(unknown)));

    // In the database, but of a type nothing registered -- which is what
    // an asset whose plugin did not load looks like.
    FluxionAssetDesc orphan{};
    orphan.type = Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr("NobodyRegisteredThis"));
    orphan.cookedPath = "assets://orphan.blob";
    FluxionUUID orphanId{};
    TEST_CHECK(ctx, Fluxion_AssetDatabase_Add(&orphan, &orphanId));
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Assets_Acquire(orphanId)));

    // In the database, of a known type, but the bytes are not there. This
    // one DOES start, so it reports itself failed rather than invalid --
    // the two are told apart on purpose.
    const FluxionUUID missing = fixture.Add(BlobTypeId(), "not-written.blob");
    FluxionAssetHandle handle = Fluxion_Assets_Acquire(missing);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(handle));
    TEST_CHECK(ctx, Fluxion_Assets_GetState(handle) == FLUXION_ASSET_STATE_FAILED);
    TEST_CHECK(ctx, Fluxion_Assets_GetObject(handle) == nullptr);
    Fluxion_Assets_Release(handle);
}

void OneAssetTwoHolders(TestContext& ctx)
{
    Fixture fixture("refcount");
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    UploadCounters counters;
    FluxionAssetTypeDesc uploaded = MakeUploadedTypeDesc(&counters);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&uploaded));
    TEST_CHECK(ctx, fixture.Write("shared.up", "shared"));

    const FluxionUUID id = fixture.Add(UploadedTypeId(), "shared.up");

    FluxionAssetHandle first = Fluxion_Assets_Acquire(id);
    FluxionAssetHandle second = Fluxion_Assets_Acquire(id);

    TEST_CHECK(ctx, first.index == second.index);
    TEST_CHECK(ctx, first.generation == second.generation);
    TEST_CHECK(ctx, Fluxion_Assets_GetReferenceCount(first) == 2);
    TEST_CHECK(ctx, Fluxion_Assets_GetLoadedCount() == 1);

    // Asked for twice, read once.
    TEST_CHECK(ctx, counters.loads == 1);

    // A type with a device-side half stops before it, and only a pump on
    // the owning thread carries it the rest of the way.
    TEST_CHECK(ctx, Fluxion_Assets_GetState(first) == FLUXION_ASSET_STATE_CPU_READY);
    TEST_CHECK(ctx, Fluxion_Assets_GetObject(first) == nullptr);
    TEST_CHECK(ctx, counters.finalizes == 0);

    Fluxion_Assets_Update();
    TEST_CHECK(ctx, counters.finalizes == 1);
    TEST_CHECK(ctx, Fluxion_Assets_GetState(first) == FLUXION_ASSET_STATE_READY);

    const UploadedObject* object = static_cast<const UploadedObject*>(Fluxion_Assets_GetObject(second));
    TEST_CHECK(ctx, object != nullptr);
    if (object) TEST_CHECK(ctx, object->uploaded);

    // Pumping again must not upload it a second time.
    Fluxion_Assets_Update();
    TEST_CHECK(ctx, counters.finalizes == 1);

    Fluxion_Assets_Release(first);
    TEST_CHECK(ctx, counters.unloads == 0);
    TEST_CHECK(ctx, Fluxion_Assets_GetReferenceCount(second) == 1);
    TEST_CHECK(ctx, Fluxion_Assets_GetObject(second) != nullptr);

    Fluxion_Assets_Release(second);
    TEST_CHECK(ctx, counters.unloads == 1);
    TEST_CHECK(ctx, Fluxion_Assets_GetLoadedCount() == 0);

    // The slot has been let go of, and the handles that named it must
    // stop naming it -- not name whatever moves in next.
    TEST_CHECK(ctx, Fluxion_Assets_GetState(first) == FLUXION_ASSET_STATE_UNLOADED);
    TEST_CHECK(ctx, Fluxion_Assets_GetObject(first) == nullptr);

    FluxionAssetHandle reacquired = Fluxion_Assets_Acquire(id);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(reacquired));
    TEST_CHECK(ctx, reacquired.index == first.index);
    TEST_CHECK(ctx, reacquired.generation != first.generation);
    TEST_CHECK(ctx, Fluxion_Assets_GetState(first) == FLUXION_ASSET_STATE_UNLOADED);
    TEST_CHECK(ctx, counters.loads == 2);
    Fluxion_Assets_Release(reacquired);
}

void AFailedUploadIsAFailedAsset(TestContext& ctx)
{
    Fixture fixture("upload-fails");
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    UploadCounters counters;
    counters.failFinalize = true;
    FluxionAssetTypeDesc uploaded = MakeUploadedTypeDesc(&counters);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&uploaded));
    TEST_CHECK(ctx, fixture.Write("bad.up", "bad"));

    const FluxionUUID id = fixture.Add(UploadedTypeId(), "bad.up");
    FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);

    Fluxion_Assets_Update();
    TEST_CHECK(ctx, Fluxion_Assets_GetState(handle) == FLUXION_ASSET_STATE_FAILED);
    TEST_CHECK(ctx, Fluxion_Assets_GetObject(handle) == nullptr);

    // The object was made even though the upload did not happen, so it
    // still has to be given back.
    Fluxion_Assets_Release(handle);
    TEST_CHECK(ctx, counters.unloads == 1);
}

// The same thing again, with the work actually going to worker threads.
void LoadsOnTheJobSystem(TestContext& ctx)
{
    Fixture fixture("load-jobs");
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    UploadCounters counters;
    FluxionAssetTypeDesc uploaded = MakeUploadedTypeDesc(&counters);
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&uploaded));

    FluxionUUID ids[8];
    for (int i = 0; i < 8; ++i)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "many%d.up", i);
        TEST_CHECK(ctx, fixture.Write(name, name));
        ids[i] = fixture.Add(UploadedTypeId(), name);
    }

    Fluxion_JobSystem_Init(4, false);

    FluxionAssetHandle handles[8];
    for (int i = 0; i < 8; ++i) handles[i] = Fluxion_Assets_Acquire(ids[i]);

    for (int i = 0; i < 8; ++i)
    {
        // Waiting on the owning thread for something that needs the
        // owning thread has to work: the wait pumps that step itself
        // rather than waiting for a pump that will never come.
        TEST_CHECK(ctx, Fluxion_Assets_Wait(handles[i]) == FLUXION_ASSET_STATE_READY);

        const UploadedObject* object = static_cast<const UploadedObject*>(Fluxion_Assets_GetObject(handles[i]));
        TEST_CHECK(ctx, object != nullptr);
        if (object)
        {
            char expected[32];
            std::snprintf(expected, sizeof(expected), "many%d.up", i);
            TEST_CHECK(ctx, std::strcmp(object->blob.text, expected) == 0);
            TEST_CHECK(ctx, object->uploaded);
        }
    }

    TEST_CHECK(ctx, counters.loads == 8);
    TEST_CHECK(ctx, counters.finalizes == 8);

    for (int i = 0; i < 8; ++i) Fluxion_Assets_Release(handles[i]);
    TEST_CHECK(ctx, counters.unloads == 8);

    Fluxion_JobSystem_Shutdown();
}

// The C++ wrapper exists for exactly one reason: the release happens
// whether or not the writer remembered it.
void TheCppWrapperLetsGo(TestContext& ctx)
{
    Fixture fixture("cpp-wrapper");
    if (!fixture.ok)
    {
        TEST_CHECK(ctx, false);
        return;
    }

    FluxionAssetTypeDesc blob = MakeBlobTypeDesc();
    TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&blob));
    TEST_CHECK(ctx, fixture.Write("scoped.blob", "scoped"));

    const FluxionUUID id = fixture.Add(BlobTypeId(), "scoped.blob");

    {
        Fluxion::Assets::Asset asset{ id };
        TEST_CHECK(ctx, asset.IsValid());
        TEST_CHECK(ctx, asset.IsReady());
        TEST_CHECK(ctx, Fluxion_Assets_GetLoadedCount() == 1);

        {
            Fluxion::Assets::Asset copy = asset;
            TEST_CHECK(ctx, Fluxion_Assets_GetReferenceCount(copy.Handle()) == 2);

            // A copy is a second holder of one asset, not a second copy
            // of it.
            TEST_CHECK(ctx, Fluxion_Assets_GetLoadedCount() == 1);
            TEST_CHECK(ctx, copy.Get<TestBlob>() == asset.Get<TestBlob>());
        }

        TEST_CHECK(ctx, Fluxion_Assets_GetReferenceCount(asset.Handle()) == 1);

        Fluxion::Assets::Asset moved = std::move(asset);
        TEST_CHECK(ctx, moved.IsValid());
        TEST_CHECK(ctx, !asset.IsValid());
        TEST_CHECK(ctx, Fluxion_Assets_GetReferenceCount(moved.Handle()) == 1);

        const TestBlob* object = moved.Get<TestBlob>();
        TEST_CHECK(ctx, object != nullptr);
        if (object) TEST_CHECK(ctx, std::strcmp(object->text, "scoped") == 0);
    }

    TEST_CHECK(ctx, Fluxion_Assets_GetLoadedCount() == 0);

    // An asset that could not even be asked for is a valid thing to hold
    // and does nothing when it goes away.
    {
        Fluxion::Assets::Asset nothing;
        TEST_CHECK(ctx, !nothing.IsValid());
        TEST_CHECK(ctx, nothing.Get<TestBlob>() == nullptr);
    }
}

// Shutting down releases what is still held, whatever the count says. A
// count that was already wrong is not a reason to leak.
void ShutdownReleasesWhatIsStillHeld(TestContext& ctx)
{
    UploadCounters counters;

    {
        Fixture fixture("shutdown");
        if (!fixture.ok)
        {
            TEST_CHECK(ctx, false);
            return;
        }

        FluxionAssetTypeDesc uploaded = MakeUploadedTypeDesc(&counters);
        TEST_CHECK(ctx, Fluxion_AssetTypes_Register(&uploaded));
        TEST_CHECK(ctx, fixture.Write("left.up", "left behind"));

        const FluxionUUID id = fixture.Add(UploadedTypeId(), "left.up");
        FluxionAssetHandle handle = Fluxion_Assets_Acquire(id);
        Fluxion_Assets_Acquire(id);
        TEST_CHECK(ctx, Fluxion_Assets_GetReferenceCount(handle) == 2);
    }

    TEST_CHECK(ctx, counters.unloads == 1);
}

} // namespace

void Test_AssetLoading_Run(TestContext& ctx)
{
    std::fprintf(stderr, "  Test_AssetLoading\n");

    LoadsWithoutAJobSystem(ctx);
    RefusesToStartWhatItCannotFind(ctx);
    OneAssetTwoHolders(ctx);
    AFailedUploadIsAFailedAsset(ctx);
    LoadsOnTheJobSystem(ctx);
    TheCppWrapperLetsGo(ctx);
    ShutdownReleasesWhatIsStillHeld(ctx);
}
