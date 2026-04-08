#include <stdint.h>

#include "unity.h"
#include "kernel/cmd.h"

extern void uart_capture_reset(void);

static int g_probe_argc = 0;
static char g_arg1[64];

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
    g_probe_argc = argc;
    if (argc > 1) {
        copy_token(g_arg1, argv[1], sizeof(g_arg1));
    } else {
        g_arg1[0] = '\0';
    }
}

void setUp(void) {
    g_probe_argc = 0;
    g_arg1[0] = '\0';
    uart_capture_reset();
}

void tearDown(void) {}

/*
 * Expected-fail gap test:
 * General-purpose shells usually preserve quoted strings as one argument,
 * but MiniOS tokenizer splits only by whitespace and does not parse quotes.
 */
void test_GAP_CMD_001_expect_shell_quoted_arg_behavior(void) {
    char line[] = "probe \"hello world\"";

    TEST_ASSERT_EQUAL_INT(STATUS_OK,
                          CMD_Register("probe", "probe command", probe_handler));

    CMD_Dispatch(line);

    TEST_ASSERT_EQUAL_INT(2, g_probe_argc);
    TEST_ASSERT_EQUAL_STRING("hello world", g_arg1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_GAP_CMD_001_expect_shell_quoted_arg_behavior);
    return UNITY_END();
}
