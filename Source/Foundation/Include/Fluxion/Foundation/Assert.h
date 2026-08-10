#pragma once

#include <Fluxion/Foundation/Defines.h>

#ifdef __cplusplus
extern "C" {
#endif

void Fluxion_AssertReport(const char* expression, const char* file, int line, const char* message);

#ifdef __cplusplus
}
#endif

#if !defined(NDEBUG)
    #define FLUXION_ASSERT(condition) \
        do { \
            if (!(condition)) { \
                Fluxion_AssertReport(#condition, __FILE__, __LINE__, NULL); \
                FLUXION_DEBUG_BREAK(); \
            } \
        } while (0)

    #define FLUXION_ASSERT_MSG(condition, message) \
        do { \
            if (!(condition)) { \
                Fluxion_AssertReport(#condition, __FILE__, __LINE__, (message)); \
                FLUXION_DEBUG_BREAK(); \
            } \
        } while (0)
#else
    #define FLUXION_ASSERT(condition) FLUXION_UNUSED(condition)
    #define FLUXION_ASSERT_MSG(condition, message) do { FLUXION_UNUSED(condition); FLUXION_UNUSED(message); } while (0)
#endif

// Unlike FLUXION_ASSERT, the expression is always evaluated (side effects
// preserved) even in release builds.
#define FLUXION_VERIFY(expression) \
    do { \
        if (!(expression)) { \
            Fluxion_AssertReport(#expression, __FILE__, __LINE__, "VERIFY failed"); \
            FLUXION_DEBUG_BREAK(); \
        } \
    } while (0)

#define FLUXION_FATAL(message) \
    do { \
        Fluxion_AssertReport("FLUXION_FATAL", __FILE__, __LINE__, (message)); \
        FLUXION_DEBUG_BREAK(); \
    } while (0)
