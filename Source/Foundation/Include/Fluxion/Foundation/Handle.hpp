#pragma once

#include <Fluxion/Foundation/Types.h>

namespace Fluxion::Foundation
{

inline constexpr u32 kHandleInvalidIndex = 0xFFFFFFFFu; // matches FLUXION_HANDLE_INVALID_INDEX

// A distinct, strongly-typed index+generation handle -- Tag is a marker
// type (typically an incomplete `struct SomeTag;` declared just for this)
// that makes Handle<TextureTag> and Handle<BufferTag> incompatible at
// compile time, so passing one where the other is expected is a build
// error, not a runtime bug. Same { u32 index; u32 generation; } layout as
// the C FLUXION_DEFINE_HANDLE-generated structs -- FromRaw/ToRaw convert
// to/from any matching raw C handle type by value (no reinterpret_cast
// needed, since the fields just get copied across).
template<typename Tag>
struct Handle
{
    u32 index = kHandleInvalidIndex;
    u32 generation = 0;

    constexpr bool IsValid() const { return index != kHandleInvalidIndex; }

    template<typename RawHandle>
    static constexpr Handle FromRaw(RawHandle raw)
    {
        return Handle{ raw.index, raw.generation };
    }

    template<typename RawHandle>
    constexpr RawHandle ToRaw() const
    {
        return RawHandle{ index, generation };
    }

    friend constexpr bool operator==(const Handle& a, const Handle& b)
    {
        return a.index == b.index && a.generation == b.generation;
    }

    friend constexpr bool operator!=(const Handle& a, const Handle& b)
    {
        return !(a == b);
    }
};

} // namespace Fluxion::Foundation
