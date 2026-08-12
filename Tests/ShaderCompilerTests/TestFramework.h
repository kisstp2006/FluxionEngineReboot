#pragma once

#include <cstdio>

struct TestContext
{
    int failures = 0;
};

#define TEST_CHECK(ctx, condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "  FAIL: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            (ctx).failures++; \
        } \
    } while (0)
