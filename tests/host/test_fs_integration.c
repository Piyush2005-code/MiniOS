#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "kernel/cmd.h"
#include "kernel/fs_cmds.h"
#include "kernel/ulfs.h"

extern void uart_capture_reset(void);
extern const char *uart_capture_get(void);

extern void ulfs_stub_reset(void);
extern void ulfs_stub_set_mkdir_result(Status s);
extern void ulfs_stub_set_unlink_result(Status s);
extern uint32_t ulfs_stub_get_sync_count(void);
extern const char *ulfs_stub_get_written_text(void);

static int g_registered = 0;

static void ensure_fs_registered(void) {
    if (!g_registered) {
        TEST_ASSERT_EQUAL_INT(STATUS_OK, FS_RegisterCommands());
        g_registered = 1;
    }
}

void setUp(void) {
    uart_capture_reset();
    ulfs_stub_reset();
    ensure_fs_registered();
}

void tearDown(void) {}

void test_IT_FS_001_fs_commands_registered_in_table(void) {
    uint32_t count = 0;
    const cmd_entry_t *table = CMD_GetTable(&count);
    (void)table;

    TEST_ASSERT_TRUE(count >= 12U);
}

void test_IT_FS_002_mkdir_dispatch_success_path(void) {
    char line[] = "mkdir logs";

    ulfs_stub_set_mkdir_result(STATUS_OK);
    CMD_Dispatch(line);

    TEST_ASSERT_NOT_EQUAL(NULL, (void *)strstr(uart_capture_get(), "created directory"));
    TEST_ASSERT_EQUAL_UINT32(1U, ulfs_stub_get_sync_count());
}

void test_IT_FS_003_mkdir_dispatch_already_exists_path(void) {
    char line[] = "mkdir exists";

    ulfs_stub_set_mkdir_result(STATUS_ERROR_INVALID_ARGUMENT);
    CMD_Dispatch(line);

    TEST_ASSERT_NOT_EQUAL(NULL, (void *)strstr(uart_capture_get(), "already exists"));
}

void test_IT_FS_004_write_then_cat_round_trip(void) {
    char write_line[] = "write note.txt hello world";
    char cat_line[] = "cat note.txt";

    CMD_Dispatch(write_line);
    TEST_ASSERT_EQUAL_STRING("hello world\n", ulfs_stub_get_written_text());

    uart_capture_reset();
    CMD_Dispatch(cat_line);
    TEST_ASSERT_NOT_EQUAL(NULL, (void *)strstr(uart_capture_get(), "hello world"));
}

void test_IT_FS_005_rm_non_empty_directory_reports_error(void) {
    char line[] = "rm nonempty";

    ulfs_stub_set_unlink_result(STATUS_ERROR_NOT_SUPPORTED);
    CMD_Dispatch(line);

    TEST_ASSERT_NOT_EQUAL(NULL, (void *)strstr(uart_capture_get(), "directory not empty"));
}

void test_IT_FS_006_df_prints_disk_usage_summary(void) {
    char line[] = "df";

    CMD_Dispatch(line);

    TEST_ASSERT_NOT_EQUAL(NULL, (void *)strstr(uart_capture_get(), "ULFS Filesystem Usage"));
    TEST_ASSERT_NOT_EQUAL(NULL, (void *)strstr(uart_capture_get(), "Usage :"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_IT_FS_001_fs_commands_registered_in_table);
    RUN_TEST(test_IT_FS_002_mkdir_dispatch_success_path);
    RUN_TEST(test_IT_FS_003_mkdir_dispatch_already_exists_path);
    RUN_TEST(test_IT_FS_004_write_then_cat_round_trip);
    RUN_TEST(test_IT_FS_005_rm_non_empty_directory_reports_error);
    RUN_TEST(test_IT_FS_006_df_prints_disk_usage_summary);
    return UNITY_END();
}
