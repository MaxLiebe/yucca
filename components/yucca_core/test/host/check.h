// Minimal test harness: CHECK(cond) and CHECK_NEAR(a, b), then RUN(test_fn).
#pragma once

#include <cmath>
#include <cstdio>

inline int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b)                                                       \
    do {                                                                       \
        double a_ = (a), b_ = (b);                                             \
        if (std::fabs(a_ - b_) > 1e-4) {                                       \
            std::printf("%s:%d: %s = %f, expected %f\n", __FILE__, __LINE__, #a, a_, b_); \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

#define RUN(fn) fn()

#define DONE(name)                                                             \
    do {                                                                       \
        if (g_failures) {                                                      \
            std::printf("%s: %d failure(s)\n", name, g_failures);              \
            return 1;                                                          \
        }                                                                      \
        std::printf("%s passed\n", name);                                      \
        return 0;                                                              \
    } while (0)
