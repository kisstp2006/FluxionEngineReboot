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

#pragma once

#include <Fluxion/Assets/AssetSystem.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Platform/File.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// What every test here needs: somewhere on disk to put files, and a
// couple of asset types simple enough that a test failing says something
// about the asset system rather than about the type.

namespace AssetTestSupport
{

// A directory of its own per case, emptied first, so one case cannot see
// what another left behind.
inline std::filesystem::path MakeDirectory(const char* name)
{
    std::error_code error;

    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error) return std::filesystem::path();

    const std::filesystem::path directory = root / "FluxionAssetTests" / name;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);

    return error ? std::filesystem::path() : directory;
}

// Through the engine's own file interface rather than the C library's,
// so these tests put files on disk the same way the thing they are
// testing does.
inline bool WriteFile(const std::filesystem::path& path, const void* bytes, std::size_t size)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;

    FluxionFile file;
    if (!Fluxion_Platform_FileOpen(&file, path.string().c_str(), FLUXION_FILE_OPEN_WRITE)) return false;

    bool ok = true;
    std::size_t written = 0;
    while (written < size)
    {
        const usize step = Fluxion_Platform_FileWrite(&file, static_cast<const u8*>(bytes) + written, size - written);
        if (step == 0)
        {
            ok = false;
            break;
        }
        written += step;
    }

    Fluxion_Platform_FileClose(&file);
    return ok;
}

inline std::vector<u8> ReadWholeFile(const std::filesystem::path& path)
{
    std::vector<u8> bytes;

    FluxionFile file;
    if (!Fluxion_Platform_FileOpen(&file, path.string().c_str(), FLUXION_FILE_OPEN_READ)) return bytes;

    const i64 size = Fluxion_Platform_FileSize(&file);
    if (size > 0)
    {
        bytes.resize(static_cast<std::size_t>(size));

        std::size_t filled = 0;
        while (filled < bytes.size())
        {
            const usize step = Fluxion_Platform_FileRead(&file, bytes.data() + filled, bytes.size() - filled);
            if (step == 0)
            {
                bytes.clear();
                break;
            }
            filled += step;
        }
    }

    Fluxion_Platform_FileClose(&file);
    return bytes;
}

inline bool WriteText(const std::filesystem::path& path, const char* text)
{
    return WriteFile(path, text, std::strlen(text));
}

// ---------------------------------------------------------------------
// A type with only a load half, which is what almost everything is.
// ---------------------------------------------------------------------

#define ASSET_TEST_BLOB_TYPE_NAME "TestBlob"

struct TestBlob
{
    char text[128];
    std::size_t length;
};

inline bool TestBlob_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    (void)userData;

    if (size >= sizeof(TestBlob::text)) return false;

    TestBlob* blob = static_cast<TestBlob*>(Fluxion_Allocator_Alloc(Fluxion_DefaultAllocator(), sizeof(TestBlob), FLUXION_DEFAULT_ALIGNMENT));
    if (!blob) return false;

    std::memcpy(blob->text, bytes, size);
    blob->text[size] = '\0';
    blob->length = size;

    *outObject = blob;
    return true;
}

inline void TestBlob_Unload(void* object, void* userData)
{
    (void)userData;
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), object, sizeof(TestBlob));
}

inline FluxionAssetTypeDesc MakeBlobTypeDesc(FluxionAssetShipPolicy policy = FLUXION_ASSET_SHIP_COOKED)
{
    FluxionAssetTypeDesc desc{};
    std::memcpy(desc.name, ASSET_TEST_BLOB_TYPE_NAME, sizeof(ASSET_TEST_BLOB_TYPE_NAME));
    std::memcpy(desc.cookedExtension, "blob", sizeof("blob"));
    desc.defaultShipPolicy = policy;
    desc.load = TestBlob_Load;
    desc.unload = TestBlob_Unload;
    return desc;
}

inline FluxionAssetTypeId BlobTypeId()
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(ASSET_TEST_BLOB_TYPE_NAME));
}

// ---------------------------------------------------------------------
// A type with BOTH halves, which is the case the shipping rules are for.
//
// Its source form is text; its cooked form is the same text with a marker
// in front. That is enough to tell the two apart in a finished package by
// looking, which is exactly what the package tests do.
// ---------------------------------------------------------------------

#define ASSET_TEST_MODEL_TYPE_NAME "TestModel"
#define ASSET_TEST_MODEL_COOKED_MARKER "COOKED:"

inline bool TestModel_Import(const u8* sourceBytes, usize sourceSize, FluxionStream* cookedOut, void* userData)
{
    (void)userData;

    char marker[] = ASSET_TEST_MODEL_COOKED_MARKER;
    Fluxion_Stream_SerializeBytes(cookedOut, marker, sizeof(marker) - 1);

    // The stream writes through a mutable pointer; this direction only
    // reads from the source.
    Fluxion_Stream_SerializeBytes(cookedOut, const_cast<u8*>(sourceBytes), sourceSize);
    return !Fluxion_Stream_HasOverflowed(cookedOut);
}

inline bool TestModel_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    const usize markerLength = sizeof(ASSET_TEST_MODEL_COOKED_MARKER) - 1;

    // A loader that accepted the source form too would make the whole
    // question moot: the point is that only the cooked form is loadable,
    // and the source form is not something a shipped game can use.
    if (size < markerLength) return false;
    if (std::memcmp(bytes, ASSET_TEST_MODEL_COOKED_MARKER, markerLength) != 0) return false;

    return TestBlob_Load(bytes + markerLength, size - markerLength, outObject, userData);
}

inline FluxionAssetTypeDesc MakeModelTypeDesc(bool withImportHalf)
{
    FluxionAssetTypeDesc desc{};
    std::memcpy(desc.name, ASSET_TEST_MODEL_TYPE_NAME, sizeof(ASSET_TEST_MODEL_TYPE_NAME));
    std::memcpy(desc.cookedExtension, "tmesh", sizeof("tmesh"));
    desc.defaultShipPolicy = FLUXION_ASSET_SHIP_COOKED;
    desc.load = TestModel_Load;
    desc.unload = TestBlob_Unload;

    // A built game has no importer plugin loaded, and this is what that
    // looks like from here: the same type, with the import half absent.
    if (withImportHalf)
    {
        std::memcpy(desc.sourceExtensions[0], "tsrc", sizeof("tsrc"));
        desc.sourceExtensionCount = 1;
        desc.import = TestModel_Import;
    }

    return desc;
}

inline FluxionAssetTypeId ModelTypeId()
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(ASSET_TEST_MODEL_TYPE_NAME));
}

} // namespace AssetTestSupport
