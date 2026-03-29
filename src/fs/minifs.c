/**
 * @file minifs.c
 * @brief MiniOS minimal in-memory filesystem implementation
 */

#include "fs/minifs.h"
#include "kernel/kmem.h"
#include "hal/uart.h"
#include "lib/string.h"

/* ------------------------------------------------------------------ */
/*  Global FS state — one root, lives in .bss                        */
/* ------------------------------------------------------------------ */

static mfs_root_t g_root;

/* ------------------------------------------------------------------ */
/*  Internal string helpers (no libc)                                */
/* ------------------------------------------------------------------ */

/** Returns 1 if strings are equal, 0 otherwise */
static int mfs_streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == '\0' && *b == '\0');
}

/** Copy src into dst, at most (max-1) chars, always null-terminates */
static void mfs_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Internal lookup helpers                                          */
/* ------------------------------------------------------------------ */

/** Find a directory by name. Returns NULL if not found. */
static mfs_dir_t *find_dir(const char *name) {
    for (uint32_t i = 0; i < g_root.dir_count; i++) {
        if (g_root.dirs[i].in_use &&
            mfs_streq(g_root.dirs[i].name, name)) {
            return &g_root.dirs[i];
        }
    }
    return NULL;
}

/** Find a file inside a directory. Returns NULL if not found. */
static mfs_file_t *find_file(mfs_dir_t *dir, const char *name) {
    for (uint32_t i = 0; i < MFS_MAX_FILES_PER_DIR; i++) {
        if (dir->files[i].in_use &&
            mfs_streq(dir->files[i].name, name)) {
            return &dir->files[i];
        }
    }
    return NULL;
}

/** Get an unused file slot in a directory. Returns NULL if full. */
static mfs_file_t *alloc_file_slot(mfs_dir_t *dir) {
    for (uint32_t i = 0; i < MFS_MAX_FILES_PER_DIR; i++) {
        if (!dir->files[i].in_use) {
            return &dir->files[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status MFS_Init(void) {
    memset(&g_root, 0, sizeof(g_root));
    HAL_UART_PutString("[MFS ] MiniFS initialized (RAM-only)\n");
    return STATUS_OK;
}

Status MFS_MkDir(const char *name) {
    if (!name) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Don't create a duplicate */
    if (find_dir(name)) return STATUS_OK;

    if (g_root.dir_count >= MFS_MAX_DIRS) {
        return STATUS_ERROR_POOL_EXHAUSTED;
    }

    mfs_dir_t *d = &g_root.dirs[g_root.dir_count];
    memset(d, 0, sizeof(*d));
    mfs_strcpy(d->name, name, MFS_NAME_MAX);
    d->in_use     = true;
    d->file_count = 0;
    g_root.dir_count++;

    HAL_UART_PutString("[MFS ] Created /");
    HAL_UART_PutString(name);
    HAL_UART_PutString("\n");
    return STATUS_OK;
}

Status MFS_Write(const char *dir_name, const char *file_name,
                 const uint8_t *data, uint32_t size) {
    if (!dir_name || !file_name || !data || size == 0)
        return STATUS_ERROR_INVALID_ARGUMENT;
    if (size > MFS_MAX_FILE_SIZE)
        return STATUS_ERROR_OUT_OF_MEMORY;

    mfs_dir_t *dir = find_dir(dir_name);
    if (!dir) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Find existing file or grab a new slot */
    mfs_file_t *f = find_file(dir, file_name);
    if (!f) {
        f = alloc_file_slot(dir);
        if (!f) return STATUS_ERROR_POOL_EXHAUSTED;
        dir->file_count++;
        f->in_use = true;
        mfs_strcpy(f->name, file_name, MFS_NAME_MAX);
    }

    /* Allocate from bump heap (permanent allocation, 8-byte aligned) */
    uint8_t *buf = (uint8_t *)KMEM_Alloc((size_t)size, 8);
    if (!buf) return STATUS_ERROR_OUT_OF_MEMORY;

    memcpy(buf, data, (size_t)size);
    f->data = buf;
    f->size = size;

    HAL_UART_PutString("[MFS ] Wrote /");
    HAL_UART_PutString(dir_name);
    HAL_UART_PutString("/");
    HAL_UART_PutString(file_name);
    HAL_UART_PutString(" (");
    HAL_UART_PutDec(size);
    HAL_UART_PutString(" bytes)\n");
    return STATUS_OK;
}

Status MFS_Read(const char *dir_name, const char *file_name,
                const uint8_t **out_data, uint32_t *out_size) {
    if (!dir_name || !file_name || !out_data || !out_size)
        return STATUS_ERROR_INVALID_ARGUMENT;

    mfs_dir_t *dir = find_dir(dir_name);
    if (!dir) return STATUS_ERROR_INVALID_ARGUMENT;

    mfs_file_t *f = find_file(dir, file_name);
    if (!f) return STATUS_ERROR_INVALID_ARGUMENT;

    *out_data = f->data;
    *out_size = f->size;
    return STATUS_OK;
}

void MFS_ListDir(const char *dir_name) {
    mfs_dir_t *dir = find_dir(dir_name);
    if (!dir) {
        HAL_UART_PutString("  (directory not found: ");
        HAL_UART_PutString(dir_name);
        HAL_UART_PutString(")\n");
        return;
    }

    HAL_UART_PutString("/");
    HAL_UART_PutString(dir_name);
    HAL_UART_PutString(":\n");

    bool found_any = false;
    for (uint32_t i = 0; i < MFS_MAX_FILES_PER_DIR; i++) {
        if (dir->files[i].in_use) {
            HAL_UART_PutString("  ");
            HAL_UART_PutString(dir->files[i].name);
            HAL_UART_PutString("  (");
            HAL_UART_PutDec(dir->files[i].size);
            HAL_UART_PutString(" bytes)\n");
            found_any = true;
        }
    }
    if (!found_any) {
        HAL_UART_PutString("  (empty)\n");
    }
}

void MFS_ListRoot(void) {
    HAL_UART_PutString("/:\n");
    if (g_root.dir_count == 0) {
        HAL_UART_PutString("  (empty)\n");
        return;
    }
    for (uint32_t i = 0; i < g_root.dir_count; i++) {
        if (g_root.dirs[i].in_use) {
            HAL_UART_PutString("  ");
            HAL_UART_PutString(g_root.dirs[i].name);
            HAL_UART_PutString("/\n");
        }
    }
}

mfs_root_t *MFS_GetRoot(void) {
    return &g_root;
}