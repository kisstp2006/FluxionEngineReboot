#include "TestFramework.h"

#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/RenderCore/Renderer/TextureAsset.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

// A 4x4 texture with every level down to one pixel. Small enough to work
// out by hand, and wide enough that the levels really do differ.
constexpr u32 kWidth = 4;
constexpr u32 kHeight = 4;
constexpr u32 kMips = 3;

std::vector<u8> MakePixels(FluxionRHIFormat format, u32 width, u32 height, u32 mips, u32 layers)
{
    const usize total = Fluxion_TextureAsset_GetTotalByteSize(format, width, height, mips, layers);
    std::vector<u8> pixels(total);

    // Every byte different from its neighbours, so a level read from the
    // wrong offset does not happen to match.
    for (usize i = 0; i < total; ++i) pixels[i] = (u8)(i * 7u + 13u);
    return pixels;
}

FluxionTextureAssetData MakeData(const std::vector<u8>& pixels, FluxionRHIFormat format, u32 width, u32 height, u32 mips, u32 layers)
{
    FluxionTextureAssetData data{};
    data.width = width;
    data.height = height;
    data.mipCount = mips;
    data.arrayLayers = layers;
    data.format = format;
    data.pixels = pixels.data();
    data.pixelBytes = pixels.size();
    return data;
}

void TheSizeMathsIsWorkedOutAndNotGuessed(TestContext* ctx)
{
    // A level halves and stops at one rather than reaching zero -- a mip
    // chain that reached zero would ask for an empty allocation and then
    // read from it.
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetLevelExtent(4, 0) == 4);
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetLevelExtent(4, 1) == 2);
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetLevelExtent(4, 2) == 1);
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetLevelExtent(4, 9) == 1);

    // 4x4 + 2x2 + 1x1 at four bytes each.
    const usize expected = (16u + 4u + 1u) * 4u;
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetTotalByteSize(FLUXION_RHI_FORMAT_R8G8B8A8_SRGB, 4, 4, 3, 1) == expected);

    // Layers multiply.
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetTotalByteSize(FLUXION_RHI_FORMAT_R8G8B8A8_SRGB, 4, 4, 3, 6) == expected * 6u);

    // A format this does not understand answers zero rather than a
    // confidently wrong number. A depth format is one; so is every
    // compressed one, until there is code that knows their block sizes.
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetLevelByteSize(FLUXION_RHI_FORMAT_D32_FLOAT, 4, 4) == 0);
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetTotalByteSize(FLUXION_RHI_FORMAT_UNKNOWN, 4, 4, 1, 1) == 0);
}

void RoundTripIsByteIdentical(TestContext* ctx)
{
    const FluxionRHIFormat format = FLUXION_RHI_FORMAT_R8G8B8A8_SRGB;
    const std::vector<u8> pixels = MakePixels(format, kWidth, kHeight, kMips, 1);
    const FluxionTextureAssetData data = MakeData(pixels, format, kWidth, kHeight, kMips, 1);

    std::vector<u8> first(8192, 0);
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, first.data(), first.size());
    TEST_CHECK(ctx, Fluxion_TextureAsset_Write(&writer, &data));
    const usize written = Fluxion_Stream_GetPosition(&writer);

    FluxionTextureAsset* asset = nullptr;
    TEST_CHECK(ctx, Fluxion_TextureAsset_Read(first.data(), written, &asset));
    TEST_CHECK(ctx, asset != nullptr);
    if (!asset) return;

    TEST_CHECK(ctx, asset->width == kWidth && asset->height == kHeight);
    TEST_CHECK(ctx, asset->mipCount == kMips && asset->arrayLayers == 1);

    // The colour space IS the format. A texture holding colour comes back
    // in an sRGB format, and there is no second field for the two to
    // disagree about.
    TEST_CHECK(ctx, asset->format == FLUXION_RHI_FORMAT_R8G8B8A8_SRGB);

    // Nothing has been given to a device, so there is neither texture nor
    // view -- and that has to be said, because index zero is a real one.
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(asset->texture));
    TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(asset->view));

    TEST_CHECK(ctx, asset->pixelBytes == pixels.size());
    TEST_CHECK(ctx, asset->pixels != nullptr && std::memcmp(asset->pixels, pixels.data(), pixels.size()) == 0);

    // Written again from what was read. Byte-identical, or something
    // landed at the wrong offset -- which every value checked above can
    // still look right through.
    FluxionTextureAssetData again{};
    again.width = asset->width;
    again.height = asset->height;
    again.mipCount = asset->mipCount;
    again.arrayLayers = asset->arrayLayers;
    again.format = asset->format;
    again.pixels = asset->pixels;
    again.pixelBytes = asset->pixelBytes;

    std::vector<u8> second(8192, 0);
    FluxionStream rewriter;
    Fluxion_MemoryStream_InitWriter(&rewriter, second.data(), second.size());
    TEST_CHECK(ctx, Fluxion_TextureAsset_Write(&rewriter, &again));

    TEST_CHECK(ctx, Fluxion_Stream_GetPosition(&rewriter) == written);
    TEST_CHECK(ctx, std::memcmp(first.data(), second.data(), written) == 0);

    Fluxion_TextureAsset_Destroy(asset);
}

void RefusesWhatItShould(TestContext* ctx)
{
    const FluxionRHIFormat format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    const std::vector<u8> pixels = MakePixels(format, kWidth, kHeight, kMips, 1);
    const FluxionTextureAssetData data = MakeData(pixels, format, kWidth, kHeight, kMips, 1);

    std::vector<u8> bytes(8192, 0);
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, bytes.data(), bytes.size());
    TEST_CHECK(ctx, Fluxion_TextureAsset_Write(&writer, &data));
    const usize written = Fluxion_Stream_GetPosition(&writer);

    FluxionTextureAsset* asset = nullptr;

    std::vector<u8> wrongMagic = bytes;
    wrongMagic[0] ^= 0xFFu;
    TEST_CHECK(ctx, !Fluxion_TextureAsset_Read(wrongMagic.data(), written, &asset));

    std::vector<u8> newer = bytes;
    newer[4] = (u8)(FLUXION_TEXTURE_ASSET_FORMAT_VERSION + 1);
    TEST_CHECK(ctx, !Fluxion_TextureAsset_Read(newer.data(), written, &asset));

    // Cut short.
    TEST_CHECK(ctx, !Fluxion_TextureAsset_Read(bytes.data(), written - 1, &asset));
    TEST_CHECK(ctx, !Fluxion_TextureAsset_Read(bytes.data(), 8, &asset));

    // More levels than halving a four-pixel side can produce. A file
    // claiming them describes something that cannot exist, and believing
    // it would mean reading past what the file holds.
    std::vector<u8> tooManyMips = bytes;
    tooManyMips[16] = 9;
    TEST_CHECK(ctx, !Fluxion_TextureAsset_Read(tooManyMips.data(), written, &asset));

    // The file's own byte count disagreeing with what its size and format
    // imply. Both are read and checked against each other, because
    // believing either one alone is how a damaged file gets an allocation
    // sized from a number nothing verified.
    std::vector<u8> wrongCount = bytes;
    wrongCount[28] = 0xFFu;
    TEST_CHECK(ctx, !Fluxion_TextureAsset_Read(wrongCount.data(), written, &asset));

    // And the writer refuses the same disagreement rather than producing
    // a file nothing can read.
    FluxionTextureAssetData short_ = data;
    short_.pixelBytes = pixels.size() - 4;
    FluxionStream badWriter;
    Fluxion_MemoryStream_InitWriter(&badWriter, bytes.data(), bytes.size());
    TEST_CHECK(ctx, !Fluxion_TextureAsset_Write(&badWriter, &short_));

    // A format with no size, so nothing can be laid out for it.
    FluxionTextureAssetData unknown = data;
    unknown.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    FluxionStream depthWriter;
    Fluxion_MemoryStream_InitWriter(&depthWriter, bytes.data(), bytes.size());
    TEST_CHECK(ctx, !Fluxion_TextureAsset_Write(&depthWriter, &unknown));
}

void TheTypeShipsCookedAndClaimsNoSourceFormat(TestContext* ctx)
{
    Fluxion_AssetTypes_Init(nullptr);

    const FluxionRHIDeviceHandle noDevice = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIQueueHandle noQueue = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    TEST_CHECK(ctx, Fluxion_TextureAsset_RegisterType(noDevice, noQueue));

    const FluxionAssetTypeDesc* type = Fluxion_AssetTypes_Find(Fluxion_TextureAsset_TypeId());
    TEST_CHECK(ctx, type != nullptr);
    if (type)
    {
        TEST_CHECK(ctx, std::strcmp(type->name, FLUXION_TEXTURE_ASSET_TYPE_NAME) == 0);
        TEST_CHECK(ctx, type->defaultShipPolicy == FLUXION_ASSET_SHIP_COOKED);

        // No image decoder here and none meant to be: that half belongs
        // to an importer, and an importer is not part of what a game
        // runs.
        TEST_CHECK(ctx, type->sourceExtensionCount == 0);
        TEST_CHECK(ctx, type->import == nullptr);

        TEST_CHECK(ctx, type->load != nullptr);
        TEST_CHECK(ctx, type->finalize != nullptr);
        TEST_CHECK(ctx, type->unload != nullptr);
    }

    Fluxion_TextureAsset_UnregisterType();
    TEST_CHECK(ctx, Fluxion_AssetTypes_Find(Fluxion_TextureAsset_TypeId()) == nullptr);

    Fluxion_AssetTypes_Shutdown();
}

// The single most consequential thing the usage decides, and the one
// whose failure produces no error at all.
void OnlyColourIsStoredWithAnSrgbCurve(TestContext* ctx)
{
    TEST_CHECK(ctx, Fluxion_TextureAsset_IsUsageSRGB(FLUXION_TEXTURE_USAGE_COLOR_SRGB));

    // A normal is a direction and a mask is a number. Through an sRGB
    // curve neither looks wrong -- it IS wrong, and nothing reports it.
    TEST_CHECK(ctx, !Fluxion_TextureAsset_IsUsageSRGB(FLUXION_TEXTURE_USAGE_NORMAL_MAP));
    TEST_CHECK(ctx, !Fluxion_TextureAsset_IsUsageSRGB(FLUXION_TEXTURE_USAGE_MASK_LINEAR));
    TEST_CHECK(ctx, !Fluxion_TextureAsset_IsUsageSRGB(FLUXION_TEXTURE_USAGE_GRAYSCALE));
    TEST_CHECK(ctx, !Fluxion_TextureAsset_IsUsageSRGB(FLUXION_TEXTURE_USAGE_HDR));

    // Every usage cooks to a format this module can actually lay out, in
    // every combination of compression and block family -- otherwise a
    // preset would name something no texture could be written in, and
    // only on one kind of machine.
    for (int i = 0; i < FLUXION_TEXTURE_USAGE_COUNT; ++i)
    {
        for (int compression = 0; compression < FLUXION_TEXTURE_COMPRESSION_COUNT; ++compression)
        {
            for (int family = 0; family <= (int)FLUXION_TEXTURE_BLOCK_FAMILY_ASTC; ++family)
            {
                const FluxionRHIFormat format = Fluxion_TextureAsset_GetCookedFormat(
                    (FluxionTextureUsage)i, (FluxionTextureCompression)compression, (FluxionTextureBlockFamily)family);

                TEST_CHECK(ctx, format != FLUXION_RHI_FORMAT_UNKNOWN);
                TEST_CHECK(ctx, Fluxion_TextureAsset_GetLevelByteSize(format, 4, 4) > 0);

                // THE COLOUR SPACE IS THE FORMAT -- and it must stay the
                // same answer whether or not the texture is compressed and
                // whichever family it lands in. A colour map that came out
                // linear because it was compressed would be washed out
                // with nothing to point at.
                TEST_CHECK(ctx, Fluxion_RHI_IsFormatSRGB(format) == Fluxion_TextureAsset_IsUsageSRGB((FluxionTextureUsage)i));

                // And a compressed answer is actually compressed, which is
                // the whole reason for asking.
                TEST_CHECK(ctx, Fluxion_RHI_IsFormatCompressed(format) == (compression == FLUXION_TEXTURE_COMPRESSION_BLOCK));
            }
        }
    }

    // Uncompressed and compressed are different answers, not the same one
    // twice: a setting that changed nothing would look like it worked.
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetCookedFormat(FLUXION_TEXTURE_USAGE_COLOR_SRGB, FLUXION_TEXTURE_COMPRESSION_BLOCK, FLUXION_TEXTURE_BLOCK_FAMILY_BC)
                    != Fluxion_TextureAsset_GetCookedFormat(FLUXION_TEXTURE_USAGE_COLOR_SRGB, FLUXION_TEXTURE_COMPRESSION_NONE, FLUXION_TEXTURE_BLOCK_FAMILY_BC));

    // The two families are different answers too, which is the reason a
    // texture cooked for both exists twice.
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetCookedFormat(FLUXION_TEXTURE_USAGE_COLOR_SRGB, FLUXION_TEXTURE_COMPRESSION_BLOCK, FLUXION_TEXTURE_BLOCK_FAMILY_BC)
                    != Fluxion_TextureAsset_GetCookedFormat(FLUXION_TEXTURE_USAGE_COLOR_SRGB, FLUXION_TEXTURE_COMPRESSION_BLOCK, FLUXION_TEXTURE_BLOCK_FAMILY_ASTC));

    // A normal map compressed for desktop hardware lands in the
    // two-channel format. Said here so that whatever samples one has to
    // meet this expectation rather than discover it.
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetCookedFormat(FLUXION_TEXTURE_USAGE_NORMAL_MAP, FLUXION_TEXTURE_COMPRESSION_BLOCK, FLUXION_TEXTURE_BLOCK_FAMILY_BC)
                    == FLUXION_RHI_FORMAT_BC5_UNORM);

    // A compression value nothing knows is refused rather than guessed at.
    TEST_CHECK(ctx, Fluxion_TextureAsset_GetCookedFormat(FLUXION_TEXTURE_USAGE_COLOR_SRGB, (FluxionTextureCompression)99, FLUXION_TEXTURE_BLOCK_FAMILY_BC)
                    == FLUXION_RHI_FORMAT_UNKNOWN);
}

// Settings written by an older build are still readable, and settings
// written by a newer one are refused rather than misread.
void OlderSettingsAreFilledInAndNewerOnesAreRefused(TestContext* ctx)
{
    const FluxionTextureImportSettings current = Fluxion_TextureAsset_DefaultImportSettings(FLUXION_TEXTURE_USAGE_COLOR_SRGB);
    TEST_CHECK(ctx, current.compression == (u32)FLUXION_TEXTURE_COMPRESSION_BLOCK);

    // What this build writes, read back unchanged.
    FluxionTextureImportSettings roundTrip{};
    TEST_CHECK(ctx, Fluxion_TextureAsset_ReadImportSettings(&current, sizeof(current), &roundTrip));
    TEST_CHECK(ctx, std::memcmp(&roundTrip, &current, sizeof(current)) == 0);

    // What the previous version wrote: the same bytes, one field shorter.
    // Everything cooked then was uncompressed, and that is what it has to
    // read back as -- not whatever happened to sit past the end.
    std::vector<u8> older(sizeof(FluxionTextureImportSettings) - sizeof(u32));
    std::memcpy(older.data(), &current, older.size());
    u32 olderVersion = 1;
    std::memcpy(older.data(), &olderVersion, sizeof(olderVersion));

    FluxionTextureImportSettings fromOlder{};
    TEST_CHECK(ctx, Fluxion_TextureAsset_ReadImportSettings(older.data(), older.size(), &fromOlder));
    TEST_CHECK(ctx, fromOlder.compression == (u32)FLUXION_TEXTURE_COMPRESSION_NONE);
    TEST_CHECK(ctx, fromOlder.usage == current.usage);
    TEST_CHECK(ctx, fromOlder.maxAnisotropy == current.maxAnisotropy);
    TEST_CHECK(ctx, fromOlder.version == FLUXION_TEXTURE_IMPORT_SETTINGS_VERSION);

    // A version this build has never heard of. Refused: a setting misread
    // is a texture silently cooked wrong, which no later run would notice.
    FluxionTextureImportSettings newer = current;
    newer.version = FLUXION_TEXTURE_IMPORT_SETTINGS_VERSION + 1;
    FluxionTextureImportSettings fromNewer{};
    TEST_CHECK(ctx, !Fluxion_TextureAsset_ReadImportSettings(&newer, sizeof(newer), &fromNewer));

    // And bytes too short to hold even the version.
    const u8 stub[2] = { 0, 0 };
    TEST_CHECK(ctx, !Fluxion_TextureAsset_ReadImportSettings(stub, sizeof(stub), &fromNewer));
    TEST_CHECK(ctx, !Fluxion_TextureAsset_ReadImportSettings(nullptr, 0, &fromNewer));

    // Bytes that claim a version they are too short to be. Accepting
    // these would leave whichever fields ran out holding defaults nobody
    // wrote down -- a texture cooked to settings nobody chose, which no
    // later run could notice.
    TEST_CHECK(ctx, !Fluxion_TextureAsset_ReadImportSettings(&current, sizeof(current) - sizeof(u32), &fromNewer));
    TEST_CHECK(ctx, !Fluxion_TextureAsset_ReadImportSettings(older.data(), older.size() - 1, &fromNewer));

    // A version of zero names nothing. Refused rather than treated as the
    // oldest one, because zero is also what an all-zero buffer looks like.
    FluxionTextureImportSettings versionless = current;
    versionless.version = 0;
    TEST_CHECK(ctx, !Fluxion_TextureAsset_ReadImportSettings(&versionless, sizeof(versionless), &fromNewer));
}

void TheDefaultsFollowTheUsage(TestContext* ctx)
{
    for (int i = 0; i < FLUXION_TEXTURE_USAGE_COUNT; ++i)
    {
        const FluxionTextureImportSettings settings = Fluxion_TextureAsset_DefaultImportSettings((FluxionTextureUsage)i);

        TEST_CHECK(ctx, settings.version == FLUXION_TEXTURE_IMPORT_SETTINGS_VERSION);
        TEST_CHECK(ctx, settings.usage == (u32)i);

        // Mips for everything by default: without them every receding
        // surface shimmers, and the shimmer reads as a broken renderer
        // rather than as a missing setting.
        TEST_CHECK(ctx, (settings.flags & FLUXION_TEXTURE_IMPORT_GENERATE_MIPS) != 0);
    }

    // The one map whose own variation says how rough a surface looks from
    // far away is the one that gets the limiter.
    const FluxionTextureImportSettings normal = Fluxion_TextureAsset_DefaultImportSettings(FLUXION_TEXTURE_USAGE_NORMAL_MAP);
    TEST_CHECK(ctx, (normal.flags & FLUXION_TEXTURE_IMPORT_ROUGHNESS_LIMITER) != 0);

    const FluxionTextureImportSettings colour = Fluxion_TextureAsset_DefaultImportSettings(FLUXION_TEXTURE_USAGE_COLOR_SRGB);
    TEST_CHECK(ctx, (colour.flags & FLUXION_TEXTURE_IMPORT_ROUGHNESS_LIMITER) == 0);

    // An environment is sampled by direction; wrapping it at the seam
    // samples the far side of the sky.
    const FluxionTextureImportSettings hdr = Fluxion_TextureAsset_DefaultImportSettings(FLUXION_TEXTURE_USAGE_HDR);
    TEST_CHECK(ctx, hdr.addressModeU == (u32)FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE);

    // The sampler follows the settings, and anisotropy is switched off
    // where there are no mips to be anisotropic across.
    FluxionRHISamplerDesc desc = Fluxion_TextureAsset_GetSamplerDesc(&colour);
    TEST_CHECK(ctx, desc.maxAnisotropy > 1.0f);
    TEST_CHECK(ctx, desc.addressModeU == FLUXION_RHI_ADDRESS_MODE_REPEAT);

    FluxionTextureImportSettings noMips = colour;
    noMips.flags &= ~FLUXION_TEXTURE_IMPORT_GENERATE_MIPS;
    desc = Fluxion_TextureAsset_GetSamplerDesc(&noMips);
    TEST_CHECK(ctx, desc.maxAnisotropy == 1.0f);

    // A description with nothing to go on is still a usable one, not a
    // zeroed struct that asks for nearest filtering nobody wanted.
    desc = Fluxion_TextureAsset_GetSamplerDesc(nullptr);
    TEST_CHECK(ctx, desc.minFilter == FLUXION_RHI_FILTER_LINEAR);
}

} // namespace

extern "C" void Test_TextureAsset_Run(TestContext* ctx)
{
    fprintf(stderr, "  Test_TextureAsset\n");

    TheSizeMathsIsWorkedOutAndNotGuessed(ctx);
    RoundTripIsByteIdentical(ctx);
    RefusesWhatItShould(ctx);
    TheTypeShipsCookedAndClaimsNoSourceFormat(ctx);
    OnlyColourIsStoredWithAnSrgbCurve(ctx);
    OlderSettingsAreFilledInAndNewerOnesAreRefused(ctx);
    TheDefaultsFollowTheUsage(ctx);
}
