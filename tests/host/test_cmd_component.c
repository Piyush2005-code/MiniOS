#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "kernel/cmd.h"

extern void uart_capture_reset(void);
extern const char *uart_capture_get(void);

static int g_probe_called = 0;
static int g_probe_argc = 0;

static char g_arg0[64];
static char g_arg1[64];
static char g_arg2[64];
static char g_last_arg[64];

static void copy_token(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (max == 0U) {
        return;
    }
    while (i + 1U < max && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void probe_handler(int argc, char *argv[]) {
    g_probe_called++;
    g_probe_argc = argc;

    g_arg0[0] = '\0';
    g_arg1[0] = '\0';
    g_arg2[0] = '\0';
    g_last_arg[0] = '\0';

    if (argc > 0) {
        copy_token(g_arg0, argv[0], sizeof(g_arg0));
    }
    if (argc > 1) {
        copy_token(g_arg1, argv[1], sizeof(g_arg1));
    }
    if (argc > 2) {
        copy_token(g_arg2, argv[2], sizeof(g_arg2));
    }
    if (argc > 0) {
        copy_token(g_last_arg, argv[argc - 1], sizeof(g_last_arg));
    }
}

void setUp(void) {
    g_probe_called = 0;
    g_probe_argc = 0;
    g_arg0[0] = '\0';
    g_arg1[0] = '\0';
    g_arg2[0] = '\0';
    g_last_arg[0] = '\0';
    uart_capture_reset();
}

void tearDown(void) {}

void test_CT_CMD_001_register_rejects_null_arguments(void) {
    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_ARGUMENT,
                          CMD_Register(NULL, "h", probe_handler));
    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_INVALID_ARGUMENT,
                          CMD_Register("x", "h", NULL));
}

void test_CT_CMD_002_dispatch_is_case_insensitive_and_tokenizes(void) {
    char line[] = "   pRoBe   alpha   beta   ";

    TEST_ASSERT_EQUAL_INT(STATUS_OK,
                          CMD_Register("probe", "probe command", probe_handler));

    CMD_Dispatch(line);

    TEST_ASSERT_EQUAL_INT(1, g_probe_called);
    TEST_ASSERT_EQUAL_INT(3, g_probe_argc);
    TEST_ASSERT_EQUAL_STRING("pRoBe", g_arg0);
    TEST_ASSERT_EQUAL_STRING("alpha", g_arg1);
    TEST_ASSERT_EQUAL_STRING("beta", g_arg2);
}

void test_CT_CMD_003_unknown_command_prints_help_hint(void) {
    char line[] = "missingcmd";

    CMD_Dispatch(line);

    TEST_ASSERT_NOT_NULL(uart_capture_get());
    TEST_ASSERT_NOT_EQUAL(NULL, (void *)strstr(uart_capture_get(), "Unknown command"));
    TEST_ASSERT_NOT_EQUAL(NULL, (void *)strstr(uart_capture_get(), "type 'help'"));
}

void test_CT_CMD_004_blank_input_is_noop(void) {
    char line[] = "   \t \r\n   ";

    CMD_Dispatch(line);

    TEST_ASSERT_EQUAL_INT(0, g_probe_called);
    TEST_ASSERT_EQUAL_STRING("", uart_capture_get());
}

void test_CT_CMD_006_register_accepts_null_help_as_empty(void) {
    uint32_t before = 0;
    uint32_t after = 0;
    const cmd_entry_t *table = CMD_GetTable(&before);
    (void)table;

    TEST_ASSERT_EQUAL_INT(STATUS_OK,
                          CMD_Register("nullhelp", NULL, probe_handler));

    table = CMD_GetTable(&after);
    TEST_ASSERT_EQUAL_UINT32(before + 1U, after);
    TEST_ASSERT_EQUAL_STRING("", table[after - 1U].help);
}

void test_CT_CMD_007_dispatch_limits_argument_count_to_max(void) {
    char line[] = "probe a b c d e f g h i j k l m";

    TEST_ASSERT_EQUAL_INT(STATUS_OK,
                          CMD_Register("probe", "arg-limit test", probe_handler));

    CMD_Dispatch(line);

    TEST_ASSERT_EQUAL_INT(CMD_MAX_ARGS, g_probe_argc);
    TEST_ASSERT_EQUAL_STRING("k", g_last_arg);
}

void test_CT_CMD_008_register_rejects_empty_name(void) {
    TEST_ASSERT_NOT_EQUAL(STATUS_OK,
                          CMD_Register("", "empty-name", probe_handler));
}

void test_CT_CMD_009_register_rejects_whitespace_name(void) {
    TEST_ASSERT_NOT_EQUAL(STATUS_OK,
                          CMD_Register("bad name", "space-in-name", probe_handler));
}

void test_CT_CMD_010_register_rejects_duplicate_name(void) {
    TEST_ASSERT_EQUAL_INT(STATUS_OK,
                          CMD_Register("dupcheck", "first", probe_handler));
    TEST_ASSERT_NOT_EQUAL(STATUS_OK,
                          CMD_Register("dupcheck", "second", probe_handler));
}

void test_CT_CMD_011_register_rejects_case_insensitive_duplicate_name(void) {
    TEST_ASSERT_EQUAL_INT(STATUS_OK,
                          CMD_Register("CaseDup", "first", probe_handler));
    TEST_ASSERT_NOT_EQUAL(STATUS_OK,
                          CMD_Register("casedup", "second", probe_handler));
}

void test_CT_CMD_005_command_table_limits_are_enforced(void) {
    uint32_t count = 0;
    const cmd_entry_t *table = CMD_GetTable(&count);
    (void)table;

    while (count < CMD_MAX_COMMANDS) {
        char name[CMD_NAME_MAX];
        uint32_t idx = count;
        uint32_t p = 0;

        name[p++] = 'c';
        name[p++] = 'm';
        name[p++] = 'd';
        do {
            name[p++] = (char)('0' + (idx % 10U));
            idx /= 10U;
        } while (idx > 0U && p + 1U < CMD_NAME_MAX);
        name[p] = '\0';

        TEST_ASSERT_EQUAL_INT(STATUS_OK, CMD_Register(name, "filler", probe_handler));
        CMD_GetTable(&count);
    }

    TEST_ASSERT_EQUAL_INT(STATUS_ERROR_POOL_EXHAUSTED,
                          CMD_Register("overflow", "x", probe_handler));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_CT_CMD_001_register_rejects_null_arguments);
    RUN_TEST(test_CT_CMD_002_dispatch_is_case_insensitive_and_tokenizes);
    RUN_TEST(test_CT_CMD_003_unknown_command_prints_help_hint);
    RUN_TEST(test_CT_CMD_004_blank_input_is_noop);
    RUN_TEST(test_CT_CMD_006_register_accepts_null_help_as_empty);
    RUN_TEST(test_CT_CMD_007_dispatch_limits_argument_count_to_max);
    RUN_TEST(test_CT_CMD_008_register_rejects_empty_name);
    RUN_TEST(test_CT_CMD_009_register_rejects_whitespace_name);
    RUN_TEST(test_CT_CMD_010_register_rejects_duplicate_name);
    RUN_TEST(test_CT_CMD_011_register_rejects_case_insensitive_duplicate_name);
    RUN_TEST(test_CT_CMD_005_command_table_limits_are_enforced);
    return UNITY_END();
}
