#include "TestFramework.h"

#include <Fluxion/RHI/Format.h>
#include <Fluxion/RHI/RHI.h>

// Every format this contract knows, so a check can be made of ALL of them
// rather than of the ones somebody remembered. A format added to the enum
// and not to this list makes the count check below fail, which is the
// point: the alternative is a new format that nothing ever asks a
// question about.
static const FluxionRHIFormat kAllFormats[] = {
    FLUXION_RHI_FORMAT_R8G8B8A8_UNORM,
    FLUXION_RHI_FORMAT_R8G8B8A8_SRGB,
    FLUXION_RHI_FORMAT_B8G8R8A8_UNORM,
    FLUXION_RHI_FORMAT_B8G8R8A8_SRGB,
    FLUXION_RHI_FORMAT_R32_FLOAT,
    FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT,
    FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT,
    FLUXION_RHI_FORMAT_R32G32B32_FLOAT,
    FLUXION_RHI_FORMAT_R32G32_FLOAT,
    FLUXION_RHI_FORMAT_D32_FLOAT,
    FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT,
    FLUXION_RHI_FORMAT_BC4_UNORM,
    FLUXION_RHI_FORMAT_BC5_UNORM,
    FLUXION_RHI_FORMAT_BC6H_UFLOAT,
    FLUXION_RHI_FORMAT_BC7_UNORM,
    FLUXION_RHI_FORMAT_BC7_SRGB,
    FLUXION_RHI_FORMAT_ASTC_4X4_UNORM,
    FLUXION_RHI_FORMAT_ASTC_4X4_SRGB,
    FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT,
    FLUXION_RHI_FORMAT_ASTC_6X6_UNORM,
    FLUXION_RHI_FORMAT_ASTC_6X6_SRGB,
    FLUXION_RHI_FORMAT_ASTC_6X6_FLOAT,
    FLUXION_RHI_FORMAT_ASTC_8X8_UNORM,
    FLUXION_RHI_FORMAT_ASTC_8X8_SRGB,
    FLUXION_RHI_FORMAT_ASTC_8X8_FLOAT,
};

static const u32 kAllFormatCount = (u32)(sizeof(kAllFormats) / sizeof(kAllFormats[0]));

static void EveryFormatInTheEnumIsAccountedFor(TestContext* ctx)
{
    // The last entry plus one is how many there are, UNKNOWN included --
    // so this list is one shorter. A format added to the enum without an
    // entry in Fluxion_RHI_GetFormatInfo would otherwise be sized as
    // zero, quietly, at whichever call site reached it first.
    TEST_CHECK(ctx, kAllFormatCount == (u32)FLUXION_RHI_FORMAT_ASTC_8X8_FLOAT);

    for (u32 i = 0; i < kAllFormatCount; ++i)
    {
        const FluxionRHIFormatInfo info = Fluxion_RHI_GetFormatInfo(kAllFormats[i]);
        TEST_CHECK(ctx, info.blockBytes != 0);
        TEST_CHECK(ctx, info.blockWidth != 0);
        TEST_CHECK(ctx, info.blockHeight != 0);
    }
}

static void AFormatNothingKnowsIsSizedAsNothing(TestContext* ctx)
{
    // Zero rather than a plausible guess. A confidently wrong size makes
    // an allocation that is too small, and reading past it is not an
    // error anything reports.
    const FluxionRHIFormatInfo unknown = Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_UNKNOWN);
    TEST_CHECK(ctx, unknown.blockBytes == 0);

    TEST_CHECK(ctx, Fluxion_RHI_GetFormatRowBytes(FLUXION_RHI_FORMAT_UNKNOWN, 64) == 0);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_UNKNOWN, 64, 64) == 0);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatBlockColumns(FLUXION_RHI_FORMAT_UNKNOWN, 64) == 0);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatBlockRows(FLUXION_RHI_FORMAT_UNKNOWN, 64) == 0);

    // Also true of a number that is not in the enum at all, which is what
    // a damaged file's format field looks like.
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatInfo((FluxionRHIFormat)9999).blockBytes == 0);
}

static void AnUncompressedFormatIsOneTexelPerBlock(TestContext* ctx)
{
    const FluxionRHIFormatInfo rgba8 = Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_R8G8B8A8_UNORM);
    TEST_CHECK(ctx, rgba8.blockWidth == 1 && rgba8.blockHeight == 1);
    TEST_CHECK(ctx, rgba8.blockBytes == 4);
    TEST_CHECK(ctx, !rgba8.compressed);

    // Which is what lets the block arithmetic answer for it unchanged.
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatRowBytes(FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, 7) == 28);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatBlockRows(FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, 7) == 7);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, 7, 7) == 196);

    TEST_CHECK(ctx, Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT).blockBytes == 8);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_R32G32B32_FLOAT).blockBytes == 12);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT).blockBytes == 16);
}

static void ABlockFormatCostsAWholeBlockEvenForOneTexel(TestContext* ctx)
{
    const FluxionRHIFormatInfo bc7 = Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_BC7_UNORM);
    TEST_CHECK(ctx, bc7.blockWidth == 4 && bc7.blockHeight == 4);
    TEST_CHECK(ctx, bc7.blockBytes == 16);
    TEST_CHECK(ctx, bc7.compressed);

    // The smallest mips of a chain are where this either works or reads
    // off the end: a 1x1 level of a 4x4-block format still holds one
    // sixteen-byte block, not one sixteenth of one.
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC7_UNORM, 1, 1) == 16);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC7_UNORM, 2, 2) == 16);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC7_UNORM, 4, 4) == 16);

    // And a size that is not a whole number of blocks rounds up in both
    // directions independently.
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatBlockColumns(FLUXION_RHI_FORMAT_BC7_UNORM, 5) == 2);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatBlockRows(FLUXION_RHI_FORMAT_BC7_UNORM, 5) == 2);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC7_UNORM, 5, 5) == 64);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC7_UNORM, 8, 4) == 32);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC7_UNORM, 4, 8) == 32);

    // BC4 is the one BC format here that is half the size of the rest,
    // which is the whole reason a single channel gets its own format.
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_BC4_UNORM).blockBytes == 8);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC4_UNORM, 8, 8) == 32);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_BC5_UNORM).blockBytes == 16);
}

static void AnAstcBlockIsAlwaysSixteenBytesAndTheSizeIsWhatChanges(TestContext* ctx)
{
    // Which is the whole of the quality/size trade: same block, more
    // texels in it.
    const FluxionRHIFormatInfo astc4 = Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_ASTC_4X4_UNORM);
    const FluxionRHIFormatInfo astc6 = Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_ASTC_6X6_UNORM);
    const FluxionRHIFormatInfo astc8 = Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_ASTC_8X8_UNORM);

    TEST_CHECK(ctx, astc4.blockBytes == 16 && astc6.blockBytes == 16 && astc8.blockBytes == 16);
    TEST_CHECK(ctx, astc4.blockWidth == 4 && astc4.blockHeight == 4);
    TEST_CHECK(ctx, astc6.blockWidth == 6 && astc6.blockHeight == 6);
    TEST_CHECK(ctx, astc8.blockWidth == 8 && astc8.blockHeight == 8);

    // A 6x6 block is the one that does not divide a power-of-two texture
    // evenly, which is exactly where rounding up shows.
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatBlockColumns(FLUXION_RHI_FORMAT_ASTC_6X6_UNORM, 8) == 2);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_ASTC_6X6_UNORM, 8, 8) == 64);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_ASTC_8X8_UNORM, 8, 8) == 16);
}

static void TheColourSpaceIsReadOffTheFormat(TestContext* ctx)
{
    TEST_CHECK(ctx, Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_R8G8B8A8_SRGB));
    TEST_CHECK(ctx, Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_B8G8R8A8_SRGB));
    TEST_CHECK(ctx, Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_BC7_SRGB));
    TEST_CHECK(ctx, Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_ASTC_4X4_SRGB));
    TEST_CHECK(ctx, Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_ASTC_6X6_SRGB));
    TEST_CHECK(ctx, Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_ASTC_8X8_SRGB));

    // A normal map or a mask stored in one of these would be wrong rather
    // than merely odd-looking, and nothing would report it -- so the
    // linear ones are checked by name too.
    TEST_CHECK(ctx, !Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_R8G8B8A8_UNORM));
    TEST_CHECK(ctx, !Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_BC4_UNORM));
    TEST_CHECK(ctx, !Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_BC5_UNORM));
    TEST_CHECK(ctx, !Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_BC6H_UFLOAT));
    TEST_CHECK(ctx, !Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_BC7_UNORM));
    TEST_CHECK(ctx, !Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT));

    // Depth is not a colour and never carries a curve.
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_D32_FLOAT).depth);
    TEST_CHECK(ctx, Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT).depth);
    TEST_CHECK(ctx, !Fluxion_RHI_IsFormatSRGB(FLUXION_RHI_FORMAT_D32_FLOAT));
    TEST_CHECK(ctx, !Fluxion_RHI_GetFormatInfo(FLUXION_RHI_FORMAT_R8G8B8A8_UNORM).depth);
}

static void TheRowAlignmentIsAWholeNumberOfBlocksForEveryFormat(TestContext* ctx)
{
    // Load-bearing rather than incidental. The Vulkan backend turns a
    // padded row back into a count of texels by dividing the padded byte
    // count by the block size -- which is only the same row if the
    // padding lands on a block boundary. It does, for every format here,
    // because 256 is a multiple of 4, 8, 12 and 16.
    //
    // Except 12: R32G32B32_FLOAT does NOT divide 256, and it is the one
    // format in the set that cannot be a texture this way. It is a
    // vertex-attribute format, and CopyBufferToTexture is never asked for
    // one -- said here so that the exception is written down rather than
    // discovered.
    for (u32 i = 0; i < kAllFormatCount; ++i)
    {
        const FluxionRHIFormatInfo info = Fluxion_RHI_GetFormatInfo(kAllFormats[i]);
        if (kAllFormats[i] == FLUXION_RHI_FORMAT_R32G32B32_FLOAT) continue;
        if (info.depth) continue;

        TEST_CHECK(ctx, (FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT % (usize)info.blockBytes) == 0);
    }
}

void Test_Format_Run(TestContext* ctx)
{
    EveryFormatInTheEnumIsAccountedFor(ctx);
    AFormatNothingKnowsIsSizedAsNothing(ctx);
    AnUncompressedFormatIsOneTexelPerBlock(ctx);
    ABlockFormatCostsAWholeBlockEvenForOneTexel(ctx);
    AnAstcBlockIsAlwaysSixteenBytesAndTheSizeIsWhatChanges(ctx);
    TheColourSpaceIsReadOffTheFormat(ctx);
    TheRowAlignmentIsAWholeNumberOfBlocksForEveryFormat(ctx);
}
