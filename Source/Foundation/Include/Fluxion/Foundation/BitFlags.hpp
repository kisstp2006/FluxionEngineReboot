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
