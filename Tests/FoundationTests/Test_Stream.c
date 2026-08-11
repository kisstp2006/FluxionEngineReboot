#include "TestFramework.h"

#include <Fluxion/Foundation/Serialization/Stream.h>

void Test_Stream_Run(TestContext* ctx)
{
    // Roundtrip every primitive type through a single buffer.
    u8 buffer[64];

    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, buffer, sizeof(buffer));
    TEST_CHECK(ctx, Fluxion_Stream_IsWriting(&writer));
    TEST_CHECK(ctx, !Fluxion_Stream_IsReading(&writer));

    u8 u8Value = 0x7Au;
    u16 u16Value = 0xBEEFu;
    u32 u32Value = 0xDEADBEEFu;
    u64 u64Value = 0x0123456789ABCDEFull;
    i32 i32Value = -12345;
    i64 i64Value = -1234567890123LL;
    f32 f32Value = 3.14159f;

    Fluxion_Stream_SerializeU8(&writer, &u8Value);
    Fluxion_Stream_SerializeU16(&writer, &u16Value);
    Fluxion_Stream_SerializeU32(&writer, &u32Value);
    Fluxion_Stream_SerializeU64(&writer, &u64Value);
    Fluxion_Stream_SerializeI32(&writer, &i32Value);
    Fluxion_Stream_SerializeI64(&writer, &i64Value);
    Fluxion_Stream_SerializeF32(&writer, &f32Value);

    TEST_CHECK(ctx, !Fluxion_Stream_HasOverflowed(&writer));
    usize written = Fluxion_Stream_GetPosition(&writer);
    TEST_CHECK(ctx, written == 1 + 2 + 4 + 8 + 4 + 8 + 4);

    FluxionStream reader;
    Fluxion_MemoryStream_InitReader(&reader, buffer, written);
    TEST_CHECK(ctx, Fluxion_Stream_IsReading(&reader));

    u8 readU8 = 0;
    u16 readU16 = 0;
    u32 readU32 = 0;
    u64 readU64 = 0;
    i32 readI32 = 0;
    i64 readI64 = 0;
    f32 readF32 = 0.0f;

    Fluxion_Stream_SerializeU8(&reader, &readU8);
    Fluxion_Stream_SerializeU16(&reader, &readU16);
    Fluxion_Stream_SerializeU32(&reader, &readU32);
    Fluxion_Stream_SerializeU64(&reader, &readU64);
    Fluxion_Stream_SerializeI32(&reader, &readI32);
    Fluxion_Stream_SerializeI64(&reader, &readI64);
    Fluxion_Stream_SerializeF32(&reader, &readF32);

    TEST_CHECK(ctx, !Fluxion_Stream_HasOverflowed(&reader));
    TEST_CHECK(ctx, readU8 == u8Value);
    TEST_CHECK(ctx, readU16 == u16Value);
    TEST_CHECK(ctx, readU32 == u32Value);
    TEST_CHECK(ctx, readU64 == u64Value);
    TEST_CHECK(ctx, readI32 == i32Value);
    TEST_CHECK(ctx, readI64 == i64Value);
    TEST_CHECK(ctx, readF32 == f32Value);

    // Explicit little-endian wire format: the first byte written for a
    // u32 must be its least significant byte, regardless of host order.
    u8 wireCheckBuffer[4];
    FluxionStream wireCheckWriter;
    Fluxion_MemoryStream_InitWriter(&wireCheckWriter, wireCheckBuffer, sizeof(wireCheckBuffer));
    u32 wireCheckValue = 0x11223344u;
    Fluxion_Stream_SerializeU32(&wireCheckWriter, &wireCheckValue);
    TEST_CHECK(ctx, wireCheckBuffer[0] == 0x44 && wireCheckBuffer[1] == 0x33 &&
        wireCheckBuffer[2] == 0x22 && wireCheckBuffer[3] == 0x11);

    // Overflow: writing past capacity sets the flag instead of
    // overrunning the buffer, and further calls stay no-ops.
    u8 tinyBuffer[2];
    FluxionStream tinyWriter;
    Fluxion_MemoryStream_InitWriter(&tinyWriter, tinyBuffer, sizeof(tinyBuffer));

    u32 tooBig = 0xFFFFFFFFu;
    Fluxion_Stream_SerializeU32(&tinyWriter, &tooBig); // 4 bytes into a 2-byte buffer
    TEST_CHECK(ctx, Fluxion_Stream_HasOverflowed(&tinyWriter));
    TEST_CHECK(ctx, Fluxion_Stream_GetPosition(&tinyWriter) == 0);

    u8 anotherByte = 0xAB;
    Fluxion_Stream_SerializeU8(&tinyWriter, &anotherByte); // still a no-op once overflowed
    TEST_CHECK(ctx, Fluxion_Stream_GetPosition(&tinyWriter) == 0);
}
