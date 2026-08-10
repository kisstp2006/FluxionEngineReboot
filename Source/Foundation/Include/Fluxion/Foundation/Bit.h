#pragma once

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

#if FLUXION_COMPILER_MSVC
    #include <intrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline u32 Fluxion_PopCount32(u32 value)
{
#if FLUXION_COMPILER_MSVC
    return (u32)__popcnt(value);
#else
    return (u32)__builtin_popcount(value);
#endif
}

static inline u32 Fluxion_PopCount64(u64 value)
{
#if FLUXION_COMPILER_MSVC
    return (u32)__popcnt64(value);
#else
    return (u32)__builtin_popcountll(value);
#endif
}

static inline u32 Fluxion_CountLeadingZeros32(u32 value)
{
    if (value == 0) return 32u;
#if FLUXION_COMPILER_MSVC
    unsigned long index;
    _BitScanReverse(&index, value);
    return 31u - (u32)index;
#else
    return (u32)__builtin_clz(value);
#endif
}

static inline u32 Fluxion_CountTrailingZeros32(u32 value)
{
    if (value == 0) return 32u;
#if FLUXION_COMPILER_MSVC
    unsigned long index;
    _BitScanForward(&index, value);
    return (u32)index;
#else
    return (u32)__builtin_ctz(value);
#endif
}

static inline u32 Fluxion_RotateLeft32(u32 value, u32 shift)
{
    shift &= 31u;
    return (value << shift) | (value >> ((32u - shift) & 31u));
}

static inline u32 Fluxion_RotateRight32(u32 value, u32 shift)
{
    shift &= 31u;
    return (value >> shift) | (value << ((32u - shift) & 31u));
}

// Smallest power of two that is >= value. Returns 1 for value <= 1.
static inline u32 Fluxion_NextPowerOfTwo32(u32 value)
{
    if (value <= 1u) return 1u;
    return 1u << (32u - Fluxion_CountLeadingZeros32(value - 1u));
}

#ifdef __cplusplus
}
#endif
