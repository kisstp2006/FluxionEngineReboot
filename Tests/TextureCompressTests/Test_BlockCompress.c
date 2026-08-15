#include "TestFramework.h"

#include <Fluxion/TextureCompress/BlockCompress.h>

#include <math.h>
#include <string.h>

// What a block encoder can and cannot be checked for on its own.
//
// A round trip through this module's own decoder proves the arithmetic is
// self-consistent. It does NOT prove the bits are in the places the format
// says, because an encoder and a decoder written from the same
// misreading agree with each other perfectly. So the checks below also
// include blocks worked out by hand, byte for byte -- and the hardware
// itself is asked in RenderCoreTests, where a device can sample one of
// these textures and hand back what it made of it.

static void ASolidBlockSurvivesExactly(TestContext* ctx)
{
    // The strongest thing that can be said about a lossy format: where
    // nothing needs approximating, nothing may change.
    //
    // In this mode an endpoint is seven bits a channel plus ONE more bit
    // shared by all four of them, which becomes the low bit of every
    // channel. So the colours it reproduces exactly are the ones whose
    // four channels agree in their low bit -- these do, all even.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = 200;
        source[i * 4 + 1] = 100;
        source[i * 4 + 2] = 36;
        source[i * 4 + 3] = 202;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC7_UNORM, source, 4, 4, block, sizeof(block)));

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC7_UNORM, block, sizeof(block), 4, 4, decoded));

    bool exact = true;
    for (u32 i = 0; i < sizeof(decoded); ++i)
    {
        if (decoded[i] != source[i]) exact = false;
    }
    TEST_CHECK(ctx, exact);
}

static void OneSharedBitIsTheMostASolidColourCanCostAnyChannel(TestContext* ctx)
{
    // The other side of the rule above, and the reason it is worth
    // writing down: a solid colour whose channels DISAGREE about their
    // low bit cannot be reproduced exactly by this mode at all, no matter
    // how much effort the encoder spends. What it can promise is that no
    // channel is out by more than one -- which is invisible, and is the
    // difference between a limit of the format and a bug in the encoder.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = 200; // even
        source[i * 4 + 1] = 100; // even
        source[i * 4 + 2] = 37;  // odd
        source[i * 4 + 3] = 201; // odd
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC7_UNORM, source, 4, 4, block, sizeof(block)));

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC7_UNORM, block, sizeof(block), 4, 4, decoded));

    for (u32 i = 0; i < sizeof(decoded); ++i)
    {
        const int difference = (int)decoded[i] - (int)source[i];
        TEST_CHECK(ctx, difference >= -1 && difference <= 1);
    }
}

static void ABC4BlockIsWrittenWhereTheFormatSaysItIs(TestContext* ctx)
{
    // Worked out by hand rather than by running the encoder and writing
    // down what came out. A block of one repeated value has both endpoint
    // bytes equal, which selects the arrangement whose first palette
    // entry is that value -- so every index is zero, and the six index
    // bytes are all zero. Any other bit order would put something else in
    // one of those eight bytes.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = 128;
        source[i * 4 + 1] = 0;
        source[i * 4 + 2] = 0;
        source[i * 4 + 3] = 255;
    }

    u8 block[8];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC4_UNORM, source, 4, 4, block, sizeof(block)));

    TEST_CHECK(ctx, block[0] == 128);
    TEST_CHECK(ctx, block[1] == 128);
    for (u32 i = 2; i < 8; ++i) TEST_CHECK(ctx, block[i] == 0);

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC4_UNORM, block, sizeof(block), 4, 4, decoded));
    for (u32 i = 0; i < 16; ++i) TEST_CHECK(ctx, decoded[i * 4 + 0] == 128);
}

static void BC4KeepsBothEndsOfAMaskExactly(TestContext* ctx)
{
    // A mask that is mostly fully on and fully off is the case the second
    // palette arrangement exists for. Whichever arrangement the encoder
    // picks, both extremes have to come back untouched -- an alpha mask
    // that decodes to 253 instead of 255 is a surface that is almost
    // opaque, which is not the same thing at all.
    u8 source[4 * 4 * 4];
    memset(source, 0, sizeof(source));
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = (i < 6) ? 0 : ((i < 10) ? 130 : 255);
        source[i * 4 + 3] = 255;
    }

    u8 block[8];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC4_UNORM, source, 4, 4, block, sizeof(block)));

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC4_UNORM, block, sizeof(block), 4, 4, decoded));

    for (u32 i = 0; i < 16; ++i)
    {
        if (source[i * 4 + 0] == 0) TEST_CHECK(ctx, decoded[i * 4 + 0] == 0);
        if (source[i * 4 + 0] == 255) TEST_CHECK(ctx, decoded[i * 4 + 0] == 255);
    }
}

static void BC5CarriesTwoChannelsAndNoMore(TestContext* ctx)
{
    // Which matters for the one thing BC5 is for: a normal map's third
    // component is not stored, and a shader that reads it gets whatever
    // the format decided to put there rather than a direction.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = (u8)(i * 16u);
        source[i * 4 + 1] = (u8)(255u - i * 16u);
        source[i * 4 + 2] = 77;
        source[i * 4 + 3] = 99;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC5_UNORM, source, 4, 4, block, sizeof(block)));

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC5_UNORM, block, sizeof(block), 4, 4, decoded));

    for (u32 i = 0; i < 16; ++i)
    {
        // Eight palette entries across a span of two hundred and forty
        // puts them thirty-four apart, so half of that is the most any
        // one texel can be out by. A bound tighter than the format allows
        // would be a test of nothing but this ramp.
        const int red = (int)decoded[i * 4 + 0] - (int)source[i * 4 + 0];
        const int green = (int)decoded[i * 4 + 1] - (int)source[i * 4 + 1];
        TEST_CHECK(ctx, red > -18 && red < 18);
        TEST_CHECK(ctx, green > -18 && green < 18);

        // The two channels that were never stored, said rather than left
        // to whatever the buffer held.
        TEST_CHECK(ctx, decoded[i * 4 + 2] == 0);
        TEST_CHECK(ctx, decoded[i * 4 + 3] == 255);
    }
}

static void AGradientComesBackCloseEnoughToBeWorthTheSize(TestContext* ctx)
{
    // The case a single-region encoder is good at. If a smooth ramp comes
    // back with visible steps, the endpoint fitting is not doing its job
    // and the bound below says so.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = (u8)(i * 17u);
        source[i * 4 + 1] = (u8)(i * 8u);
        source[i * 4 + 2] = (u8)(255u - i * 17u);
        source[i * 4 + 3] = 255;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC7_UNORM, source, 4, 4, block, sizeof(block)));

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC7_UNORM, block, sizeof(block), 4, 4, decoded));

    int worst = 0;
    for (u32 i = 0; i < sizeof(decoded); ++i)
    {
        int difference = (int)decoded[i] - (int)source[i];
        if (difference < 0) difference = -difference;
        if (difference > worst) worst = difference;
    }

    // Sixteen positions along one line, endpoints quantized to eight
    // bits: a straight ramp should land almost on top of itself.
    TEST_CHECK(ctx, worst <= 4);
}

static void AGradientRunningTheOtherWayIsJustAsGood(TestContext* ctx)
{
    // Not the same test twice. The first texel of a block has its index
    // stored in three bits rather than four, its high bit taken to be
    // zero -- so a block whose first texel belongs at the FAR end of the
    // line cannot be written as it stands. The line has to be turned
    // around and every index inverted. A ramp that descends is the case
    // that needs it, and a ramp that ascends is the case that hides it.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = (u8)(255u - i * 17u);
        source[i * 4 + 1] = (u8)(120u - i * 8u);
        source[i * 4 + 2] = (u8)(i * 17u);
        source[i * 4 + 3] = 255;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC7_UNORM, source, 4, 4, block, sizeof(block)));

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC7_UNORM, block, sizeof(block), 4, 4, decoded));

    int worst = 0;
    for (u32 i = 0; i < sizeof(decoded); ++i)
    {
        int difference = (int)decoded[i] - (int)source[i];
        if (difference < 0) difference = -difference;
        if (difference > worst) worst = difference;
    }
    TEST_CHECK(ctx, worst <= 4);
}

static void AnImageThatIsNotAWholeNumberOfBlocksIsPaddedFromItsOwnEdge(TestContext* ctx)
{
    // Five across and three down: the last block in each direction hangs
    // off the image. Padding it from the edge rather than from whatever
    // followed the image in memory is what keeps the right and bottom of
    // a texture from picking up a stripe of something unrelated -- and
    // reading past the image at all is what a sanitizer build is here to
    // notice.
    const u32 width = 5;
    const u32 height = 3;

    u8 source[5 * 3 * 4];
    for (u32 i = 0; i < width * height; ++i)
    {
        source[i * 4 + 0] = (u8)(40u + i * 3u);
        source[i * 4 + 1] = (u8)(90u + i * 2u);
        source[i * 4 + 2] = (u8)(160u - i);
        source[i * 4 + 3] = 255;
    }

    const usize outputSize = Fluxion_TextureCompress_GetOutputSize(FLUXION_RHI_FORMAT_BC7_UNORM, width, height);

    // Two blocks across and one down, sixteen bytes each.
    TEST_CHECK(ctx, outputSize == 32);

    u8 blocks[32];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC7_UNORM, source, width, height, blocks, sizeof(blocks)));

    u8 decoded[5 * 3 * 4];
    memset(decoded, 0xCD, sizeof(decoded));
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC7_UNORM, blocks, sizeof(blocks), width, height, decoded));

    int worst = 0;
    for (u32 i = 0; i < sizeof(decoded); ++i)
    {
        int difference = (int)decoded[i] - (int)source[i];
        if (difference < 0) difference = -difference;
        if (difference > worst) worst = difference;
    }
    TEST_CHECK(ctx, worst <= 8);
}

static void AnOutputBufferTooSmallIsRefusedRatherThanFilled(TestContext* ctx)
{
    u8 source[4 * 4 * 4];
    memset(source, 0x40, sizeof(source));

    u8 block[15]; // one short
    TEST_CHECK(ctx, !Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC7_UNORM, source, 4, 4, block, sizeof(block)));

    // And a format nothing here encodes is refused rather than quietly
    // producing zero bytes that would read as an empty texture.
    TEST_CHECK(ctx, Fluxion_TextureCompress_GetOutputSize(FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, 4, 4) == 0);
    TEST_CHECK(ctx, !Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, source, 4, 4, block, sizeof(block)));
    TEST_CHECK(ctx, Fluxion_TextureCompress_GetPixelLayout(FLUXION_RHI_FORMAT_D32_FLOAT) == FLUXION_TEXTURE_COMPRESS_PIXELS_NONE);
}

static void BC6HKeepsValuesAboveOne(TestContext* ctx)
{
    // The entire reason this format exists. A sky at eight hundred and a
    // shadow at a hundredth have to survive in the same block, and the
    // error that matters is relative, not absolute -- an absolute bound
    // would be met by a format that threw the sky away.
    // A ratio of ten between the darkest and brightest texel in one
    // block, which is a lot for sixteen adjacent texels of a real image
    // and still comfortably inside what the format reproduces well.
    f32 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = 2.0f + (f32)i * 1.2f;
        source[i * 4 + 1] = 1.0f + (f32)i * 0.6f;
        source[i * 4 + 2] = 0.5f + (f32)i * 0.3f;
        source[i * 4 + 3] = 1.0f;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC6H_UFLOAT, source, 4, 4, block, sizeof(block)));

    f32 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC6H_UFLOAT, block, sizeof(block), 4, 4, decoded));

    for (u32 i = 0; i < 16; ++i)
    {
        for (u32 channel = 0; channel < 3; ++channel)
        {
            const f32 expected = source[i * 4 + channel];
            const f32 got = decoded[i * 4 + channel];
            // RELATIVE, and the same bound for the darkest texel as for
            // the brightest -- which is the property worth checking, not
            // an incidental one. This format's sixteen positions are
            // evenly spaced in a space that is roughly logarithmic in
            // brightness, so they land geometrically: over a ratio of ten
            // each step is a factor of about 1.17, and half of that is
            // the most any texel can be out by. An absolute bound would
            // be met by an encoder that threw the bright end away.
            const f32 relative = fabsf(got - expected) / (expected > 0.001f ? expected : 0.001f);
            TEST_CHECK(ctx, relative < 0.08f);
        }

        // No alpha in this format, and one is what a decoder must produce.
        TEST_CHECK(ctx, decoded[i * 4 + 3] == 1.0f);
    }
}

static void BC6HRefusesNothingAndInventsNothingBelowZero(TestContext* ctx)
{
    // The unsigned form has no negative values at all. A caller handing
    // one over gets zero rather than a large positive number, which is
    // what reading a sign bit as magnitude would produce.
    f32 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = -5.0f;
        source[i * 4 + 1] = 0.0f;
        source[i * 4 + 2] = 2.0f;
        source[i * 4 + 3] = 1.0f;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_BC6H_UFLOAT, source, 4, 4, block, sizeof(block)));

    f32 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_BC6H_UFLOAT, block, sizeof(block), 4, 4, decoded));

    for (u32 i = 0; i < 16; ++i)
    {
        TEST_CHECK(ctx, decoded[i * 4 + 0] == 0.0f);
        TEST_CHECK(ctx, decoded[i * 4 + 1] == 0.0f);
        TEST_CHECK(ctx, decoded[i * 4 + 2] > 1.7f && decoded[i * 4 + 2] < 2.3f);
    }
}

static void TheOutputSizeIsTheFormatsOwnAnswer(TestContext* ctx)
{
    // Not a second opinion. A cooked file is sized by the format table,
    // and an encoder that filled a different number of bytes would write
    // a file whose own header disagreed with its contents.
    TEST_CHECK(ctx, Fluxion_TextureCompress_GetOutputSize(FLUXION_RHI_FORMAT_BC7_UNORM, 8, 8) == Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC7_UNORM, 8, 8));
    TEST_CHECK(ctx, Fluxion_TextureCompress_GetOutputSize(FLUXION_RHI_FORMAT_BC4_UNORM, 9, 5) == Fluxion_RHI_GetFormatLevelBytes(FLUXION_RHI_FORMAT_BC4_UNORM, 9, 5));
    TEST_CHECK(ctx, Fluxion_TextureCompress_GetOutputSize(FLUXION_RHI_FORMAT_BC6H_UFLOAT, 1, 1) == 16);
}

static void AnAstcBlockCarriesItsConfigurationWhereTheFormatSaysItDoes(TestContext* ctx)
{
    // Read straight out of the bytes rather than through the decoder,
    // which would only confirm that the decoder reads what the encoder
    // wrote. The eleven low bits name the weight grid and its precision,
    // the next two the number of regions, and the four after that how
    // colour is described. Those four fields are what a hardware decoder
    // reads first, and everything else in the block depends on them.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = 60;
        source[i * 4 + 1] = 120;
        source[i * 4 + 2] = 180;
        source[i * 4 + 3] = 255;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_ASTC_4X4_UNORM, source, 4, 4, block, sizeof(block)));

    const u32 low = (u32)block[0] | ((u32)block[1] << 8) | ((u32)block[2] << 16);
    TEST_CHECK(ctx, (low & 0x7FFu) == 0x42u);        // four by four, two bits a weight
    TEST_CHECK(ctx, ((low >> 11) & 0x3u) == 0u);     // one region
    TEST_CHECK(ctx, ((low >> 13) & 0xFu) == 12u);    // red, green, blue and alpha, given outright
}

static void AnAstcBlockOfOneColourComesBackUntouched(TestContext* ctx)
{
    // Every endpoint value is eight bits here and the quantization step is
    // the identity, so a solid colour has nothing to lose -- unlike BC7,
    // where the two endpoints share one extra bit and the channels have to
    // agree about it.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = 201;
        source[i * 4 + 1] = 100;
        source[i * 4 + 2] = 37;
        source[i * 4 + 3] = 202;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_ASTC_4X4_UNORM, source, 4, 4, block, sizeof(block)));

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_ASTC_4X4_UNORM, block, sizeof(block), 4, 4, decoded));

    bool exact = true;
    for (u32 i = 0; i < sizeof(decoded); ++i)
    {
        if (decoded[i] != source[i]) exact = false;
    }
    TEST_CHECK(ctx, exact);
}

static void AnAstcGradientHasFourPositionsAndNoMore(TestContext* ctx)
{
    // Two bits a weight is four positions along the line, against BC7's
    // sixteen. That is the trade this configuration makes -- the same
    // sixteen bytes, spent on full-precision endpoints instead of on
    // index precision -- and the bound says what it costs rather than
    // pretending it costs nothing.
    u8 source[4 * 4 * 4];
    for (u32 i = 0; i < 16; ++i)
    {
        source[i * 4 + 0] = (u8)(i * 17u);
        source[i * 4 + 1] = (u8)(i * 8u);
        source[i * 4 + 2] = (u8)(255u - i * 17u);
        source[i * 4 + 3] = 255;
    }

    u8 block[16];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_ASTC_4X4_UNORM, source, 4, 4, block, sizeof(block)));

    u8 decoded[4 * 4 * 4];
    TEST_CHECK(ctx, Fluxion_TextureCompress_Decode(FLUXION_RHI_FORMAT_ASTC_4X4_UNORM, block, sizeof(block), 4, 4, decoded));

    int worst = 0;
    for (u32 i = 0; i < sizeof(decoded); ++i)
    {
        int difference = (int)decoded[i] - (int)source[i];
        if (difference < 0) difference = -difference;
        if (difference > worst) worst = difference;
    }

    // Four positions across a span of 255 puts them 85 apart, so half of
    // that is what a texel between two of them costs.
    TEST_CHECK(ctx, worst <= 43);

    // And the two ends of the line are still exact, which is what makes
    // the four positions worth having at all.
    TEST_CHECK(ctx, decoded[0] == source[0]);
    TEST_CHECK(ctx, decoded[15 * 4] == source[15 * 4]);
}

static void TheAstcSizesWithNoEncoderSayNoRatherThanGuess(TestContext* ctx)
{
    // Six by six and eight by eight cannot give every texel its own
    // weight, so they lean on the decoder to interpolate between grid
    // points -- a piece of the format this module does not write. Saying
    // so is the point: an encoder that quietly produced something for
    // them would produce it wrong, on the one kind of device that uses
    // this format.
    u8 source[4 * 4 * 4];
    memset(source, 0x60, sizeof(source));
    u8 block[16];

    TEST_CHECK(ctx, Fluxion_TextureCompress_GetPixelLayout(FLUXION_RHI_FORMAT_ASTC_6X6_UNORM) == FLUXION_TEXTURE_COMPRESS_PIXELS_NONE);
    TEST_CHECK(ctx, Fluxion_TextureCompress_GetPixelLayout(FLUXION_RHI_FORMAT_ASTC_8X8_UNORM) == FLUXION_TEXTURE_COMPRESS_PIXELS_NONE);
    TEST_CHECK(ctx, Fluxion_TextureCompress_GetPixelLayout(FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT) == FLUXION_TEXTURE_COMPRESS_PIXELS_NONE);

    TEST_CHECK(ctx, !Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_ASTC_6X6_UNORM, source, 4, 4, block, sizeof(block)));
    TEST_CHECK(ctx, !Fluxion_TextureCompress_Encode(FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT, source, 4, 4, block, sizeof(block)));

    // The sRGB form of the size that IS written shares its bits with the
    // linear one -- which curve applies is the reading, not the writing.
    TEST_CHECK(ctx, Fluxion_TextureCompress_GetPixelLayout(FLUXION_RHI_FORMAT_ASTC_4X4_SRGB) == FLUXION_TEXTURE_COMPRESS_PIXELS_RGBA8);
}

void Test_BlockCompress_Run(TestContext* ctx)
{
    ASolidBlockSurvivesExactly(ctx);
    OneSharedBitIsTheMostASolidColourCanCostAnyChannel(ctx);
    ABC4BlockIsWrittenWhereTheFormatSaysItIs(ctx);
    BC4KeepsBothEndsOfAMaskExactly(ctx);
    BC5CarriesTwoChannelsAndNoMore(ctx);
    AGradientComesBackCloseEnoughToBeWorthTheSize(ctx);
    AGradientRunningTheOtherWayIsJustAsGood(ctx);
    AnImageThatIsNotAWholeNumberOfBlocksIsPaddedFromItsOwnEdge(ctx);
    AnOutputBufferTooSmallIsRefusedRatherThanFilled(ctx);
    BC6HKeepsValuesAboveOne(ctx);
    BC6HRefusesNothingAndInventsNothingBelowZero(ctx);
    TheOutputSizeIsTheFormatsOwnAnswer(ctx);
    AnAstcBlockCarriesItsConfigurationWhereTheFormatSaysItDoes(ctx);
    AnAstcBlockOfOneColourComesBackUntouched(ctx);
    AnAstcGradientHasFourPositionsAndNoMore(ctx);
    TheAstcSizesWithNoEncoderSayNoRatherThanGuess(ctx);
}
