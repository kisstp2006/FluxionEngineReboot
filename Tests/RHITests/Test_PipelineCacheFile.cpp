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

#include "TestFramework.h"

#include <Fluxion/RHI/RHI.h>

#include <cstdio>
#include <string>
#include <vector>

// The file wrapper lives in the RHI's private tree: nothing outside the
// backends has any business writing one, so it is not published in a
// header. These checks only need to call it, and a declaration is enough
// for that -- the definitions are in the same library this test links.
struct FluxionRHIPipelineCacheIdentity
{
    FluxionRHIBackendType backend;
    u32 vendorId;
    u32 deviceId;
    u32 driverVersion;
    u64 extra;
};

bool Fluxion_RHIPipelineCacheFile_Write(const char* path, const FluxionRHIPipelineCacheIdentity& identity, const void* payload, usize payloadSize);
bool Fluxion_RHIPipelineCacheFile_Read(const char* path, const FluxionRHIPipelineCacheIdentity& identity, std::vector<u8>* outPayload);

namespace
{

const char* kPath = "Test_PipelineCacheFile.bin";

FluxionRHIPipelineCacheIdentity MakeIdentity()
{
    FluxionRHIPipelineCacheIdentity identity = {};
    identity.backend = FLUXION_RHI_BACKEND_VULKAN;
    identity.vendorId = 0x10DE;
    identity.deviceId = 0x2484;
    identity.driverVersion = 0x0001'0002;
    identity.extra = 0xABCDEF0123456789ull;
    return identity;
}

const std::vector<u8> kPayload = { 1, 2, 3, 4, 5, 6, 7, 8, 250, 251, 252, 253 };

// What a file this engine never wrote is filled with. Any value works so
// long as it is neither zero nor anything the header would hold, so that
// a file made of it is refused for what it is rather than for being
// empty.
const u8 kFillerByte = 0x5A;

std::vector<u8> ReadWholeFile(const char* path)
{
    std::vector<u8> bytes;
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr) return bytes;
#else
    file = fopen(path, "rb");
    if (file == nullptr) return bytes;
#endif
    u8 chunk[256];
    usize got = 0;
    while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0) bytes.insert(bytes.end(), chunk, chunk + got);
    fclose(file);
    return bytes;
}

bool WriteWholeFile(const char* path, const std::vector<u8>& bytes)
{
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) return false;
#else
    file = fopen(path, "wb");
    if (file == nullptr) return false;
#endif
    const bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    return ok;
}

} // namespace

extern "C" void Test_PipelineCacheFile_Run(TestContext* ctx)
{
    const FluxionRHIPipelineCacheIdentity identity = MakeIdentity();

    // The round trip first: without this, every rejection below could be
    // passing because nothing is ever accepted.
    TEST_CHECK(ctx, Fluxion_RHIPipelineCacheFile_Write(kPath, identity, kPayload.data(), kPayload.size()));

    std::vector<u8> readBack;
    TEST_CHECK(ctx, Fluxion_RHIPipelineCacheFile_Read(kPath, identity, &readBack));
    TEST_CHECK(ctx, readBack == kPayload);

    const std::vector<u8> good = ReadWholeFile(kPath);
    TEST_CHECK(ctx, good.size() > kPayload.size()); // header actually present

    // A file this engine never wrote. This is the case that used to load
    // as a success on two backends: the driver quietly ignores bytes it
    // cannot use and reports the cache as created either way, so a caller
    // could not tell a warm start from a cold one.
    std::vector<u8> foreign;
    foreign.assign(good.size(), kFillerByte);
    TEST_CHECK(ctx, WriteWholeFile(kPath, foreign));
    TEST_CHECK(ctx, !Fluxion_RHIPipelineCacheFile_Read(kPath, identity, &readBack));

    // Each identity field on its own has to be enough to refuse the file.
    // Checking only one would leave the others free to be dropped from
    // the comparison without any test noticing.
    struct
    {
        const char* what;
        FluxionRHIPipelineCacheIdentity value;
    } mismatches[] = {
        { "backend", identity }, { "vendor", identity }, { "device", identity },
        { "driver", identity }, { "extra", identity },
    };
    mismatches[0].value.backend = FLUXION_RHI_BACKEND_D3D12;
    mismatches[1].value.vendorId ^= 1u;
    mismatches[2].value.deviceId ^= 1u;
    mismatches[3].value.driverVersion ^= 1u;
    mismatches[4].value.extra ^= 1ull;

    for (const auto& mismatch : mismatches)
    {
        TEST_CHECK(ctx, WriteWholeFile(kPath, good));
        TEST_CHECK(ctx, !Fluxion_RHIPipelineCacheFile_Read(kPath, mismatch.value, &readBack));
    }

    // Damage inside the payload, with every identity field still correct.
    // Only the hash can catch this one -- a half-synced or truncated
    // write produces exactly this shape.
    std::vector<u8> damaged = good;
    damaged[damaged.size() - 1] ^= 0xFFu;
    TEST_CHECK(ctx, WriteWholeFile(kPath, damaged));
    TEST_CHECK(ctx, !Fluxion_RHIPipelineCacheFile_Read(kPath, identity, &readBack));

    // A short file: the header promises more payload than is there.
    std::vector<u8> truncated = good;
    truncated.pop_back();
    TEST_CHECK(ctx, WriteWholeFile(kPath, truncated));
    TEST_CHECK(ctx, !Fluxion_RHIPipelineCacheFile_Read(kPath, identity, &readBack));

    // And a long one. Appending is the easier accident of the two -- a
    // reader that stops at payloadSize would accept this happily and hand
    // back bytes from a file that is not what it claims to be.
    std::vector<u8> extended = good;
    extended.push_back(0x00);
    TEST_CHECK(ctx, WriteWholeFile(kPath, extended));
    TEST_CHECK(ctx, !Fluxion_RHIPipelineCacheFile_Read(kPath, identity, &readBack));

    // A header cut off mid-way, which must not read past what was read.
    TEST_CHECK(ctx, WriteWholeFile(kPath, std::vector<u8>(good.begin(), good.begin() + 8)));
    TEST_CHECK(ctx, !Fluxion_RHIPipelineCacheFile_Read(kPath, identity, &readBack));

    // An empty payload is a legitimate thing to store, and must survive
    // the round trip rather than being confused with failure.
    TEST_CHECK(ctx, Fluxion_RHIPipelineCacheFile_Write(kPath, identity, nullptr, 0));
    readBack.assign(1, 0xFFu);
    TEST_CHECK(ctx, Fluxion_RHIPipelineCacheFile_Read(kPath, identity, &readBack));
    TEST_CHECK(ctx, readBack.empty());

    // Reading a path that does not exist is a cold start, not a crash.
    TEST_CHECK(ctx, !Fluxion_RHIPipelineCacheFile_Read("Test_PipelineCacheFile_missing.bin", identity, &readBack));

    std::remove(kPath);
}
