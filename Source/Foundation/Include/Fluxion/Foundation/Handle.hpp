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

// A raw C handle that points at nothing.
//
// The C handles have no default of their own, and that is a trap worth
// naming: `FluxionRHITextureHandle texture{}` gives index ZERO, and index
// zero is a perfectly good slot -- FLUXION_HANDLE_IS_VALID rejects only
// the all-ones index. A struct full of zeroed handles therefore reads as
// though it holds every one of them, and cleaning it up frees whatever
// happens to be sitting in slot zero, which belongs to somebody else.
//
// The Handle above cannot go wrong that way because its default is
// written into the type. This is for the C ones, which cannot have that.
template<typename RawHandle>
constexpr RawHandle NoHandle()
{
    return RawHandle{ kHandleInvalidIndex, 0 };
}

} // namespace Fluxion::Foundation
