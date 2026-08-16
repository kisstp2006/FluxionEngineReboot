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

#include <Fluxion/Foundation/UUID.h>

#include <Fluxion/Foundation/Atomic.h>
#include <Fluxion/Foundation/Defines.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

// The generator has to answer one question well: can two ids ever come out
// the same? Two runs of the same program are the hard case -- a per-second
// clock and a shared starting point would have two processes launched in
// the same second produce byte-identical sequences, and anything that
// stores an id and expects to find one thing behind it later would then be
// looking at two.
//
// So the state is mixed from three things that do not agree with each
// other: the clock at its finest resolution, where the stack happens to
// sit this run, and a count that rises with every id ever asked for. The
// count alone makes ids within one run distinct however the seed turned
// out; the other two make the runs differ.

// SplitMix64's three constants. The first is the odd number nearest to
// 2^64 divided by the golden ratio, which is what makes stepping by it
// walk the whole 64-bit range without settling into a short cycle; the
// other two are its published multipliers, chosen so that every input bit
// reaches every output bit.
#define FLUXION_UUID_MIX_STEP        0x9E3779B97F4A7C15ull
#define FLUXION_UUID_MIX_MULTIPLIER1 0xBF58476D1CE4E5B9ull
#define FLUXION_UUID_MIX_MULTIPLIER2 0x94D049BB133111EBull

// Which byte carries the RFC 4122 version, which carries the variant, and
// what each has to end up as. Named because "byte 6" says nothing about
// why byte 6.
#define FLUXION_UUID_VERSION_BYTE   6
#define FLUXION_UUID_VARIANT_BYTE   8
#define FLUXION_UUID_VERSION_MASK   0x0Fu
#define FLUXION_UUID_VERSION_RANDOM 0x40u
#define FLUXION_UUID_VARIANT_MASK   0x3Fu
#define FLUXION_UUID_VARIANT_RFC    0x80u

// How far the one-time seeding has got. Three states rather than a flag
// because a second caller arriving mid-way has to be told to wait rather
// than to go ahead on a seed that is only half written.
#define FLUXION_UUID_SEED_UNTOUCHED 0
#define FLUXION_UUID_SEED_BUILDING  1
#define FLUXION_UUID_SEED_READY     2

static FluxionAtomicI32 s_seedState;
static FluxionAtomicI64 s_counter;
static u64 s_seed;

// One round of SplitMix64. Every bit of the result depends on every bit of
// the input, which is what lets a counter that only ever adds one produce
// values with no visible relation to each other.
static u64 Fluxion_UUID_Mix(u64 value)
{
    value += FLUXION_UUID_MIX_STEP;
    value = (value ^ (value >> 30)) * FLUXION_UUID_MIX_MULTIPLIER1;
    value = (value ^ (value >> 27)) * FLUXION_UUID_MIX_MULTIPLIER2;
    return value ^ (value >> 31);
}

static void Fluxion_UUID_EnsureSeeded(void)
{
    struct timespec now;
    u64 stackAddress;
    i32 expected = FLUXION_UUID_SEED_UNTOUCHED;

    if (Fluxion_AtomicI32_Load(&s_seedState) == FLUXION_UUID_SEED_READY) return;

    if (!Fluxion_AtomicI32_CompareExchange(&s_seedState, &expected, FLUXION_UUID_SEED_BUILDING))
    {
        // Somebody else claimed the seeding. Waiting is bounded by the few
        // instructions below, and happens at most once in the life of the
        // process.
        while (Fluxion_AtomicI32_Load(&s_seedState) != FLUXION_UUID_SEED_READY) { }
        return;
    }

    now.tv_sec = 0;
    now.tv_nsec = 0;

    // Two ways of asking the same clock the same question. clock_gettime
    // is the POSIX one and has been in every POSIX C library including
    // Android's from the start; timespec_get is the ISO C one, which is
    // what Windows offers and what Android's only gained at API level 29.
    // Taking the POSIX one wherever POSIX exists means one fewer platform
    // level to care about.
#if FLUXION_PLATFORM_WINDOWS
    (void)timespec_get(&now, TIME_UTC);
#else
    (void)clock_gettime(CLOCK_REALTIME, &now);
#endif

    // Where this frame sits differs between runs wherever the loader
    // places things differently each time, and costs nothing to read.
    stackAddress = (u64)(uintptr_t)(void*)&now;

    s_seed = Fluxion_UUID_Mix((u64)now.tv_sec);
    s_seed ^= Fluxion_UUID_Mix((u64)now.tv_nsec);
    s_seed ^= Fluxion_UUID_Mix(stackAddress);

    Fluxion_AtomicI64_Store(&s_counter, 0);
    Fluxion_AtomicI32_Store(&s_seedState, FLUXION_UUID_SEED_READY);
}

FluxionUUID Fluxion_UUID_Generate(void)
{
    FluxionUUID id;
    u64 step;
    u64 low, high;

    Fluxion_UUID_EnsureSeeded();

    // Two ids never share a step, so two ids never share both halves --
    // however many callers ask at once.
    step = (u64)Fluxion_AtomicI64_Increment(&s_counter);

    // The two halves are mixed from different points of the sequence
    // rather than from one value split in two, so neither half can be
    // worked back from the other.
    low = Fluxion_UUID_Mix(s_seed + step * 2u);
    high = Fluxion_UUID_Mix(s_seed + step * 2u + 1u);

    for (int i = 0; i < 8; ++i)
    {
        id.bytes[i] = (u8)(low >> (i * 8));
        id.bytes[i + 8] = (u8)(high >> (i * 8));
    }

    id.bytes[FLUXION_UUID_VERSION_BYTE] =
        (u8)((id.bytes[FLUXION_UUID_VERSION_BYTE] & FLUXION_UUID_VERSION_MASK) | FLUXION_UUID_VERSION_RANDOM);
    id.bytes[FLUXION_UUID_VARIANT_BYTE] =
        (u8)((id.bytes[FLUXION_UUID_VARIANT_BYTE] & FLUXION_UUID_VARIANT_MASK) | FLUXION_UUID_VARIANT_RFC);

    return id;
}

bool Fluxion_UUID_Equals(FluxionUUID a, FluxionUUID b)
{
    return memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

bool Fluxion_UUID_IsNil(FluxionUUID id)
{
    for (int i = 0; i < 16; ++i)
    {
        if (id.bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void Fluxion_UUID_ToString(FluxionUUID id, char outBuffer[37])
{
    snprintf(outBuffer, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        id.bytes[0], id.bytes[1], id.bytes[2], id.bytes[3],
        id.bytes[4], id.bytes[5],
        id.bytes[6], id.bytes[7],
        id.bytes[8], id.bytes[9],
        id.bytes[10], id.bytes[11], id.bytes[12], id.bytes[13], id.bytes[14], id.bytes[15]);
}
