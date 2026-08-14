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
    TEST_CHECK(ctx, WriteWholeFile(kPath, std::vector<u8>(good.size(), 0x5A)));
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
