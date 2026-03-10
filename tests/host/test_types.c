/**
 * @file test_types.c
 * @brief Unity tests for UT-TYPES-001 through UT-TYPES-017
 *
 * Tests MiniOS fixed-width type definitions (types.h).
 * Compiled with gcc for x86_64 — no QEMU required.
 */

#include "unity.h"

#define MINIOS_HAL_ARCH_H  /* prevent arch.h arm asm from being included */
#include "types.h"


/* ------------------------------------------------------------------ */
/*  Unity setUp / tearDown                                             */
/* ------------------------------------------------------------------ */
void setUp(void)    {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/*  UT-TYPES-001..008: Integer type sizes                             */
/* ------------------------------------------------------------------ */

void test_UT_TYPES_001(void) {
    TEST_ASSERT_EQUAL_INT(1, (int)sizeof(uint8_t));
}
void test_UT_TYPES_002(void) {
    TEST_ASSERT_EQUAL_INT(2, (int)sizeof(uint16_t));
}
void test_UT_TYPES_003(void) {
    TEST_ASSERT_EQUAL_INT(4, (int)sizeof(uint32_t));
}
void test_UT_TYPES_004(void) {
    TEST_ASSERT_EQUAL_INT(8, (int)sizeof(uint64_t));
}
void test_UT_TYPES_005(void) {
    TEST_ASSERT_EQUAL_INT(1, (int)sizeof(int8_t));
}
void test_UT_TYPES_006(void) {
    TEST_ASSERT_EQUAL_INT(2, (int)sizeof(int16_t));
}
void test_UT_TYPES_007(void) {
    TEST_ASSERT_EQUAL_INT(4, (int)sizeof(int32_t));
}
void test_UT_TYPES_008(void) {
    TEST_ASSERT_EQUAL_INT(8, (int)sizeof(int64_t));
}

/* ------------------------------------------------------------------ */
/*  UT-TYPES-009..010: Pointer-sized types                            */
/* ------------------------------------------------------------------ */

void test_UT_TYPES_009(void) {
    /* size_t must equal sizeof(void*) */
    TEST_ASSERT_EQUAL_INT((int)sizeof(void *), (int)sizeof(size_t));
}
void test_UT_TYPES_010(void) {
    TEST_ASSERT_EQUAL_INT((int)sizeof(void *), (int)sizeof(uintptr_t));
}

/* ------------------------------------------------------------------ */
/*  UT-TYPES-011: Boolean values                                      */
/* ------------------------------------------------------------------ */

void test_UT_TYPES_011(void) {
    TEST_ASSERT_EQUAL_INT(1, (int)true);
    TEST_ASSERT_EQUAL_INT(0, (int)false);
}

/* ------------------------------------------------------------------ */
/*  UT-TYPES-012: NULL definition                                     */
/* ------------------------------------------------------------------ */

void test_UT_TYPES_012(void) {
    TEST_ASSERT_EQUAL_PTR((void *)0, NULL);
}

/* ------------------------------------------------------------------ */
/*  UT-TYPES-013..014: REG32 / REG64 macro compile + access           */
/*                                                                    */
/*  We can't read arbitrary MMIO addresses on the host, so we verify  */
/*  the macros produce a volatile read from a known local variable.   */
/* ------------------------------------------------------------------ */

void test_UT_TYPES_013(void) {
    /* REG32 should produce a volatile uint32_t dereference */
    volatile uint32_t scratch = 0xDEADBEEFUL;
    uint32_t val = REG32((uintptr_t)&scratch);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFUL, val);
}

void test_UT_TYPES_014(void) {
    volatile uint64_t scratch = 0x0123456789ABCDEFULL;
    uint64_t val = REG64((uintptr_t)&scratch);
    TEST_ASSERT_EQUAL_UINT64(0x0123456789ABCDEFULL, val);
}

/* ------------------------------------------------------------------ */
/*  UT-TYPES-015: uint8_t range                                       */
/* ------------------------------------------------------------------ */

void test_UT_TYPES_015(void) {
    uint8_t max_val = 255;
    TEST_ASSERT_EQUAL_UINT8(255, max_val);

    /* Wrap at 0 */
    uint8_t wrap = max_val;
    wrap++;   /* wraps to 0 */
    TEST_ASSERT_EQUAL_UINT8(0, wrap);
}

/* ------------------------------------------------------------------ */
/*  UT-TYPES-016: int8_t range                                        */
/* ------------------------------------------------------------------ */

void test_UT_TYPES_016(void) {
    int8_t min_val = -128;
    int8_t max_val =  127;
    TEST_ASSERT_EQUAL_INT8(-128, min_val);
    TEST_ASSERT_EQUAL_INT8( 127, max_val);
}

/* ------------------------------------------------------------------ */
/*  UT-TYPES-017: uint64_t can hold 0xFFFFFFFFFFFFFFFF                */
/* ------------------------------------------------------------------ */

void test_UT_TYPES_017(void) {
    uint64_t all_ones = 0xFFFFFFFFFFFFFFFFULL;
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFFFFFFFFFULL, all_ones);
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_UT_TYPES_001);
    RUN_TEST(test_UT_TYPES_002);
    RUN_TEST(test_UT_TYPES_003);
    RUN_TEST(test_UT_TYPES_004);
    RUN_TEST(test_UT_TYPES_005);
    RUN_TEST(test_UT_TYPES_006);
    RUN_TEST(test_UT_TYPES_007);
    RUN_TEST(test_UT_TYPES_008);
    RUN_TEST(test_UT_TYPES_009);
    RUN_TEST(test_UT_TYPES_010);
    RUN_TEST(test_UT_TYPES_011);
    RUN_TEST(test_UT_TYPES_012);
    RUN_TEST(test_UT_TYPES_013);
    RUN_TEST(test_UT_TYPES_014);
    RUN_TEST(test_UT_TYPES_015);
    RUN_TEST(test_UT_TYPES_016);
    RUN_TEST(test_UT_TYPES_017);

    return UNITY_END();
}
