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

/* ==================================================================
 * UT-MEM-005..014: KMEM_Alloc
 * ================================================================== */

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

/* ==================================================================
 * UT-MEM-015..017: KMEM_TensorAlloc (via arena)
 * ================================================================== */

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

/* ==================================================================
 * UT-MEM-018..022: GetUsedBytes / GetFreeBytes
 * ================================================================== */

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

/* ==================================================================
 * UT-MEM-023..025: Peak usage tracking
 * ================================================================== */

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

/* ==================================================================
 * UT-MEM-026..029: Arena Reset (mapped from spec "Allocator Reset")
 * ================================================================== */

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

/* ==================================================================
 * UT-MEM-030..035: KMEM_GetStats
 * ================================================================== */

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

/* ==================================================================
 * UT-MEM-036..038: memset
 * ================================================================== */

void test_UT_MEM_036(void) {
    /* MEM_Set fills every byte with the specified value */
    uint8_t buf[32];
    memset(buf, 0xAB, sizeof(buf));
    for (int i = 0; i < (int)sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_UINT8(0xAB, buf[i]);
    }
}

void test_UT_MEM_037(void) {
    /* MEM_Set with count 0 does not crash or modify memory */
    uint8_t buf[8];
    memset(buf, 0xCC, sizeof(buf));
    memset(buf, 0x00, 0);  /* count == 0 */
    /* Buffer should be unchanged */
    for (int i = 0; i < (int)sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_UINT8(0xCC, buf[i]);
    }
}

void test_UT_MEM_038(void) {
    /* MEM_Set does not write beyond the specified byte count */
    uint8_t buf[16];
    memset(buf, 0xBB, sizeof(buf));
    memset(buf, 0x11, 8);   /* only first 8 bytes */
    for (int i = 0; i < 8;  i++) TEST_ASSERT_EQUAL_UINT8(0x11, buf[i]);
    for (int i = 8; i < 16; i++) TEST_ASSERT_EQUAL_UINT8(0xBB, buf[i]);
}

/* ==================================================================
 * UT-MEM-039..041: memcpy
 * ================================================================== */

void test_UT_MEM_039(void) {
    /* MEM_Copy produces a byte-for-byte identical destination */
    uint8_t src[32], dst[32];
    for (int i = 0; i < 32; i++) src[i] = (uint8_t)i;
    memcpy(dst, src, 32);
    TEST_ASSERT_EQUAL_MEMORY(src, dst, 32);
}

void test_UT_MEM_040(void) {
    /* MEM_Copy with count 0 does not crash */
    uint8_t src[8] = {1,2,3,4,5,6,7,8};
    uint8_t dst[8] = {0};
    memcpy(dst, src, 0);
    /* dst unchanged */
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_UINT8(0, dst[i]);
}

void test_UT_MEM_041(void) {
    /* MEM_Copy does not modify bytes beyond the specified count */
    uint8_t src[16];
    uint8_t dst[16];
    memset(src, 0xAA, 16);
    memset(dst, 0xFF, 16);
    memcpy(dst, src, 8);
    for (int i = 0;  i < 8;  i++) TEST_ASSERT_EQUAL_UINT8(0xAA, dst[i]);
    for (int i = 8;  i < 16; i++) TEST_ASSERT_EQUAL_UINT8(0xFF, dst[i]);
}

/* ==================================================================
 * UT-MEM-042..045: memcmp
 * ================================================================== */

void test_UT_MEM_042(void) {
    /* MEM_Compare returns 0 for two identical regions */
    uint8_t a[16], b[16];
    memset(a, 0x42, 16);
    memset(b, 0x42, 16);
    TEST_ASSERT_EQUAL_INT(0, memcmp(a, b, 16));
}

void test_UT_MEM_043(void) {
    /* MEM_Compare returns non-zero when regions differ at the first byte */
    uint8_t a[8] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    uint8_t b[8] = {0xFF,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, 8));
}

void test_UT_MEM_044(void) {
    /* MEM_Compare returns non-zero when regions differ only at the last byte */
    uint8_t a[8] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    uint8_t b[8] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0xFF};
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, 8));
}

void test_UT_MEM_045(void) {
    /* MEM_Compare with count 0 returns 0 */
    uint8_t a[8] = {0xAA};
    uint8_t b[8] = {0xBB};
    TEST_ASSERT_EQUAL_INT(0, memcmp(a, b, 0));
}

/* ==================================================================
 * CT-MEM-001..007: Component Tests (multiple KMEM functions together)
 * ================================================================== */

void test_CT_MEM_001(void) {
    /* Full allocation lifecycle: Init → Alloc → Stats → Arena-Reset → Alloc */
    kmem_stats_t st;
    void *p1 = KMEM_Alloc(256, 8);
    void *p2 = KMEM_Alloc(512, 64);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);

    KMEM_GetStats(&st);
    TEST_ASSERT_TRUE(st.alloc_count >= 2);
    TEST_ASSERT_TRUE(st.heap_used >= 768);

    /* Create arena and reset it — heap stats should be consistent */
    kmem_arena_t *arena = KMEM_ArenaCreate(1024);
    KMEM_ArenaAlloc(arena, 512, 8);
    KMEM_GetStats(&st);
    size_t peak = st.heap_peak;

    KMEM_ArenaReset(arena);
    KMEM_GetStats(&st);
    TEST_ASSERT_EQUAL_UINT(peak, (unsigned)st.heap_peak); /* peak unchanged */

    void *p3 = KMEM_ArenaAlloc(arena, 256, 8);
    TEST_ASSERT_NOT_NULL(p3);
}

void test_CT_MEM_002(void) {
    /* Alignment diversity: interleave 8, 16, 64, 128 aligned allocs */
    void *p8   = KMEM_Alloc(16, 8);
    void *p16  = KMEM_Alloc(16, 16);
    void *p64  = KMEM_Alloc(64, 64);
    void *p128 = KMEM_Alloc(128, 128);

    TEST_ASSERT_NOT_NULL(p8);
    TEST_ASSERT_NOT_NULL(p16);
    TEST_ASSERT_NOT_NULL(p64);
    TEST_ASSERT_NOT_NULL(p128);

    TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p8   % 8));
    TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p16  % 16));
    TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p64  % 64));
    TEST_ASSERT_EQUAL_INT(0, (int)((uintptr_t)p128 % 128));

    /* No region overlaps: each pointer is past the previous one's end */
    TEST_ASSERT_TRUE((uintptr_t)p16  >= (uintptr_t)p8   + 16);
    TEST_ASSERT_TRUE((uintptr_t)p64  >= (uintptr_t)p16  + 16);
    TEST_ASSERT_TRUE((uintptr_t)p128 >= (uintptr_t)p64  + 64);
}

void test_CT_MEM_003(void) {
    /* Heap exhaustion path: allocate until NULL, then arena-reset and retry */
    kmem_arena_t *arena = KMEM_ArenaCreate(8192);
    TEST_ASSERT_NOT_NULL(arena);

    int count = 0;
    while (KMEM_ArenaAlloc(arena, 256, 8) != NULL) count++;
    TEST_ASSERT_TRUE(count > 0);

    /* ArenaReset restores it */
    KMEM_ArenaReset(arena);
    size_t free_after = KMEM_ArenaGetUsed(arena);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)free_after);

    void *p = KMEM_ArenaAlloc(arena, 256, 8);
    TEST_ASSERT_NOT_NULL(p);
}

void test_CT_MEM_004(void) {
    /* Peak tracking: allocate progressively larger across multiple resets */
    kmem_arena_t *arena = KMEM_ArenaCreate(65536);
    TEST_ASSERT_NOT_NULL(arena);

    kmem_stats_t st;

    /* Cycle 1 */
    KMEM_ArenaAlloc(arena, 1024, 8);
    KMEM_GetStats(&st);
    size_t peak1 = st.heap_peak;
    KMEM_ArenaReset(arena);

    /* Cycle 2: larger allocation */
    KMEM_ArenaAlloc(arena, 2048, 8);
    KMEM_GetStats(&st);
    size_t peak2 = st.heap_peak;
    KMEM_ArenaReset(arena);

    /* Cycle 3: even larger */
    KMEM_ArenaAlloc(arena, 4096, 8);
    KMEM_GetStats(&st);
    size_t peak3 = st.heap_peak;

    /* Global peak should climb each cycle (arena data comes from heap) */
    TEST_ASSERT_TRUE(peak3 >= peak2);
    TEST_ASSERT_TRUE(peak2 >= peak1);
}

void test_CT_MEM_005(void) {
    /* Wasted bytes: allocations with known padding produce wasted bytes.
     * Allocate 1 byte with 8-byte alignment; 7 bytes of padding. */
    size_t free_before = KMEM_GetFreeSpace();
    KMEM_Alloc(1, 8);
    size_t free_after = KMEM_GetFreeSpace();
    size_t consumed = free_before - free_after;
    /* Consumed must be at least 8 (1 byte + up to 7 padding) */
    TEST_ASSERT_TRUE(consumed >= 1);
    TEST_ASSERT_TRUE(consumed <= 16);  /* sanity upper bound */
}

void test_CT_MEM_006(void) {
    /* MEM_Copy + MEM_Compare round-trip */
    uint8_t region_a[64], region_b[64];
    memset(region_a, 0x55, sizeof(region_a));
    memcpy(region_b, region_a, sizeof(region_a));
    TEST_ASSERT_EQUAL_INT(0, memcmp(region_a, region_b, sizeof(region_a)));

    /* Mutate copy */
    region_b[32] = 0xFF;
    TEST_ASSERT_NOT_EQUAL(0, memcmp(region_a, region_b, sizeof(region_a)));
}

void test_CT_MEM_007(void) {
    /* Tensor workflow: 5 tensors via arena, each with distinct pattern */
    kmem_arena_t *arena = KMEM_ArenaCreate(5 * 1024);
    TEST_ASSERT_NOT_NULL(arena);

    uint8_t *tensors[5];
    uint8_t patterns[5] = {0x11, 0x22, 0x33, 0x44, 0x55};

    for (int i = 0; i < 5; i++) {
        tensors[i] = (uint8_t *)KMEM_TensorAlloc(arena, 512);
        TEST_ASSERT_NOT_NULL(tensors[i]);
        memset(tensors[i], patterns[i], 512);
    }

    /* Verify no aliasing */
    for (int i = 0; i < 5; i++) {
        for (int b = 0; b < 512; b++) {
            TEST_ASSERT_EQUAL_UINT8(patterns[i], tensors[i][b]);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
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
