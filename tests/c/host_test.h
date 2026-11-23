/* Minimal assertion helpers for the host C tests (tests/c). */
#ifndef S23_HOST_TEST_H
#define S23_HOST_TEST_H

#include <stdio.h>
#include <stdlib.h>

static unsigned long long g_checks;
static int g_failures;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_failures++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                              \
    } while (0)

#define CHECK_EQ_U64(a, b)                                                             \
    do {                                                                               \
        unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
        g_checks++;                                                                    \
        if (_a != _b) {                                                                \
            g_failures++;                                                              \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (0x%llx != 0x%llx)\n",   \
                    __FILE__, __LINE__, #a, #b, _a, _b);                               \
        }                                                                              \
    } while (0)

static int test_report(const char *name)
{
    printf("%s: %.0f checks, %d failures\n", name, (double)g_checks, g_failures);
    return g_failures ? 1 : 0;
}

#endif
