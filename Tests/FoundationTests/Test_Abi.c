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

#include "TestFramework.h"

#include <Fluxion/Foundation/Bit.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

#include <string.h>

// The properties an ARM64 target depends on, pinned where they can be
// checked rather than left as things everybody assumed.
//
// None of these can fail on the machine that runs them today: every one
// holds on x86-64 as well. That is the point. A build for another
// architecture would trip whichever of them stopped being true there --
// but so would a change made HERE that only happens to work because of
// what x86 forgives, and that is the one this catches early, on the
// machine the change was written on.
//
// What is deliberately not here: anything that only a real ARM64 build
// could answer. A test cannot tell you your code runs on a CPU you never
// ran it on. It can tell you that the code has not quietly grown a reason
// why it could not.

// Two values whose bytes are all different, so that a byte written in the
// wrong order or read from the wrong offset gives a wrong answer rather
// than the right one by symmetry. 0x01020304 reversed is a different
// number; 0x0000FFFF reversed is not, which is why it is not used.
#define FLUXION_TEST_ABI_U32 0x01020304u
#define FLUXION_TEST_ABI_U64 0x0102030405060708ull

typedef struct FluxionTestAbiMixed
{
    u8 first;
    u32 second;
    u8 third;
    u64 fourth;
} FluxionTestAbiMixed;

void Test_Abi_Run(TestContext* ctx)
{
    // --- Exactly one platform and one architecture ----------------------

    // Defines.h refuses to compile when these do not add up, so reaching
    // this line at all is most of the check. Reading them back makes the
    // build's own answer visible to a failing test rather than only to a
    // compiler error.
    const int platforms = FLUXION_PLATFORM_WINDOWS + FLUXION_PLATFORM_LINUX
        + FLUXION_PLATFORM_MACOS + FLUXION_PLATFORM_ANDROID + FLUXION_PLATFORM_IOS;
    const int architectures = FLUXION_ARCH_X64 + FLUXION_ARCH_X86
        + FLUXION_ARCH_ARM64 + FLUXION_ARCH_ARM32 + FLUXION_ARCH_WASM;
    TEST_CHECK(ctx, platforms == 1);
    TEST_CHECK(ctx, architectures == 1);

    // --- Pointers, and the integer type that has to hold one ------------

    // The two 64-bit targets this engine is built for are x86-64 and
    // ARM64, and the handle and allocator interfaces below assume a
    // pointer fits in a usize. An architecture where it did not would
    // break both silently.
    TEST_CHECK(ctx, sizeof(void*) * 8u == (usize)FLUXION_POINTER_BITS);
    TEST_CHECK(ctx, sizeof(usize) == sizeof(void*));
    TEST_CHECK(ctx, sizeof(isize) == sizeof(void*));
#if FLUXION_ARCH_X64 || FLUXION_ARCH_ARM64
    TEST_CHECK(ctx, sizeof(void*) == 8);
#endif

    // --- The fixed-width types are actually fixed -----------------------

    // These are typedefs over <stdint.h>, so the sizes come from the
    // toolchain. A cross-compiler configured for the wrong data model --
    // the classic way an AArch64 build goes wrong before it ever runs --
    // shows up here rather than as a file that reads back as noise.
    TEST_CHECK(ctx, sizeof(u8) == 1 && sizeof(i8) == 1);
    TEST_CHECK(ctx, sizeof(u16) == 2 && sizeof(i16) == 2);
    TEST_CHECK(ctx, sizeof(u32) == 4 && sizeof(i32) == 4);
    TEST_CHECK(ctx, sizeof(u64) == 8 && sizeof(i64) == 8);
    TEST_CHECK(ctx, sizeof(f32) == 4 && sizeof(f64) == 8);

    // `char` being signed or unsigned is left to the compiler by the
    // standard, and the two disagree on ARM: it is unsigned there and
    // signed on x86. Anything comparing a char against a negative number
    // -- an end-of-input marker, a high byte read out of a name -- gets a
    // different answer on the two. Nothing here relies on it; this states
    // that it must stay that way, because the engine uses u8 for bytes
    // and char only for text.
    TEST_CHECK(ctx, sizeof(char) == 1);

    // --- Byte order -----------------------------------------------------

    {
        const u32 word = FLUXION_TEST_ABI_U32;
        u8 bytes[sizeof(word)];
        memcpy(bytes, &word, sizeof(word));

#if FLUXION_LITTLE_ENDIAN
        TEST_CHECK(ctx, bytes[0] == 0x04 && bytes[1] == 0x03 && bytes[2] == 0x02 && bytes[3] == 0x01);
#else
        TEST_CHECK(ctx, bytes[0] == 0x01 && bytes[1] == 0x02 && bytes[2] == 0x03 && bytes[3] == 0x04);
#endif
        TEST_CHECK(ctx, FLUXION_LITTLE_ENDIAN + FLUXION_BIG_ENDIAN == 1);
    }

    // --- Reading a wide value from an address that is not aligned -------

    // x86 loads an unaligned word without complaint. ARM64 mostly does
    // too, but not for every instruction the compiler may pick, and a
    // compiler is entitled to assume a `u32*` is aligned whatever the
    // hardware would have tolerated -- which is how this becomes a
    // miscompilation rather than a trap.
    //
    // memcpy is the way to say "these bytes, whatever they are aligned
    // to". Checked here at every offset within a word, so that the
    // pattern the engine's file readers use is known to be right rather
    // than believed to be.
    {
        u8 storage[sizeof(u64) * 2];
        for (usize offset = 0; offset < sizeof(u64); ++offset)
        {
            u64 readBack = 0;
            memset(storage, 0, sizeof(storage));
            {
                const u64 value = FLUXION_TEST_ABI_U64;
                memcpy(storage + offset, &value, sizeof(value));
            }
            memcpy(&readBack, storage + offset, sizeof(readBack));
            TEST_CHECK(ctx, readBack == FLUXION_TEST_ABI_U64);
        }
    }

    // --- Struct layout is the compiler's, and nothing assumes otherwise -

    // No header in this engine packs a struct, so every one of them is
    // laid out with the padding its target wants. What must hold is that
    // nothing has come to depend on a particular size: a struct written
    // out field by field survives a layout change, one written out with a
    // single memcpy of the whole thing does not.
    {
        FluxionTestAbiMixed mixed;
        memset(&mixed, 0, sizeof(mixed));
        mixed.first = 1;
        mixed.second = FLUXION_TEST_ABI_U32;
        mixed.third = 3;
        mixed.fourth = FLUXION_TEST_ABI_U64;

        // Padded out to at least the sum of its parts, and aligned to its
        // widest member. Both are true on x86-64 and on ARM64; a target
        // where they were not would need every binary format revisited.
        TEST_CHECK(ctx, sizeof(FluxionTestAbiMixed) >= sizeof(u8) + sizeof(u32) + sizeof(u8) + sizeof(u64));
        TEST_CHECK(ctx, sizeof(FluxionTestAbiMixed) % sizeof(u64) == 0);

        TEST_CHECK(ctx, mixed.second == FLUXION_TEST_ABI_U32);
        TEST_CHECK(ctx, mixed.fourth == FLUXION_TEST_ABI_U64);
    }

    // --- The bit helpers, and the two ways they are worked out ----------

    // Fluxion_PopCount32/64 take an x86 instruction where the compiler
    // offers one and count the bits themselves everywhere else. The two
    // paths have to agree, and only the machine that has both can say so
    // -- which is this one. An ARM build takes the second path alone,
    // with nothing left to compare it against, so it is compared here.
    {
        const u64 patterns[] =
        {
            0ull,
            1ull,
            FLUXION_TEST_ABI_U64,
            0xFFFFFFFFFFFFFFFFull,
            0x8000000000000000ull,
            0x0000000100000000ull,
            0xAAAAAAAAAAAAAAAAull,
            0x5555555555555555ull,
        };

        for (usize i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i)
        {
            const u64 value = patterns[i];
            TEST_CHECK(ctx, Fluxion_PopCount64(value) == Fluxion_PopCountPortable64(value));
            TEST_CHECK(ctx, Fluxion_PopCount32((u32)value) == Fluxion_PopCountPortable64((u32)value));
        }

        // And against an answer worked out a third way, one bit at a
        // time, so that both paths being wrong in the same way would
        // still be caught.
        for (usize i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i)
        {
            u64 remaining = patterns[i];
            u32 counted = 0;
            while (remaining != 0)
            {
                counted += (u32)(remaining & 1ull);
                remaining >>= 1;
            }
            TEST_CHECK(ctx, Fluxion_PopCount64(patterns[i]) == counted);
        }

        TEST_CHECK(ctx, Fluxion_PopCount64(0xFFFFFFFFFFFFFFFFull) == 64u);
        TEST_CHECK(ctx, Fluxion_PopCount32(0xFFFFFFFFu) == 32u);
    }

    // --- Shifting by the full width of the type -------------------------

    // Shifting a 32-bit value by 32 is undefined, and the two
    // architectures do visibly different things with it: x86 masks the
    // count to five bits and shifts by zero, ARM shifts the value away to
    // nothing. Code that gets this wrong therefore works on one and not
    // the other. The rotate helpers mask the count themselves rather than
    // leaving it to the hardware, which is what these check.
    TEST_CHECK(ctx, Fluxion_RotateLeft32(FLUXION_TEST_ABI_U32, 0) == FLUXION_TEST_ABI_U32);
    TEST_CHECK(ctx, Fluxion_RotateLeft32(FLUXION_TEST_ABI_U32, 32) == FLUXION_TEST_ABI_U32);
    TEST_CHECK(ctx, Fluxion_RotateRight32(FLUXION_TEST_ABI_U32, 32) == FLUXION_TEST_ABI_U32);
    TEST_CHECK(ctx, Fluxion_RotateLeft32(FLUXION_TEST_ABI_U32, 8) == 0x02030401u);
    TEST_CHECK(ctx, Fluxion_RotateRight32(FLUXION_TEST_ABI_U32, 8) == 0x04010203u);
    TEST_CHECK(ctx, Fluxion_RotateRight32(Fluxion_RotateLeft32(FLUXION_TEST_ABI_U32, 13), 13) == FLUXION_TEST_ABI_U32);
}
