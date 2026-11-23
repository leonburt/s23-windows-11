/* GPU VA interval allocator from drivers/s23gpu/hw.c (VaInitLocked / VaAllocLocked / VaReserveLocked /
 * VaFreeLocked / VaDestroyLocked), extracted verbatim by run_c_tests.py and run against a reference model.
 *
 * Invariants checked after every operation:
 *   - free list sorted, page aligned, inside [S23GPU_VA_USER_START, S23GPU_VA_USER_END), non-empty nodes
 *   - free nodes never touch or overlap (fully coalesced)
 *   - live allocations never overlap each other or a free node
 *   - free bytes + allocated bytes + bytes lost to failed node allocations == whole user range
 *   - pool accounting: live pool blocks == free-list nodes (no node leaks, no double frees)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "host_test.h"

/* ---- user-mode shims for the kernel names used by the extracted code ---- */
#define VOID void
typedef unsigned long long ULONGLONG;
typedef size_t SIZE_T;
typedef unsigned char BOOLEAN;
#define TRUE 1
#define FALSE 0
#define PAGE_SIZE 0x1000ULL
#define POOL_FLAG_NON_PAGED 0x40ULL
#define S23GPU_POOL_TAG 0x55473332u
#define CONTAINING_RECORD(addr, type, field) ((type *)((char *)(addr) - offsetof(type, field)))

static long g_pool_live;      /* outstanding pool blocks */
static long g_fail_next = -1; /* >= 0: fail the allocation after that many successful ones */

static void *ExAllocatePool2(ULONGLONG flags, SIZE_T bytes, unsigned tag)
{
    (void)flags; (void)tag;
    if (g_fail_next == 0) { g_fail_next = -1; return NULL; }
    if (g_fail_next > 0) g_fail_next--;
    g_pool_live++;
    return calloc(1, bytes);
}

static void ExFreePoolWithTag(void *p, unsigned tag)
{
    (void)tag;
    if (p) { g_pool_live--; free(p); }
}

#include "va_alloc_extracted.inc"

/* ---- reference model ---- */
#define TOTAL (S23GPU_VA_USER_END - S23GPU_VA_USER_START)
#define MAX_LIVE 512

typedef struct { ULONGLONG va, size; } RANGE;
static RANGE g_live[MAX_LIVE];
static int g_nlive;
static ULONGLONG g_lost;     /* VA dropped by VaFreeLocked when it could not allocate a node */

static unsigned long long g_rng = 0x5323A740ULL;
static unsigned long long rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

static ULONGLONG free_bytes(int *nodes)
{
    ULONGLONG sum = 0;
    int n = 0;
    for (VA_NODE *c = g_VaFreeHead; c; c = c->Next) { sum += c->End - c->Start; n++; }
    if (nodes) *nodes = n;
    return sum;
}

static int overlaps(ULONGLONG a, ULONGLONG an, ULONGLONG b, ULONGLONG bn) { return a < b + bn && b < a + an; }

static int check_invariants(const char *ctx)
{
    int before = g_failures, nodes = 0;
    int badNode = 0, badOrder = 0, badFreeLive = 0, badLiveLive = 0;
    ULONGLONG prevEnd = 0, live = 0;

    for (VA_NODE *c = g_VaFreeHead; c; c = c->Next) {
        badNode += !(c->Start < c->End) || (c->Start & (PAGE_SIZE - 1)) || (c->End & (PAGE_SIZE - 1)) ||
                   c->Start < S23GPU_VA_USER_START || c->End > S23GPU_VA_USER_END;
        if (c != g_VaFreeHead) badOrder += !(c->Start > prevEnd);   /* sorted, not touching, not overlapping */
        prevEnd = c->End;
        for (int i = 0; i < g_nlive; i++) badFreeLive += overlaps(c->Start, c->End - c->Start, g_live[i].va, g_live[i].size);
    }
    for (int i = 0; i < g_nlive; i++) {
        live += g_live[i].size;
        for (int j = i + 1; j < g_nlive; j++) badLiveLive += overlaps(g_live[i].va, g_live[i].size, g_live[j].va, g_live[j].size);
    }
    CHECK(badNode == 0);
    CHECK(badOrder == 0);
    CHECK(badFreeLive == 0);
    CHECK(badLiveLive == 0);
    CHECK_EQ_U64(free_bytes(&nodes) + live + g_lost, TOTAL);
    CHECK_EQ_U64(g_pool_live, nodes);
    if (g_failures != before) fprintf(stderr, "  (after %s)\n", ctx);
    return g_failures == before;
}

static void track(ULONGLONG va, ULONGLONG size) { if (g_nlive < MAX_LIVE) { g_live[g_nlive].va = va; g_live[g_nlive].size = size; g_nlive++; } }

static void untrack(int i) { g_live[i] = g_live[--g_nlive]; }

static void free_range(int i)
{
    ULONGLONG before = free_bytes(NULL), va = g_live[i].va, size = g_live[i].size;
    VaFreeLocked(va, size);
    untrack(i);
    ULONGLONG gained = free_bytes(NULL) - before;
    CHECK(gained == size || gained == 0);        /* either fully returned or dropped on OOM */
    if (gained == 0) g_lost += size;
}

static void reset(void)
{
    VaDestroyLocked();
    CHECK(g_VaFreeHead == NULL && g_pool_live == 0);
    g_nlive = 0;
    g_lost = 0;
    g_fail_next = -1;
    VaInitLocked();
}

static void test_edges(void)
{
    reset();
    CHECK(g_VaFreeHead && g_VaFreeHead->Start == S23GPU_VA_USER_START && g_VaFreeHead->End == S23GPU_VA_USER_END);

    CHECK(VaAllocLocked(0) == 0);
    CHECK(VaAllocLocked(PAGE_SIZE - 1) == 0);
    CHECK(VaAllocLocked(PAGE_SIZE + 1) == 0);
    CHECK(VaAllocLocked(TOTAL + PAGE_SIZE) == 0);
    CHECK(!VaReserveLocked(S23GPU_VA_USER_START + 1, PAGE_SIZE));
    CHECK(!VaReserveLocked(S23GPU_VA_USER_START, 0));
    CHECK(!VaReserveLocked(S23GPU_VA_USER_START - PAGE_SIZE, PAGE_SIZE));
    CHECK(!VaReserveLocked(S23GPU_VA_USER_END - PAGE_SIZE, 2 * PAGE_SIZE));
    VaFreeLocked(0, PAGE_SIZE);                   /* ignored */
    VaFreeLocked(S23GPU_VA_USER_START, 0);        /* ignored */
    check_invariants("edge no-ops");

    /* whole range in one allocation, then nothing left */
    ULONGLONG all = VaAllocLocked(TOTAL);
    CHECK_EQ_U64(all, S23GPU_VA_USER_START);
    CHECK(g_VaFreeHead == NULL);
    CHECK(VaAllocLocked(PAGE_SIZE) == 0);
    track(all, TOTAL);
    check_invariants("alloc all");
    free_range(0);
    CHECK(g_VaFreeHead && g_VaFreeHead->Next == NULL && g_VaFreeHead->End - g_VaFreeHead->Start == TOTAL);

    /* first allocation from a large node keeps a one-page guard below it */
    ULONGLONG a = VaAllocLocked(4 * PAGE_SIZE);
    CHECK_EQ_U64(a, S23GPU_VA_USER_START + PAGE_SIZE);
    track(a, 4 * PAGE_SIZE);
    check_invariants("guard alloc");

    /* reserve: exact middle split, then neighbours, then coalesce everything back */
    ULONGLONG r = S23GPU_VA_USER_START + 0x100000ULL;
    CHECK(VaReserveLocked(r, 2 * PAGE_SIZE));   track(r, 2 * PAGE_SIZE);
    CHECK(!VaReserveLocked(r, PAGE_SIZE));       /* already taken */
    CHECK(!VaReserveLocked(r - PAGE_SIZE, 3 * PAGE_SIZE)); /* spans free + taken */
    CHECK(VaReserveLocked(r - PAGE_SIZE, PAGE_SIZE));      track(r - PAGE_SIZE, PAGE_SIZE);   /* node end */
    CHECK(VaReserveLocked(r + 2 * PAGE_SIZE, PAGE_SIZE));  track(r + 2 * PAGE_SIZE, PAGE_SIZE); /* node start */
    check_invariants("reserves");
    while (g_nlive) free_range(g_nlive - 1);
    check_invariants("free all after reserves");
    CHECK(g_VaFreeHead && g_VaFreeHead->Next == NULL);

    /* reserve the exact remaining node */
    CHECK(VaReserveLocked(S23GPU_VA_USER_START, TOTAL));
    CHECK(g_VaFreeHead == NULL);
    track(S23GPU_VA_USER_START, TOTAL);
    free_range(0);
    check_invariants("reserve whole range");
}

static void test_coalescing_orders(void)
{
    /* allocate N blocks, free them in forward, backward and interleaved order: always one node at the end */
    for (int order = 0; order < 3; order++) {
        reset();
        ULONGLONG va[16];
        for (int i = 0; i < 16; i++) { va[i] = VaAllocLocked((ULONGLONG)(i + 1) * PAGE_SIZE); CHECK(va[i] != 0); track(va[i], (ULONGLONG)(i + 1) * PAGE_SIZE); }
        check_invariants("16 allocs");
        int seq[16];
        for (int i = 0; i < 16; i++) seq[i] = order == 0 ? i : order == 1 ? 15 - i : (i % 2 ? 15 - i / 2 : i / 2);
        for (int k = 0; k < 16; k++) {
            for (int i = 0; i < g_nlive; i++) if (g_live[i].va == va[seq[k]]) { free_range(i); break; }
            check_invariants("ordered free");
        }
        CHECK(g_VaFreeHead && g_VaFreeHead->Next == NULL && g_VaFreeHead->Start == S23GPU_VA_USER_START &&
              g_VaFreeHead->End == S23GPU_VA_USER_END);
    }
}

static void test_oom_paths(void)
{
    reset();
    /* VaAllocLocked needs a node for the tail split: on OOM it must fail without changing the list */
    ULONGLONG fb = free_bytes(NULL);
    g_fail_next = 0;
    CHECK(VaAllocLocked(4 * PAGE_SIZE) == 0);
    CHECK_EQ_U64(free_bytes(NULL), fb);
    check_invariants("alloc OOM");

    /* VaReserveLocked middle split needs a node: on OOM it must fail without changing the list */
    g_fail_next = 0;
    CHECK(!VaReserveLocked(S23GPU_VA_USER_START + 0x200000ULL, PAGE_SIZE));
    CHECK_EQ_U64(free_bytes(NULL), fb);
    check_invariants("reserve OOM");

    /* VaFreeLocked of an isolated range needs a node: on OOM the range is dropped (VA leak), list stays valid */
    ULONGLONG x = S23GPU_VA_USER_START + 0x400000ULL;
    CHECK(VaReserveLocked(x, PAGE_SIZE));  track(x, PAGE_SIZE);
    check_invariants("isolated reserve");
    g_fail_next = 0;
    free_range(g_nlive - 1);
    check_invariants("free OOM");
}

static void test_random(int iterations)
{
    reset();
    for (int it = 0; it < iterations; it++) {
        unsigned op = (unsigned)(rnd() % 100);
        if (op < 2) g_fail_next = (long)(rnd() % 2);       /* sporadic pool failures */
        if (op < 45 && g_nlive < MAX_LIVE) {
            ULONGLONG pages = (rnd() % 8 == 0) ? 1 + rnd() % 4096 : 1 + rnd() % 64;
            ULONGLONG va = VaAllocLocked(pages * PAGE_SIZE);
            if (va) {
                CHECK((va & (PAGE_SIZE - 1)) == 0);
                track(va, pages * PAGE_SIZE);
            }
        } else if (op < 60 && g_nlive < MAX_LIVE) {
            ULONGLONG pages = 1 + rnd() % 32;
            ULONGLONG va = S23GPU_VA_USER_START + (rnd() % (TOTAL / PAGE_SIZE - pages)) * PAGE_SIZE;
            int inside = 0;   /* reservable iff the range lies inside a single free node */
            for (VA_NODE *c = g_VaFreeHead; c; c = c->Next)
                if (va >= c->Start && va + pages * PAGE_SIZE <= c->End) inside = 1;
            int oomArmed = g_fail_next >= 0;
            BOOLEAN ok = VaReserveLocked(va, pages * PAGE_SIZE);
            if (ok) track(va, pages * PAGE_SIZE);
            CHECK(!ok || inside);
            if (!oomArmed) CHECK(ok == inside);
        } else if (g_nlive) {
            free_range((int)(rnd() % (unsigned long long)g_nlive));
        }
        g_fail_next = -1;
        if (!check_invariants("random op")) { fprintf(stderr, "  iteration %d op %u\n", it, op); break; }
    }
    while (g_nlive) free_range(g_nlive - 1);
    check_invariants("random teardown");
    if (g_lost == 0) CHECK(g_VaFreeHead && g_VaFreeHead->Next == NULL);
}

int main(void)
{
    test_edges();
    test_coalescing_orders();
    test_oom_paths();
    test_random(20000);
    VaDestroyLocked();
    CHECK(g_pool_live == 0);
    return test_report("test_va_alloc");
}
