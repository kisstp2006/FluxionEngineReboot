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
