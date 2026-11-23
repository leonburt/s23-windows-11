/* Regression guard for the TgCreateAllocation BO range check (drivers/s23gpu/ddi.c).
 * The condition is extracted verbatim from ddi.c; bo_offset and size come from user-mode private data
 * and must not be able to wrap the check (32-bit overflow fixed in #3). */
#include <stddef.h>
#include "host_test.h"

typedef unsigned long long ULONGLONG;
typedef struct { size_t Size; } BO;
typedef struct { unsigned bo_offset; unsigned size; } DESC;
typedef struct { BO *Bo; DESC Desc; } ALLOC;

#include "alloc_range_check.inc"

static int rejects(BO *bo, unsigned off, unsigned size)
{
    ALLOC a;
    ALLOC *al = &a;
    a.Bo = bo;
    a.Desc.bo_offset = off;
    a.Desc.size = size;
    return S23_ALLOC_RANGE_REJECT(al) ? 1 : 0;
}

int main(void)
{
    BO bo = { 0x2000 };

    CHECK(rejects(NULL, 0, 0x1000));                 /* no BO */
    CHECK(!rejects(&bo, 0, 0x2000));                 /* exact fit */
    CHECK(!rejects(&bo, 0x1000, 0x1000));            /* tail fit */
    CHECK(rejects(&bo, 0x1000, 0x1001));             /* one byte past the end */
    CHECK(rejects(&bo, 0x2000, 1));                  /* starts at the end */
    /* 32-bit wrap: 0xFFFFF000 + 0x2000 == 0x1000 in 32 bits, must still be rejected */
    CHECK(rejects(&bo, 0xFFFFF000u, 0x2000));
    CHECK(rejects(&bo, 0xFFFFFFFFu, 1));
    CHECK(rejects(&bo, 1, 0xFFFFFFFFu));
    CHECK(rejects(&bo, 0x80000000u, 0x80000000u));   /* sum == 2^32 */

    BO big = { (size_t)0x100000000ULL };             /* 4 GiB BO: full 32-bit range is legal */
    CHECK(!rejects(&big, 0xFFFFF000u, 0x1000));
    CHECK(rejects(&big, 0xFFFFF000u, 0x1001));

    return test_report("test_alloc_range");
}
