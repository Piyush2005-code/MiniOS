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

void test_UT_TYPES_009(void) {
    /* size_t must equal sizeof(void*) */
    TEST_ASSERT_EQUAL_INT((int)sizeof(void *), (int)sizeof(size_t));
}

void test_UT_TYPES_010(void) {
    TEST_ASSERT_EQUAL_INT((int)sizeof(void *), (int)sizeof(uintptr_t));
}

void test_UT_TYPES_011(void) {
    TEST_ASSERT_EQUAL_INT(1, (int)true);
    TEST_ASSERT_EQUAL_INT(0, (int)false);
}

void test_UT_TYPES_012(void) {
    TEST_ASSERT_EQUAL_PTR((void *)0, NULL);
}


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
