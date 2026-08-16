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

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(__cplusplus)
    #include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float  f32;
typedef double f64;

typedef size_t    usize;
typedef ptrdiff_t isize;

// A safe default alignment for allocations with no stricter requirement.
// Deliberately not `alignof(max_align_t)`/`_Alignof` — at the time this was
// written, MSVC's C23 (`/std:clatest`) mode did not yet expose `alignof` as
// a C keyword, so a fixed, portable constant is used instead. Revisit once
// MSVC's C23 support matures.
#define FLUXION_DEFAULT_ALIGNMENT ((usize)16)

static_assert(sizeof(u8) == 1, "Fluxion: unexpected size for u8");
static_assert(sizeof(u16) == 2, "Fluxion: unexpected size for u16");
static_assert(sizeof(u32) == 4, "Fluxion: unexpected size for u32");
static_assert(sizeof(u64) == 8, "Fluxion: unexpected size for u64");
static_assert(sizeof(i8) == 1, "Fluxion: unexpected size for i8");
static_assert(sizeof(i16) == 2, "Fluxion: unexpected size for i16");
static_assert(sizeof(i32) == 4, "Fluxion: unexpected size for i32");
static_assert(sizeof(i64) == 8, "Fluxion: unexpected size for i64");
static_assert(sizeof(f32) == 4, "Fluxion: unexpected size for f32");
static_assert(sizeof(f64) == 8, "Fluxion: unexpected size for f64");

#ifdef __cplusplus
}
#endif
