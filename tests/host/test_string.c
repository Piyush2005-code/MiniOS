#include <stdint.h>

#include "unity.h"
#include "lib/string.h"

void setUp(void) {}
void tearDown(void) {}

void test_UT_STR_001_memset_fills_nonzero_pattern(void) {
    uint8_t buf[32];
    memset(buf, 0xAB, sizeof(buf));

    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAB, buf[i]);
    }
}

void test_UT_STR_002_memset_zeroes_large_region(void) {
    uint8_t buf[128];
    memset(buf, 0x7E, sizeof(buf));
    memset(buf, 0, sizeof(buf));

    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
    }
}

void test_UT_STR_003_memcpy_copies_aligned_blocks(void) {
    uint64_t src[8];
    uint64_t dst[8];

    for (size_t i = 0; i < 8; i++) {
        src[i] = 0x1111111111111111ULL + (uint64_t)i;
        dst[i] = 0;
    }

    memcpy(dst, src, sizeof(src));
    TEST_ASSERT_EQUAL_MEMORY(src, dst, sizeof(src));
}

void test_UT_STR_004_memcpy_copies_unaligned_regions(void) {
    uint8_t src[33];
    uint8_t dst[33];

    for (size_t i = 0; i < sizeof(src); i++) {
        src[i] = (uint8_t)(i + 3U);
        dst[i] = 0;
    }

    memcpy(&dst[1], &src[1], 31);
    TEST_ASSERT_EQUAL_MEMORY(&src[1], &dst[1], 31);
}

void test_UT_STR_005_strlen_handles_empty_and_non_empty(void) {
    TEST_ASSERT_EQUAL_UINT32(0U, (uint32_t)strlen(""));
    TEST_ASSERT_EQUAL_UINT32(6U, (uint32_t)strlen("MiniOS"));
}

void test_UT_STR_006_memcmp_reports_equality(void) {
    uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_INT(0, memcmp(a, b, sizeof(a)));
}

void test_UT_STR_007_memcmp_reports_ordering(void) {
    uint8_t a[4] = {1, 2, 9, 4};
    uint8_t b[4] = {1, 2, 8, 4};

    TEST_ASSERT_TRUE(memcmp(a, b, sizeof(a)) > 0);
    TEST_ASSERT_TRUE(memcmp(b, a, sizeof(a)) < 0);
}

void test_UT_STR_008_memset_returns_destination_pointer(void) {
    uint8_t buf[8];
    void *ret = memset(buf, 0x5A, sizeof(buf));
    TEST_ASSERT_EQUAL_PTR(buf, ret);
}

void test_UT_STR_009_memcpy_returns_destination_pointer(void) {
    uint8_t src[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t dst[8] = {0};
    void *ret = memcpy(dst, src, sizeof(src));
    TEST_ASSERT_EQUAL_PTR(dst, ret);
    TEST_ASSERT_EQUAL_MEMORY(src, dst, sizeof(src));
}

void test_UT_STR_010_memcpy_zero_length_leaves_destination_unchanged(void) {
    uint8_t src[4] = {9, 9, 9, 9};
    uint8_t dst[4] = {1, 2, 3, 4};

    memcpy(dst, src, 0);

    TEST_ASSERT_EQUAL_UINT8(1, dst[0]);
    TEST_ASSERT_EQUAL_UINT8(2, dst[1]);
    TEST_ASSERT_EQUAL_UINT8(3, dst[2]);
    TEST_ASSERT_EQUAL_UINT8(4, dst[3]);
}

void test_UT_STR_011_memcmp_zero_length_returns_equal(void) {
    uint8_t a[4] = {1, 2, 3, 4};
    uint8_t b[4] = {9, 8, 7, 6};
    TEST_ASSERT_EQUAL_INT(0, memcmp(a, b, 0));
}

void test_UT_STR_012_strlen_stops_at_first_null(void) {
    char s[6] = {'a', 'b', '\0', 'c', 'd', '\0'};
    TEST_ASSERT_EQUAL_UINT32(2U, (uint32_t)strlen(s));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_UT_STR_001_memset_fills_nonzero_pattern);
    RUN_TEST(test_UT_STR_002_memset_zeroes_large_region);
    RUN_TEST(test_UT_STR_003_memcpy_copies_aligned_blocks);
    RUN_TEST(test_UT_STR_004_memcpy_copies_unaligned_regions);
    RUN_TEST(test_UT_STR_005_strlen_handles_empty_and_non_empty);
    RUN_TEST(test_UT_STR_006_memcmp_reports_equality);
    RUN_TEST(test_UT_STR_007_memcmp_reports_ordering);
    RUN_TEST(test_UT_STR_008_memset_returns_destination_pointer);
    RUN_TEST(test_UT_STR_009_memcpy_returns_destination_pointer);
    RUN_TEST(test_UT_STR_010_memcpy_zero_length_leaves_destination_unchanged);
    RUN_TEST(test_UT_STR_011_memcmp_zero_length_returns_equal);
    RUN_TEST(test_UT_STR_012_strlen_stops_at_first_null);
    return UNITY_END();
}
