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

#include <type_traits>

namespace Fluxion::Foundation
{

// A typed flag set over an enum (plain or `enum class`). Deliberately
// requires wrapping a raw enum value in BitFlags<T> before combining it
// with `|` -- prevents accidentally OR-ing bare enum values without ever
// going through a flag set.
template<typename EnumT>
class BitFlags
{
public:
    using Underlying = std::underlying_type_t<EnumT>;

    constexpr BitFlags() = default;
    constexpr BitFlags(EnumT value) : m_bits(static_cast<Underlying>(value)) {}

    constexpr BitFlags& Set(EnumT value)
    {
        m_bits |= static_cast<Underlying>(value);
        return *this;
    }

    constexpr BitFlags& Clear(EnumT value)
    {
        m_bits &= static_cast<Underlying>(~static_cast<Underlying>(value));
        return *this;
    }

    constexpr bool Has(EnumT value) const
    {
        const Underlying bits = static_cast<Underlying>(value);
        return (m_bits & bits) == bits;
    }

    constexpr bool Any() const { return m_bits != 0; }

    constexpr Underlying Raw() const { return m_bits; }

    friend constexpr BitFlags operator|(BitFlags a, BitFlags b)
    {
        return BitFlags(static_cast<Underlying>(a.m_bits | b.m_bits));
    }

    friend constexpr BitFlags operator&(BitFlags a, BitFlags b)
    {
        return BitFlags(static_cast<Underlying>(a.m_bits & b.m_bits));
    }

    constexpr BitFlags& operator|=(BitFlags other)
    {
        m_bits |= other.m_bits;
        return *this;
    }

private:
    explicit constexpr BitFlags(Underlying bits) : m_bits(bits) {}

    Underlying m_bits = 0;
};

} // namespace Fluxion::Foundation
