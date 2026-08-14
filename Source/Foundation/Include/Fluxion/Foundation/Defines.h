#pragma once

// ---------------------------------------------------------------------
// Compiler detection
// ---------------------------------------------------------------------

#if defined(__clang__)
    #define FLUXION_COMPILER_CLANG 1
#else
    #define FLUXION_COMPILER_CLANG 0
#endif

#if defined(_MSC_VER) && !FLUXION_COMPILER_CLANG
    #define FLUXION_COMPILER_MSVC 1
#else
    #define FLUXION_COMPILER_MSVC 0
#endif

#if defined(__GNUC__) && !FLUXION_COMPILER_CLANG
    #define FLUXION_COMPILER_GCC 1
#else
    #define FLUXION_COMPILER_GCC 0
#endif

#if (FLUXION_COMPILER_MSVC + FLUXION_COMPILER_CLANG + FLUXION_COMPILER_GCC) != 1
    #error "Fluxion: unsupported or ambiguous compiler"
#endif

// ---------------------------------------------------------------------
// Platform (OS) detection
// ---------------------------------------------------------------------

#if defined(_WIN32)
    #define FLUXION_PLATFORM_WINDOWS 1
#else
    #define FLUXION_PLATFORM_WINDOWS 0
#endif

#if defined(__ANDROID__)
    #define FLUXION_PLATFORM_ANDROID 1
#else
    #define FLUXION_PLATFORM_ANDROID 0
#endif

#if defined(__linux__) && !FLUXION_PLATFORM_ANDROID
    #define FLUXION_PLATFORM_LINUX 1
#else
    #define FLUXION_PLATFORM_LINUX 0
#endif

#if defined(__APPLE__)
    #include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
    #define FLUXION_PLATFORM_IOS 1
#else
    #define FLUXION_PLATFORM_IOS 0
#endif

#if defined(__APPLE__) && !FLUXION_PLATFORM_IOS
    #define FLUXION_PLATFORM_MACOS 1
#else
    #define FLUXION_PLATFORM_MACOS 0
#endif

#if (FLUXION_PLATFORM_WINDOWS + FLUXION_PLATFORM_LINUX + FLUXION_PLATFORM_MACOS + \
     FLUXION_PLATFORM_ANDROID + FLUXION_PLATFORM_IOS) != 1
    #error "Fluxion: unsupported or ambiguous platform"
#endif

// ---------------------------------------------------------------------
// Architecture detection
// ---------------------------------------------------------------------

#if defined(_M_X64) || defined(__x86_64__)
    #define FLUXION_ARCH_X64 1
#else
    #define FLUXION_ARCH_X64 0
#endif

#if defined(_M_IX86) || defined(__i386__)
    #define FLUXION_ARCH_X86 1
#else
    #define FLUXION_ARCH_X86 0
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
    #define FLUXION_ARCH_ARM64 1
#else
    #define FLUXION_ARCH_ARM64 0
#endif

#if defined(_M_ARM) || defined(__arm__)
    #define FLUXION_ARCH_ARM32 1
#else
    #define FLUXION_ARCH_ARM32 0
#endif

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
    #define FLUXION_ARCH_WASM 1
#else
    #define FLUXION_ARCH_WASM 0
#endif

#if (FLUXION_ARCH_X64 + FLUXION_ARCH_X86 + FLUXION_ARCH_ARM64 + FLUXION_ARCH_ARM32 + FLUXION_ARCH_WASM) != 1
    #error "Fluxion: unsupported or ambiguous architecture"
#endif

// How wide a pointer is, as a number rather than as a set of
// architectures to enumerate. Anything that has to know -- a serialised
// layout, a handle packed into a word -- asks this instead of asking
// which CPU it is on.
#if FLUXION_ARCH_X64 || FLUXION_ARCH_ARM64
    #define FLUXION_POINTER_BITS 64
#else
    #define FLUXION_POINTER_BITS 32
#endif

// ---------------------------------------------------------------------
// Byte order
// ---------------------------------------------------------------------
//
// Every architecture this engine targets runs little-endian: x86 and x64
// have no other mode, ARM64 is configurable in principle but runs
// little-endian on every operating system that ships on it, and WebAssembly
// fixes its memory as little-endian in the specification.
//
// This is detected rather than assumed all the same, so that code which
// writes bytes to a file or a socket can say which order it means and be
// checked on it, instead of being right only for as long as nobody ports
// the engine anywhere new.
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    #define FLUXION_LITTLE_ENDIAN 0
    #define FLUXION_BIG_ENDIAN 1
#else
    #define FLUXION_LITTLE_ENDIAN 1
    #define FLUXION_BIG_ENDIAN 0
#endif

// ---------------------------------------------------------------------
// Export / import (for future SHARED modules; STATIC modules ignore this)
// ---------------------------------------------------------------------

#if FLUXION_PLATFORM_WINDOWS
    #define FLUXION_EXPORT __declspec(dllexport)
    #define FLUXION_IMPORT __declspec(dllimport)
#else
    #define FLUXION_EXPORT __attribute__((visibility("default")))
    #define FLUXION_IMPORT
#endif

// ---------------------------------------------------------------------
// Compiler abstraction
// ---------------------------------------------------------------------

#if FLUXION_COMPILER_MSVC
    #define FLUXION_FORCE_INLINE __forceinline
    #define FLUXION_NO_INLINE __declspec(noinline)
#else
    #define FLUXION_FORCE_INLINE inline __attribute__((always_inline))
    #define FLUXION_NO_INLINE __attribute__((noinline))
#endif

#if FLUXION_COMPILER_MSVC
    #define FLUXION_LIKELY(x) (x)
    #define FLUXION_UNLIKELY(x) (x)
#else
    #define FLUXION_LIKELY(x) __builtin_expect(!!(x), 1)
    #define FLUXION_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// Not the standard `alignas` keyword: MSVC's C23 (`/std:clatest`) mode does
// not yet implement `alignas` in C mode (same gap as `alignof`/
// `max_align_t` — see FLUXION_DEFAULT_ALIGNMENT in Types.h). Compiler-
// specific attributes work on all three toolchains today.
#if FLUXION_COMPILER_MSVC
    #define FLUXION_ALIGN(N) __declspec(align(N))
#else
    #define FLUXION_ALIGN(N) __attribute__((aligned(N)))
#endif

#if FLUXION_COMPILER_MSVC
    #define FLUXION_DEBUG_BREAK() __debugbreak()
#elif FLUXION_COMPILER_CLANG
    #define FLUXION_DEBUG_BREAK() __builtin_debugtrap()
#else
    #define FLUXION_DEBUG_BREAK() __builtin_trap()
#endif

#if defined(__cplusplus)
    #if FLUXION_COMPILER_MSVC
        #define FLUXION_RESTRICT __restrict
    #else
        #define FLUXION_RESTRICT __restrict__
    #endif
#else
    #define FLUXION_RESTRICT restrict
#endif

// C23/C++11+ both have `thread_local` as a standard keyword.
#define FLUXION_THREAD_LOCAL thread_local

// ---------------------------------------------------------------------
// Utility macros
// ---------------------------------------------------------------------

#define FLUXION_ARRAY_COUNT(x) (sizeof(x) / sizeof((x)[0]))

#define FLUXION_BIT(x) (1u << (x))

#define FLUXION_UNUSED(x) ((void)(x))

#define FLUXION_STRINGIFY_IMPL(x) #x
#define FLUXION_STRINGIFY(x) FLUXION_STRINGIFY_IMPL(x)

#define FLUXION_CONCAT_IMPL(a, b) a##b
#define FLUXION_CONCAT(a, b) FLUXION_CONCAT_IMPL(a, b)
