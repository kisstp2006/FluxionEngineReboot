#include <Fluxion/Foundation/Serialization/Stream.h>

#include <string.h>

void Fluxion_MemoryStream_InitWriter(FluxionStream* stream, void* buffer, usize capacity)
{
    stream->mode = FLUXION_STREAM_WRITE;
    stream->buffer = (u8*)buffer;
    stream->capacity = capacity;
    stream->position = 0;
    stream->overflowed = false;
}

void Fluxion_MemoryStream_InitReader(FluxionStream* stream, const void* buffer, usize size)
{
    stream->mode = FLUXION_STREAM_READ;
    // Cast away const: read-mode SerializeBytes only ever copies FROM this
    // buffer, never into it -- the single-buffer-pointer design keeps
    // FluxionStream one struct for both directions instead of two.
    stream->buffer = (u8*)buffer;
    stream->capacity = size;
    stream->position = 0;
    stream->overflowed = false;
}

void Fluxion_Stream_SerializeBytes(FluxionStream* stream, void* data, usize size)
{
    if (stream->overflowed) return;

    if (stream->position + size > stream->capacity)
    {
        stream->overflowed = true;
        return;
    }

    if (stream->mode == FLUXION_STREAM_WRITE)
    {
        memcpy(stream->buffer + stream->position, data, size);
    }
    else
    {
        memcpy(data, stream->buffer + stream->position, size);
    }
    stream->position += size;
}

void Fluxion_Stream_SerializeU8(FluxionStream* stream, u8* value)
{
    Fluxion_Stream_SerializeBytes(stream, value, sizeof(*value));
}

void Fluxion_Stream_SerializeU16(FluxionStream* stream, u16* value)
{
    u8 bytes[2];
    if (stream->mode == FLUXION_STREAM_WRITE)
    {
        bytes[0] = (u8)(*value & 0xFFu);
        bytes[1] = (u8)((*value >> 8) & 0xFFu);
    }

    Fluxion_Stream_SerializeBytes(stream, bytes, sizeof(bytes));

    if (stream->mode == FLUXION_STREAM_READ)
    {
        *value = (u16)((u16)bytes[0] | ((u16)bytes[1] << 8));
    }
}

void Fluxion_Stream_SerializeU32(FluxionStream* stream, u32* value)
{
    u8 bytes[4];
    if (stream->mode == FLUXION_STREAM_WRITE)
    {
        bytes[0] = (u8)(*value & 0xFFu);
        bytes[1] = (u8)((*value >> 8) & 0xFFu);
        bytes[2] = (u8)((*value >> 16) & 0xFFu);
        bytes[3] = (u8)((*value >> 24) & 0xFFu);
    }

    Fluxion_Stream_SerializeBytes(stream, bytes, sizeof(bytes));

    if (stream->mode == FLUXION_STREAM_READ)
    {
        *value = (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
    }
}

void Fluxion_Stream_SerializeU64(FluxionStream* stream, u64* value)
{
    u8 bytes[8];
    if (stream->mode == FLUXION_STREAM_WRITE)
    {
        for (int i = 0; i < 8; ++i)
        {
            bytes[i] = (u8)((*value >> (8 * i)) & 0xFFu);
        }
    }

    Fluxion_Stream_SerializeBytes(stream, bytes, sizeof(bytes));

    if (stream->mode == FLUXION_STREAM_READ)
    {
        u64 result = 0;
        for (int i = 0; i < 8; ++i)
        {
            result |= ((u64)bytes[i]) << (8 * i);
        }
        *value = result;
    }
}

void Fluxion_Stream_SerializeI32(FluxionStream* stream, i32* value)
{
    u32 bits;
    if (stream->mode == FLUXION_STREAM_WRITE)
    {
        memcpy(&bits, value, sizeof(bits));
    }
    Fluxion_Stream_SerializeU32(stream, &bits);
    if (stream->mode == FLUXION_STREAM_READ)
    {
        memcpy(value, &bits, sizeof(bits));
    }
}

void Fluxion_Stream_SerializeI64(FluxionStream* stream, i64* value)
{
    u64 bits;
    if (stream->mode == FLUXION_STREAM_WRITE)
    {
        memcpy(&bits, value, sizeof(bits));
    }
    Fluxion_Stream_SerializeU64(stream, &bits);
    if (stream->mode == FLUXION_STREAM_READ)
    {
        memcpy(value, &bits, sizeof(bits));
    }
}

void Fluxion_Stream_SerializeF32(FluxionStream* stream, f32* value)
{
    u32 bits;
    if (stream->mode == FLUXION_STREAM_WRITE)
    {
        memcpy(&bits, value, sizeof(bits));
    }
    Fluxion_Stream_SerializeU32(stream, &bits);
    if (stream->mode == FLUXION_STREAM_READ)
    {
        memcpy(value, &bits, sizeof(bits));
    }
}

usize Fluxion_Stream_GetPosition(const FluxionStream* stream)
{
    return stream->position;
}

bool Fluxion_Stream_HasOverflowed(const FluxionStream* stream)
{
    return stream->overflowed;
}

void Fluxion_Stream_Skip(FluxionStream* stream, usize size)
{
    if (stream->overflowed) return;

    if (stream->position + size > stream->capacity)
    {
        stream->overflowed = true;
        return;
    }
    stream->position += size;
}
