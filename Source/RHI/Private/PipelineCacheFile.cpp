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

#include "PipelineCacheFile.h"

#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Log.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{

// Spelled out rather than written as the number it comes to, so the
// value and the four characters it stands for cannot drift apart -- a
// hex constant with a comment saying what it spells is only correct
// until someone edits one of the two.
constexpr u32 FourCC(char a, char b, char c, char d)
{
    return (u32)(u8)a | ((u32)(u8)b << 8) | ((u32)(u8)c << 16) | ((u32)(u8)d << 24);
}

constexpr u32 kMagic = FourCC('F', 'H', 'P', 'C');

// Bumped whenever the framing below changes in a way that would make an
// older file parse into something wrong rather than fail outright. A
// mismatch is not an error -- it means "written by a different build",
// which is exactly what starting cold is for.
constexpr u32 kFormatVersion = 1;

// Everything the header carries, in the order it is written. Reading and
// writing both walk this sequentially: a field inserted in the middle
// moves everything after it, and hand-written byte offsets would keep
// compiling while quietly pointing at the wrong thing.
struct Header
{
    u32 magic = kMagic;
    u32 formatVersion = kFormatVersion;
    u32 backend = 0;
    u32 vendorId = 0;
    u32 deviceId = 0;
    u32 driverVersion = 0;
    u64 extra = 0;
    u64 payloadSize = 0;
    u64 payloadHash = 0;
};

constexpr usize kHeaderSize = 6 * sizeof(u32) + 3 * sizeof(u64);

// Explicit little-endian, one byte at a time. The same reasoning as the
// shader cache's own framing: a struct written wholesale would carry this
// compiler's padding and this machine's byte order into a file that
// another build is expected to read.
struct Cursor
{
    u8* bytes;
    usize at = 0;

    void U32(u32 value)
    {
        bytes[at++] = (u8)(value & 0xFFu);
        bytes[at++] = (u8)((value >> 8) & 0xFFu);
        bytes[at++] = (u8)((value >> 16) & 0xFFu);
        bytes[at++] = (u8)((value >> 24) & 0xFFu);
    }
    void U64(u64 value)
    {
        U32((u32)(value & 0xFFFFFFFFull));
        U32((u32)((value >> 32) & 0xFFFFFFFFull));
    }
};

struct ConstCursor
{
    const u8* bytes;
    usize at = 0;

    u32 U32()
    {
        const u32 value = (u32)bytes[at] | ((u32)bytes[at + 1] << 8) | ((u32)bytes[at + 2] << 16) | ((u32)bytes[at + 3] << 24);
        at += 4;
        return value;
    }
    u64 U64()
    {
        const u64 low = U32();
        return low | ((u64)U32() << 32);
    }
};

void WriteHeader(u8* out, const Header& header)
{
    Cursor cursor{ out };
    cursor.U32(header.magic);
    cursor.U32(header.formatVersion);
    cursor.U32(header.backend);
    cursor.U32(header.vendorId);
    cursor.U32(header.deviceId);
    cursor.U32(header.driverVersion);
    cursor.U64(header.extra);
    cursor.U64(header.payloadSize);
    cursor.U64(header.payloadHash);
}

// The caller has already established that kHeaderSize bytes are there,
// which is what makes walking without per-field bounds checks safe here.
Header ReadHeader(const u8* in)
{
    ConstCursor cursor{ in };
    Header header;
    header.magic = cursor.U32();
    header.formatVersion = cursor.U32();
    header.backend = cursor.U32();
    header.vendorId = cursor.U32();
    header.deviceId = cursor.U32();
    header.driverVersion = cursor.U32();
    header.extra = cursor.U64();
    header.payloadSize = cursor.U64();
    header.payloadHash = cursor.U64();
    return header;
}

bool SameDevice(const Header& header, const FluxionRHIPipelineCacheIdentity& identity)
{
    return header.backend == (u32)identity.backend &&
           header.vendorId == identity.vendorId &&
           header.deviceId == identity.deviceId &&
           header.driverVersion == identity.driverVersion &&
           header.extra == identity.extra;
}

// Two processes writing the same cache path at once would otherwise pick
// the same temporary and rename each other's half-written file into
// place. The pid makes the temporary the writer's own.
std::string TemporaryPathFor(const char* path)
{
    std::string temporary = path;
    temporary += ".writing.";
#if defined(_WIN32)
    temporary += std::to_string((unsigned long)_getpid());
#else
    temporary += std::to_string((unsigned long)getpid());
#endif
    return temporary;
}

} // namespace

bool Fluxion_RHIPipelineCacheFile_Write(const char* path, const FluxionRHIPipelineCacheIdentity& identity, const void* payload, usize payloadSize)
{
    if (path == nullptr || (payload == nullptr && payloadSize != 0)) return false;

    Header fields;
    fields.backend = (u32)identity.backend;
    fields.vendorId = identity.vendorId;
    fields.deviceId = identity.deviceId;
    fields.driverVersion = identity.driverVersion;
    fields.extra = identity.extra;
    fields.payloadSize = (u64)payloadSize;
    fields.payloadHash = Fluxion_HashBytes64(payload, payloadSize);

    u8 header[kHeaderSize];
    WriteHeader(header, fields);

    const std::string temporary = TemporaryPathFor(path);

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, temporary.c_str(), "wb") != 0 || file == nullptr) return false;
#else
    file = fopen(temporary.c_str(), "wb");
    if (file == nullptr) return false;
#endif

    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    if (ok && payloadSize != 0) ok = fwrite(payload, 1, payloadSize, file) == payloadSize;

    // fclose can fail where every fwrite succeeded -- buffered bytes only
    // reach the disk here, so a full volume surfaces at this call and
    // nowhere earlier. Renaming a short file over a good one because
    // nobody looked is exactly the corruption this whole path exists to
    // avoid.
    if (fclose(file) != 0) ok = false;

    if (!ok)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool Fluxion_RHIPipelineCacheFile_Read(const char* path, const FluxionRHIPipelineCacheIdentity& identity, std::vector<u8>* outPayload)
{
    if (path == nullptr || outPayload == nullptr) return false;

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr) return false;
#else
    file = fopen(path, "rb");
    if (file == nullptr) return false;
#endif

    u8 header[kHeaderSize];
    if (fread(header, 1, sizeof(header), file) != sizeof(header))
    {
        fclose(file);
        return false;
    }

    const Header fields = ReadHeader(header);
    if (fields.magic != kMagic || fields.formatVersion != kFormatVersion)
    {
        fclose(file);
        return false;
    }

    // A blob from another backend or another GPU is not merely useless --
    // it is the case the drivers handle inconsistently, which is the
    // reason this file exists. Refused here, before any driver sees it.
    if (!SameDevice(fields, identity))
    {
        fclose(file);
        FLUXION_LOG_INFO("RHI", "Pipeline cache '%s' was written by a different backend, adapter, or driver -- starting cold.", path);
        return false;
    }

    const u64 payloadSize = fields.payloadSize;
    const u64 payloadHash = fields.payloadHash;

    // The size is read from the file, so it is attacker-shaped input as
    // far as this code is concerned: allocating on it before knowing the
    // bytes exist is how a one-byte edit turns into a huge allocation.
    if (payloadSize > (u64)SIZE_MAX)
    {
        fclose(file);
        return false;
    }

    std::vector<u8> payload((usize)payloadSize);
    const usize readBytes = payloadSize == 0 ? 0 : fread(payload.data(), 1, (usize)payloadSize, file);

    // Trailing bytes mean the file is not what its own header says it is.
    u8 trailing = 0;
    const bool hasTrailing = fread(&trailing, 1, 1, file) == 1;
    fclose(file);

    if (readBytes != (usize)payloadSize || hasTrailing) return false;

    if (Fluxion_HashBytes64(payload.data(), payload.size()) != payloadHash)
    {
        FLUXION_LOG_WARN("RHI", "Pipeline cache '%s' is damaged -- starting cold.", path);
        return false;
    }

    *outPayload = std::move(payload);
    return true;
}
