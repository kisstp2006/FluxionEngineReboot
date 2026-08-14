#pragma once

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

#if FLUXION_COMPILER_MSVC
    #include <intrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Counting bits without asking the hardware to. Every step folds pairs of
// counts into the next wider field: first the bits of each pair of bits,
// then each nibble, then each byte, and the final multiply sums all eight
// byte counts into the top byte in one go.
//
// This is here because __popcnt is not a compiler intrinsic but an x86
// one: MSVC offers it on x86 and x64 only, so the same source that builds
// on a desktop would not build for an ARM target at all. Rather than
// reach for a second architecture's intrinsic that cannot be tried on the
// machine this is written on, the non-x86 path counts the bits itself --
// slower than one instruction, right on every architecture, and checked
// against the intrinsic where both exist.
#define FLUXION_BIT_PAIR_MASK  0x5555555555555555ull
#define FLUXION_BIT_NIBBLE_LOW 0x3333333333333333ull
#define FLUXION_BIT_BYTE_LOW   0x0F0F0F0F0F0F0F0Full
#define FLUXION_BIT_BYTE_ONES  0x0101010101010101ull

static inline u32 Fluxion_PopCountPortable64(u64 value)
{
    value = value - ((value >> 1) & FLUXION_BIT_PAIR_MASK);
    value = (value & FLUXION_BIT_NIBBLE_LOW) + ((value >> 2) & FLUXION_BIT_NIBBLE_LOW);
    value = (value + (value >> 4)) & FLUXION_BIT_BYTE_LOW;
    return (u32)((value * FLUXION_BIT_BYTE_ONES) >> 56);
}

static inline u32 Fluxion_PopCount32(u32 value)
{
#if FLUXION_COMPILER_MSVC && (FLUXION_ARCH_X64 || FLUXION_ARCH_X86)
    return (u32)__popcnt(value);
#elif FLUXION_COMPILER_MSVC
    return Fluxion_PopCountPortable64((u64)value);
#else
    return (u32)__builtin_popcount(value);
#endif
}

static inline u32 Fluxion_PopCount64(u64 value)
{
#if FLUXION_COMPILER_MSVC && FLUXION_ARCH_X64
    return (u32)__popcnt64(value);
#elif FLUXION_COMPILER_MSVC
    // __popcnt64 is x64 only -- it is missing on 32-bit x86 as well as on
    // every ARM target, so this covers both.
    return Fluxion_PopCountPortable64(value);
#else
    return (u32)__builtin_popcountll(value);
#endif
}

// _BitScanReverse and _BitScanForward, unlike __popcnt above, are offered
// by MSVC on every architecture it targets, so these two need no second
// path.
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
