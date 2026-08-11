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
// depending on stream->mode (FArchive's <<-both-ways pattern). All
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
