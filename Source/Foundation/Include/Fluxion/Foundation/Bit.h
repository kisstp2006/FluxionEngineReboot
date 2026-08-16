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
