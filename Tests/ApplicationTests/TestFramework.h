#pragma once

#include <stdio.h>

// Minimal, zero-dependency test harness — no external test framework.
typedef struct TestContext
{
    int failures;
} TestContext;

#define TEST_CHECK(ctx, condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "  FAIL: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            (ctx)->failures++; \
        } \
    } while (0)
