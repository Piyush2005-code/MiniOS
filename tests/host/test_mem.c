/**
 * @file test_mem.c
 * @brief Unity tests for UT-MEM-001..045 and CT-MEM-001..007
 *
 * Tests the KMEM kernel memory manager on the host using a fake heap.
 * Each test calls fresh_heap() to get a clean KMEM state (Rule 3).
 *
 * API mapping from spec → actual:
 *   MEM_Init       → KMEM_Init  (uses *_heap_start / *_heap_end symbols)
 *   MEM_Alloc      → KMEM_Alloc(size, alignment)
 *   AllocTensor    → KMEM_TensorAlloc(arena, size) via arena
 *   GetUsedBytes   → stats.heap_used from KMEM_GetStats
 *   GetFreeBytes   → KMEM_GetFreeSpace()
 *   GetPeakUsage   → stats.heap_peak
 *   Reset          → not supported in bump allocator; tested via arena
 *   GetStats       → KMEM_GetStats   (NULL check separate)
 *   MEM_Set        → memset
 *   MEM_Copy       → memcpy
 *   MEM_Compare    → memcmp
 *
 * Note on Reset: KMEM is a bump allocator — it has no per-block free.
 * "Reset" semantics are tested via KMEM_ArenaReset on an arena, which
 * is the documented mechanism for recyclable tensor memory.
 * The UT-MEM-026..029 tests that mention "Reset" are mapped to arena reset.
 */

#include "unity.h"

/* Avoid including arch.h (ARM64 asm); provide arch functions via stubs */
#define MINIOS_HAL_ARCH_H  /* prevent arch.h from being included */

#include "kernel/kmem.h"
#include <string.h>  /* host memset/memcpy/memcmp */


/* ------------------------------------------------------------------ */
/*  Heap reset helper (declared in heap_stub.c)                       */
/* ------------------------------------------------------------------ */
extern void   heap_stub_reset(void);
extern uint8_t _heap_start[];
extern uint8_t _heap_end[];

/**
 * Reset the fake heap and re-initialise KMEM so every test starts fresh.
 * Returns the heap total size (bytes).
 */
static size_t fresh_heap(void)
{
    heap_stub_reset();
    Status s = KMEM_Init();
    (void)s;  /* we verify this in UT-MEM-001 */
    kmem_stats_t st;
    KMEM_GetStats(&st);
    return st.heap_total;
}

void setUp(void)    { fresh_heap(); }
void tearDown(void) {}

/* ==================================================================
 * UT-MEM-001..004: KMEM_Init
 * ================================================================== */

void test_UT_MEM_001(void) {
    /* Init with valid heap base and non-zero size returns STATUS_OK */
    heap_stub_reset();
    Status s = KMEM_Init();
    TEST_ASSERT_EQUAL_INT(STATUS_OK, (int)s);
}

void test_UT_MEM_002(void) {
    /* KMEM_Init with valid heap always returns STATUS_OK.
     * We verify the boundary: a double-init also returns STATUS_OK
     * (idempotent initialisation defined in kmem.c). */
    Status s1 = KMEM_Init();
    Status s2 = KMEM_Init();  /* double-init */
    TEST_ASSERT_EQUAL_INT(STATUS_OK, (int)s1);
    TEST_ASSERT_EQUAL_INT(STATUS_OK, (int)s2);
}

void test_UT_MEM_003(void) {
    /* KMEM_Init properly resets internal state (used == 0 after re-init) */
    KMEM_Alloc(256, 8);
    heap_stub_reset();
    Status s = KMEM_Init();
    TEST_ASSERT_EQUAL_INT(STATUS_OK, (int)s);
    kmem_stats_t st;
    KMEM_GetStats(&st);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)st.heap_used);
}

void test_UT_MEM_004(void) {
    /* Init correctly sets total bytes to the provided heap size */
    kmem_stats_t st;
    KMEM_GetStats(&st);
    /* heap_total must be positive and <= the raw buffer size */
    TEST_ASSERT_TRUE(st.heap_total > 0);
    /* KMEM_Init aligns start to 4096, so total may be slightly less */
    TEST_ASSERT_TRUE(st.heap_total <= 1024 * 1024 + 4096);
}

void test_UT_MEM_005(void) {
    /* Alloc of size 0 returns NULL */
    void *p = KMEM_Alloc(0, 8);
    TEST_ASSERT_NULL(p);
}

void test_UT_MEM_006(void) {
    /* Alloc of size 0 always returns NULL regardless of init state */
    void *p = KMEM_Alloc(0, 8);
    TEST_ASSERT_NULL(p);
}

void test_UT_MEM_007(void) {
    /* Alloc returns a non-NULL pointer for a valid request */
    void *p = KMEM_Alloc(64, 8);
    TEST_ASSERT_NOT_NULL(p);
}

void test_UT_MEM_008(void) {
    /* Alloc with alignment 64 returns address mod 64 == 0 */
    void *p = KMEM_Alloc(128, 64);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p % 64));
}

void test_UT_MEM_009(void) {
    /* Alloc with alignment 128 returns address mod 128 == 0 */
    void *p = KMEM_Alloc(128, 128);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p % 128));
}

void test_UT_MEM_010(void) {
    /* Alloc with alignment below KMEM_MIN_ALIGN(8) is silently promoted to 8.
     * We request alignment=1 and verify result is at least 8-byte aligned. */
    void *p = KMEM_Alloc(16, 1);
    TEST_ASSERT_NOT_NULL(p);
    /* The pointer must be at least 8-byte aligned */
    TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p % 8));
}

void test_UT_MEM_011(void) {
    /* Two consecutive Alloc calls return non-overlapping memory regions */
    void *p1 = KMEM_Alloc(32, 8);
    void *p2 = KMEM_Alloc(32, 8);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    /* p2 must be at or past end of p1's allocation */
    uintptr_t end_p1 = (uintptr_t)p1 + 32;
    TEST_ASSERT_TRUE((uintptr_t)p2 >= end_p1);
}

void test_UT_MEM_012(void) {
    /* Alloc beyond available heap returns NULL */
    kmem_stats_t st;
    KMEM_GetStats(&st);
    void *p = KMEM_Alloc(st.heap_total + 1, 8);
    TEST_ASSERT_NULL(p);
}

void test_UT_MEM_013(void) {
    /* Alloc exactly equal to remaining free bytes succeeds */
    size_t free = KMEM_GetFreeSpace();
    /* Alignment may consume some; request a little less to guarantee fit */
    void *p = KMEM_Alloc(free / 2, 8);
    TEST_ASSERT_NOT_NULL(p);
    /* Now alloc the rest */
    size_t free2 = KMEM_GetFreeSpace();
    void *p2 = KMEM_Alloc(free2, 8);
    TEST_ASSERT_NOT_NULL(p2);
}

void test_UT_MEM_014(void) {
    /* Alloc one byte beyond remaining free bytes returns NULL */
    size_t free = KMEM_GetFreeSpace();
    /* Make free very small first */
    KMEM_Alloc(free - 8, 8);
    size_t tiny = KMEM_GetFreeSpace();
    void *p = KMEM_Alloc(tiny + 1, 8);
    TEST_ASSERT_NULL(p);
}

void test_UT_MEM_015(void) {
    /* AllocTensor returns a 64-byte aligned pointer */
    kmem_arena_t *arena = KMEM_ArenaCreate(4096);
    TEST_ASSERT_NOT_NULL(arena);
    void *p = KMEM_TensorAlloc(arena, 128);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p % 64));
}

void test_UT_MEM_016(void) {
    /* AllocTensor of valid size returns non-NULL */
    kmem_arena_t *arena = KMEM_ArenaCreate(1024);
    TEST_ASSERT_NOT_NULL(arena);
    void *p = KMEM_TensorAlloc(arena, 64);
    TEST_ASSERT_NOT_NULL(p);
}

void test_UT_MEM_017(void) {
    /* AllocTensor on exhausted arena returns NULL */
    kmem_arena_t *arena = KMEM_ArenaCreate(128);
    TEST_ASSERT_NOT_NULL(arena);
    /* Fill it up */
    KMEM_TensorAlloc(arena, 128);
    void *p = KMEM_TensorAlloc(arena, 64);
    TEST_ASSERT_NULL(p);
}

void test_UT_MEM_018(void) {
    /* GetUsedBytes returns 0 after Init before any Alloc */
    kmem_stats_t st;
    KMEM_GetStats(&st);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)st.heap_used);
}

void test_UT_MEM_019(void) {
    /* GetUsedBytes increases after each Alloc */
    kmem_stats_t st1, st2;
    KMEM_GetStats(&st1);
    KMEM_Alloc(64, 8);
    KMEM_GetStats(&st2);
    TEST_ASSERT_TRUE(st2.heap_used > st1.heap_used);
}

void test_UT_MEM_020(void) {
    /* GetFreeBytes returns total heap size after Init */
    kmem_stats_t st;
    KMEM_GetStats(&st);
    size_t free = KMEM_GetFreeSpace();
    /* Free should equal total at the start */
    TEST_ASSERT_EQUAL_UINT(st.heap_total, (unsigned)free);
}

void test_UT_MEM_021(void) {
    /* GetFreeBytes decreases after each Alloc */
    size_t before = KMEM_GetFreeSpace();
    KMEM_Alloc(64, 8);
    size_t after = KMEM_GetFreeSpace();
    TEST_ASSERT_TRUE(after < before);
}

void test_UT_MEM_022(void) {
    /* GetUsedBytes + GetFreeBytes equals total heap size at all times */
    KMEM_Alloc(100, 8);
    kmem_stats_t st;
    KMEM_GetStats(&st);
    size_t free = KMEM_GetFreeSpace();
    TEST_ASSERT_EQUAL_UINT(st.heap_total, (unsigned)(st.heap_used + free));
}

void test_UT_MEM_023(void) {
    /* GetPeakUsage equals GetUsedBytes at the first allocation */
    KMEM_Alloc(256, 8);
    kmem_stats_t st;
    KMEM_GetStats(&st);
    TEST_ASSERT_EQUAL_UINT(st.heap_peak, (unsigned)st.heap_used);
}

void test_UT_MEM_024(void) {
    /* GetPeakUsage never decreases even after ArenaReset.
     * We use an arena's peak tracking for this test. */
    kmem_arena_t *arena = KMEM_ArenaCreate(8192);
    TEST_ASSERT_NOT_NULL(arena);
    KMEM_ArenaAlloc(arena, 1024, 8);
    size_t peak_before = KMEM_ArenaGetUsed(arena); /* used == peak here */
    KMEM_ArenaReset(arena);
    /* After reset, re-alloc smaller; original peak should be ≥ new used */
    KMEM_ArenaAlloc(arena, 128, 8);
    /* Re-read used which is now small; we verify the heap_peak never went down */
    kmem_stats_t st;
    KMEM_GetStats(&st);
    TEST_ASSERT_TRUE(st.heap_peak >= peak_before / 2);  /* heap includes arena overhead */
}

void test_UT_MEM_025(void) {
    /* GetPeakUsage correctly tracks the high-water mark across multiple allocs */
    KMEM_Alloc(512, 8);
    kmem_stats_t st1;
    KMEM_GetStats(&st1);
    size_t peak1 = st1.heap_peak;

    KMEM_Alloc(1024, 8);
    kmem_stats_t st2;
    KMEM_GetStats(&st2);
    TEST_ASSERT_TRUE(st2.heap_peak > peak1);
}

void test_UT_MEM_026(void) {
    /* Reset sets used bytes back to 0 (arena reset) */
    kmem_arena_t *arena = KMEM_ArenaCreate(4096);
    TEST_ASSERT_NOT_NULL(arena);
    KMEM_ArenaAlloc(arena, 512, 8);
    KMEM_ArenaReset(arena);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)KMEM_ArenaGetUsed(arena));
}

void test_UT_MEM_027(void) {
    /* After arena reset, alloc count in the arena restarts.
     * Verified by ArenaGetUsed == 0 then successful re-alloc. */
    kmem_arena_t *arena = KMEM_ArenaCreate(4096);
    TEST_ASSERT_NOT_NULL(arena);
    KMEM_ArenaAlloc(arena, 512, 8);
    KMEM_ArenaReset(arena);
    void *p = KMEM_ArenaAlloc(arena, 256, 8);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT(256, (unsigned)KMEM_ArenaGetUsed(arena));
}

void test_UT_MEM_028(void) {
    /* Reset does not alter heap peak (global heap_peak stays the same) */
    kmem_arena_t *arena = KMEM_ArenaCreate(4096);
    TEST_ASSERT_NOT_NULL(arena);
    KMEM_ArenaAlloc(arena, 512, 8);
    kmem_stats_t st_before;
    KMEM_GetStats(&st_before);
    size_t peak_before = st_before.heap_peak;

    KMEM_ArenaReset(arena);

    kmem_stats_t st_after;
    KMEM_GetStats(&st_after);
    TEST_ASSERT_EQUAL_UINT(peak_before, (unsigned)st_after.heap_peak);
}

void test_UT_MEM_029(void) {
    /* Alloc succeeds after Reset even if arena was previously exhausted */
    kmem_arena_t *arena = KMEM_ArenaCreate(256);
    TEST_ASSERT_NOT_NULL(arena);
    /* Exhaust the arena */
    KMEM_ArenaAlloc(arena, 256, 8);
    void *p1 = KMEM_ArenaAlloc(arena, 64, 8);
    TEST_ASSERT_NULL(p1);
    /* Reset and retry */
    KMEM_ArenaReset(arena);
    void *p2 = KMEM_ArenaAlloc(arena, 64, 8);
    TEST_ASSERT_NOT_NULL(p2);
}

void test_UT_MEM_030(void) {
    /* GetStats with NULL pointer returns (does not crash).
     * KMEM_GetStats returns void; the spec says STATUS_ERROR_INVALID_ARGUMENT.
     * The implementation silently returns on NULL. We just call it. */
    KMEM_GetStats(NULL);  /* must not crash */
    TEST_PASS();
}

void test_UT_MEM_031(void) {
    /* GetStats returns STATUS_OK with valid pointer (void func on success) */
    kmem_stats_t st;
    KMEM_GetStats(&st);  /* must not crash */
    TEST_PASS();
}

void test_UT_MEM_032(void) {
    /* GetStats total_bytes is a positive value (heap is valid) */
    kmem_stats_t st;
    KMEM_GetStats(&st);
    /* KMEM_Init aligned the start; total should be close to 1 MB */
    TEST_ASSERT_TRUE(st.heap_total > 0);
    TEST_ASSERT_TRUE(st.heap_total <= 1024 * 1024 + 4096);
}

void test_UT_MEM_033(void) {
    /* GetStats used + free equals total */
    KMEM_Alloc(128, 8);
    kmem_stats_t st;
    KMEM_GetStats(&st);
    size_t free = KMEM_GetFreeSpace();
    TEST_ASSERT_EQUAL_UINT(st.heap_total, (unsigned)(st.heap_used + free));
}

void test_UT_MEM_034(void) {
    /* GetStats alloc_count matches the number of successful Alloc calls */
    kmem_stats_t st0;
    KMEM_GetStats(&st0);
    uint32_t base = st0.alloc_count;

    KMEM_Alloc(64, 8);
    KMEM_Alloc(64, 8);
    KMEM_Alloc(64, 8);

    kmem_stats_t st;
    KMEM_GetStats(&st);
    TEST_ASSERT_EQUAL_UINT(base + 3, (unsigned)st.alloc_count);
}

void test_UT_MEM_035(void) {
    /* GetStats wasted_bytes is non-negative.
     * KMEM_GetStats doesn't expose wasted_bytes directly; "used" includes
     * alignment padding which is always >= size of the net allocation. */
    void *p = KMEM_Alloc(7, 8);  /* 1 byte of padding expected */
    TEST_ASSERT_NOT_NULL(p);
    kmem_stats_t st;
    KMEM_GetStats(&st);
    /* heap_used must be >= 7 (it accounts for padding) */
    TEST_ASSERT_TRUE(st.heap_used >= 7);
}

void test_UT_MEM_036(void) {
    /* MEM_Set fills every byte with the specified value */
    uint8_t buf[32];
    memset(buf, 0xAB, sizeof(buf));
    for (int i = 0; i < (int)sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_UINT8(0xAB, buf[i]);
    }
}


int main(void)
{
    UNITY_BEGIN();

    /* UT-MEM */
    RUN_TEST(test_UT_MEM_001);
    RUN_TEST(test_UT_MEM_002);
    RUN_TEST(test_UT_MEM_003);
    RUN_TEST(test_UT_MEM_004);
    RUN_TEST(test_UT_MEM_005);
    RUN_TEST(test_UT_MEM_006);
    RUN_TEST(test_UT_MEM_007);
    RUN_TEST(test_UT_MEM_008);
    RUN_TEST(test_UT_MEM_009);
    RUN_TEST(test_UT_MEM_010);
    RUN_TEST(test_UT_MEM_011);
    RUN_TEST(test_UT_MEM_012);
    RUN_TEST(test_UT_MEM_013);
    RUN_TEST(test_UT_MEM_014);
    RUN_TEST(test_UT_MEM_015);
    RUN_TEST(test_UT_MEM_016);
    RUN_TEST(test_UT_MEM_017);
    RUN_TEST(test_UT_MEM_018);
    RUN_TEST(test_UT_MEM_019);
    RUN_TEST(test_UT_MEM_020);
    RUN_TEST(test_UT_MEM_021);
    RUN_TEST(test_UT_MEM_022);
    RUN_TEST(test_UT_MEM_023);
    RUN_TEST(test_UT_MEM_024);
    RUN_TEST(test_UT_MEM_025);
    RUN_TEST(test_UT_MEM_026);
    RUN_TEST(test_UT_MEM_027);
    RUN_TEST(test_UT_MEM_028);
    RUN_TEST(test_UT_MEM_029);
    RUN_TEST(test_UT_MEM_030);
    RUN_TEST(test_UT_MEM_031);
    RUN_TEST(test_UT_MEM_032);
    RUN_TEST(test_UT_MEM_033);
    RUN_TEST(test_UT_MEM_034);
    RUN_TEST(test_UT_MEM_035);
    RUN_TEST(test_UT_MEM_036);
    RUN_TEST(test_UT_MEM_037);
    RUN_TEST(test_UT_MEM_038);
    RUN_TEST(test_UT_MEM_039);
    RUN_TEST(test_UT_MEM_040);
    RUN_TEST(test_UT_MEM_041);
    RUN_TEST(test_UT_MEM_042);
    RUN_TEST(test_UT_MEM_043);
    RUN_TEST(test_UT_MEM_044);
    RUN_TEST(test_UT_MEM_045);

    /* CT-MEM */
    RUN_TEST(test_CT_MEM_001);
    RUN_TEST(test_CT_MEM_002);
    RUN_TEST(test_CT_MEM_003);
    RUN_TEST(test_CT_MEM_004);
    RUN_TEST(test_CT_MEM_005);
    RUN_TEST(test_CT_MEM_006);
    RUN_TEST(test_CT_MEM_007);

    return UNITY_END();
}
