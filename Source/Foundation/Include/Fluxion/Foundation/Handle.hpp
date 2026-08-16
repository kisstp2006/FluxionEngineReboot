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
