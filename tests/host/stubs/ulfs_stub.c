#include <stdbool.h>
#include <stdint.h>

#include "kernel/ulfs.h"

#define ULFS_STUB_TEXT_MAX 512

static Status g_mkdir_result = STATUS_OK;
static Status g_unlink_result = STATUS_OK;
static uint32_t g_sync_count = 0;

static char g_cwd[ULFS_PATH_MAX] = "/";
static char g_written[ULFS_STUB_TEXT_MAX];
static uint32_t g_written_len = 0;
static uint32_t g_read_pos = 0;

static void ulfs_stub_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (max == 0) {
        return;
    }
    while (i + 1 < max && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void ulfs_stub_reset(void) {
    g_mkdir_result = STATUS_OK;
    g_unlink_result = STATUS_OK;
    g_sync_count = 0;
    ulfs_stub_copy(g_cwd, "/", ULFS_PATH_MAX);
    g_written[0] = '\0';
    g_written_len = 0;
    g_read_pos = 0;
}

void ulfs_stub_set_mkdir_result(Status s) { g_mkdir_result = s; }
void ulfs_stub_set_unlink_result(Status s) { g_unlink_result = s; }
uint32_t ulfs_stub_get_sync_count(void) { return g_sync_count; }
const char *ulfs_stub_get_written_text(void) { return g_written; }

Status ULFS_Init(void) { return STATUS_OK; }

void ULFS_Sync(void) { g_sync_count++; }

Status ULFS_Open(const char *path, uint8_t flags, int *fd_out) {
    if (path == NULL || fd_out == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    if ((flags & ULFS_O_WRONLY) != 0U) {
        g_written[0] = '\0';
        g_written_len = 0;
        *fd_out = 4;
        return STATUS_OK;
    }

    if ((flags & ULFS_O_RDONLY) != 0U) {
        g_read_pos = 0;
        *fd_out = 3;
        return STATUS_OK;
    }

    return STATUS_ERROR_NOT_SUPPORTED;
}

Status ULFS_Close(int fd) {
    (void)fd;
    return STATUS_OK;
}

Status ULFS_Read(int fd, void *buf, uint32_t count, uint32_t *nread) {
    uint8_t *out = (uint8_t *)buf;

    if (fd != 3 || buf == NULL || nread == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    if (g_read_pos >= g_written_len) {
        *nread = 0;
        return STATUS_OK;
    }

    uint32_t remaining = g_written_len - g_read_pos;
    uint32_t to_copy = (count < remaining) ? count : remaining;
    for (uint32_t i = 0; i < to_copy; i++) {
        out[i] = (uint8_t)g_written[g_read_pos + i];
    }

    g_read_pos += to_copy;
    *nread = to_copy;
    return STATUS_OK;
}

Status ULFS_Write(int fd, const void *buf, uint32_t count, uint32_t *nwritten) {
    const uint8_t *in = (const uint8_t *)buf;

    if (fd != 4 || buf == NULL || nwritten == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    uint32_t room = (ULFS_STUB_TEXT_MAX - 1U) - g_written_len;
    uint32_t to_copy = (count < room) ? count : room;

    for (uint32_t i = 0; i < to_copy; i++) {
        g_written[g_written_len + i] = (char)in[i];
    }

    g_written_len += to_copy;
    g_written[g_written_len] = '\0';
    *nwritten = to_copy;

    return (to_copy == count) ? STATUS_OK : STATUS_ERROR_OUT_OF_MEMORY;
}

Status ULFS_Create(const char *path) {
    return (path == NULL) ? STATUS_ERROR_INVALID_ARGUMENT : STATUS_OK;
}

Status ULFS_Mkdir(const char *path) {
    if (path == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    return g_mkdir_result;
}

Status ULFS_Unlink(const char *path) {
    if (path == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    return g_unlink_result;
}

Status ULFS_Readdir(const char *path, ulfs_readdir_cb_t callback, void *user_data) {
    ulfs_stat_t st_file;
    ulfs_stat_t st_dir;

    if (path == NULL || callback == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    st_file.ino = 1;
    st_file.type = ULFS_TYPE_FILE;
    st_file.size = g_written_len;
    st_file.blocks = 1;

    st_dir.ino = 2;
    st_dir.type = ULFS_TYPE_DIR;
    st_dir.size = 0;
    st_dir.blocks = 1;

    callback("note.txt", &st_file, user_data);
    callback("logs", &st_dir, user_data);
    return STATUS_OK;
}

Status ULFS_Stat(const char *path, ulfs_stat_t *st) {
    if (path == NULL || st == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    st->ino = 1;
    st->type = ULFS_TYPE_FILE;
    st->size = g_written_len;
    st->blocks = 1;
    return STATUS_OK;
}

Status ULFS_ChDir(const char *path) {
    if (path == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    if (path[0] == '/') {
        ulfs_stub_copy(g_cwd, path, ULFS_PATH_MAX);
        return STATUS_OK;
    }

    if (path[0] == '.' && path[1] == '\0') {
        return STATUS_OK;
    }

    if (path[0] == '.' && path[1] == '.' && path[2] == '\0') {
        ulfs_stub_copy(g_cwd, "/", ULFS_PATH_MAX);
        return STATUS_OK;
    }

    return STATUS_ERROR_INVALID_ARGUMENT;
}

Status ULFS_GetCwd(char *buf, uint32_t buflen) {
    if (buf == NULL || buflen == 0U) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    ulfs_stub_copy(buf, g_cwd, buflen);
    return STATUS_OK;
}

void ULFS_GetStats(uint32_t *total, uint32_t *used, uint32_t *free_blks) {
    if (total != NULL) {
        *total = ULFS_TOTAL_BLOCKS;
    }
    if (used != NULL) {
        *used = 10U;
    }
    if (free_blks != NULL) {
        *free_blks = ULFS_TOTAL_BLOCKS - 10U;
    }
}
