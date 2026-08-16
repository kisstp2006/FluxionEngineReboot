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

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionStreamMode
{
    FLUXION_STREAM_READ,
    FLUXION_STREAM_WRITE,
} FluxionStreamMode;

// A single type for both directions (mode decides which way each
// Serialize* call actually goes) instead of separate Reader/Writer types
// with duplicated per-type logic -- code that serializes a struct only
// needs to be written once, not once per direction. Non-owning: the
// caller allocates and owns `buffer` for as long as the stream is used.
typedef struct FluxionStream
{
    FluxionStreamMode mode;
    u8* buffer;
    usize capacity;
    usize position;

    // Set once a Serialize* call would exceed capacity; every call after
    // that becomes a no-op instead of reading/writing out of bounds.
    // Checked, not a crash -- callers inspect it once at the end via
    // Fluxion_Stream_HasOverflowed.
    bool overflowed;
} FluxionStream;

void Fluxion_MemoryStream_InitWriter(FluxionStream* stream, void* buffer, usize capacity);
void Fluxion_MemoryStream_InitReader(FluxionStream* stream, const void* buffer, usize size);

static inline bool Fluxion_Stream_IsReading(const FluxionStream* stream) { return stream->mode == FLUXION_STREAM_READ; }
static inline bool Fluxion_Stream_IsWriting(const FluxionStream* stream) { return stream->mode == FLUXION_STREAM_WRITE; }

// Every Serialize* function is symmetric: the same call reads or writes
// depending on stream->mode, so the shape of a record is written down
// once rather than once per direction -- two copies being the way a
// reader and a writer come to disagree about the same bytes. All
// multi-byte values are explicitly encoded/decoded least-significant-
// byte-first on the wire, regardless of host byte order -- not a
// memcpy-of-the-native-representation assumption.
void Fluxion_Stream_SerializeBytes(FluxionStream* stream, void* data, usize size);
void Fluxion_Stream_SerializeU8(FluxionStream* stream, u8* value);
void Fluxion_Stream_SerializeU16(FluxionStream* stream, u16* value);
void Fluxion_Stream_SerializeU32(FluxionStream* stream, u32* value);
void Fluxion_Stream_SerializeU64(FluxionStream* stream, u64* value);
void Fluxion_Stream_SerializeI32(FluxionStream* stream, i32* value);
void Fluxion_Stream_SerializeI64(FluxionStream* stream, i64* value);
void Fluxion_Stream_SerializeF32(FluxionStream* stream, f32* value);

usize Fluxion_Stream_GetPosition(const FluxionStream* stream);
bool Fluxion_Stream_HasOverflowed(const FluxionStream* stream);

// Advances the cursor by `size` bytes without reading or writing any of
// them -- used to skip data whose meaning isn't known at this call site
// (e.g. BinarySerializer skipping a property tag it doesn't recognize).
void Fluxion_Stream_Skip(FluxionStream* stream, usize size);

#ifdef __cplusplus
}
#endif
