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
