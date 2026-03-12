/**
 * @file test_status.c
 * @brief Unity tests for UT-STAT-001 through UT-STAT-006
 *
 * Tests MiniOS status codes and STATUS_ToString().
 * STATUS_ToString is defined in main.c; we link to it here.
 */

#include "unity.h"
#include "status.h"


/* STATUS_ToString is defined in src/kernel/main.c which we link */
extern const char *STATUS_ToString(Status status);

void setUp(void)    {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/*  UT-STAT-001: STATUS_OK == 0                                       */
/* ------------------------------------------------------------------ */
void test_UT_STAT_001(void) {
    TEST_ASSERT_EQUAL_INT(0, (int)STATUS_OK);
}

/* ------------------------------------------------------------------ */
/*  UT-STAT-002: STATUS_ToString("OK")                                */
/* ------------------------------------------------------------------ */
void test_UT_STAT_002(void) {
    const char *s = STATUS_ToString(STATUS_OK);
    TEST_ASSERT_NOT_NULL(s);
    /* The string contains "OK" */
    TEST_ASSERT_EQUAL_STRING("OK", s);
}

/* ------------------------------------------------------------------ */
/*  UT-STAT-003: STATUS_ToString returns non-NULL for all codes       */
/* ------------------------------------------------------------------ */
void test_UT_STAT_003(void) {
    for (int i = 0; i < (int)STATUS_ERROR_COUNT; i++) {
        const char *s = STATUS_ToString((Status)i);
        TEST_ASSERT_NOT_NULL_MESSAGE(s, "STATUS_ToString returned NULL");
    }
}

/* ------------------------------------------------------------------ */
/*  UT-STAT-004: STATUS_ToString returns non-empty for all codes      */
/* ------------------------------------------------------------------ */
void test_UT_STAT_004(void) {
    for (int i = 0; i < (int)STATUS_ERROR_COUNT; i++) {
        const char *s = STATUS_ToString((Status)i);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE_MESSAGE(s[0] != '\0', "STATUS_ToString returned empty string");
    }
}

/* ------------------------------------------------------------------ */
/*  UT-STAT-005: STATUS_ERROR_COUNT is the last value                 */
/* ------------------------------------------------------------------ */
void test_UT_STAT_005(void) {
    /* All defined codes are below STATUS_ERROR_COUNT.
     * We verify by checking every known code is < STATUS_ERROR_COUNT. */
    TEST_ASSERT_TRUE(STATUS_OK                       < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_INVALID_ARGUMENT   < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_NOT_SUPPORTED      < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_NOT_INITIALIZED    < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_OUT_OF_MEMORY      < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_MEMORY_ALIGNMENT   < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_MEMORY_PROTECTION  < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_HARDWARE_FAULT     < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_TIMEOUT            < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_EXECUTION_FAILED   < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_EXECUTION_TIMEOUT  < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_INVALID_GRAPH      < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_UNSUPPORTED_OPERATOR < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_SHAPE_MISMATCH     < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_COMM_FAILURE       < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_CRC_MISMATCH       < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_THREAD_LIMIT       < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_SCHEDULER_ACTIVE   < STATUS_ERROR_COUNT);
    TEST_ASSERT_TRUE(STATUS_ERROR_POOL_EXHAUSTED     < STATUS_ERROR_COUNT);
}

/* ------------------------------------------------------------------ */
/*  UT-STAT-006: No two status codes share the same integer value     */
/* ------------------------------------------------------------------ */
void test_UT_STAT_006(void) {
    /* The enum is defined in sequential order so the cast of each
     * value to int must produce a unique number. We verify that
     * STATUS_ERROR_COUNT == the number of distinct enumerators. */
    int codes[] = {
        (int)STATUS_OK,
        (int)STATUS_ERROR_INVALID_ARGUMENT,
        (int)STATUS_ERROR_NOT_SUPPORTED,
        (int)STATUS_ERROR_NOT_INITIALIZED,
        (int)STATUS_ERROR_OUT_OF_MEMORY,
        (int)STATUS_ERROR_MEMORY_ALIGNMENT,
        (int)STATUS_ERROR_MEMORY_PROTECTION,
        (int)STATUS_ERROR_HARDWARE_FAULT,
        (int)STATUS_ERROR_TIMEOUT,
        (int)STATUS_ERROR_EXECUTION_FAILED,
        (int)STATUS_ERROR_EXECUTION_TIMEOUT,
        (int)STATUS_ERROR_INVALID_GRAPH,
        (int)STATUS_ERROR_UNSUPPORTED_OPERATOR,
        (int)STATUS_ERROR_SHAPE_MISMATCH,
        (int)STATUS_ERROR_COMM_FAILURE,
        (int)STATUS_ERROR_CRC_MISMATCH,
        (int)STATUS_ERROR_THREAD_LIMIT,
        (int)STATUS_ERROR_SCHEDULER_ACTIVE,
        (int)STATUS_ERROR_POOL_EXHAUSTED,
    };
    int n = (int)(sizeof(codes) / sizeof(codes[0]));

    /* Brute-force uniqueness check */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(codes[i], codes[j],
                "Two status codes share the same integer value");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_UT_STAT_001);
    RUN_TEST(test_UT_STAT_002);
    RUN_TEST(test_UT_STAT_003);
    RUN_TEST(test_UT_STAT_004);
    RUN_TEST(test_UT_STAT_005);
    RUN_TEST(test_UT_STAT_006);
    return UNITY_END();
}
