/**
 * @file initfs.c
 * @brief Populate /storage with files embedded at build time.
 *
 * Uses the auto-generated initfs_data.c/h tables produced by
 * scripts/embed_storage.py to write files into the ULFS /storage
 * mount at boot.
 */

#include "kernel/initfs.h"
#include "kernel/ulfs.h"
#include "hal/uart.h"
#include "lib/string.h"
#include "initfs_data.h"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Ensure all parent directories for a given path exist.
 *
 * Given a path like "models/sub/foo.onnx", creates:
 *   /storage/models
 *   /storage/models/sub
 *
 * Stops before the final component (the filename).
 */
static void ensure_parent_dirs(const char *rel_path)
{
    char full[128];
    uint32_t fi = 0;  /* index into full */

    /* Start with /storage/ or / */
    const char *prefix = (rel_path[0]=='b'&&rel_path[1]=='e'&&rel_path[2]=='n'&&rel_path[3]=='c'&&rel_path[4]=='h'&&rel_path[5]=='/') ? "/" : "/storage/";
    while (*prefix) {
        full[fi++] = *prefix++;
    }

    const char *p = rel_path;
    while (*p) {
        if (*p == '/') {
            /* End of a directory component — try to create it */
            full[fi] = '\0';

            /* Ignore errors (directory may already exist) */
            ulfs_stat_t st;
            if (ULFS_Stat(full, &st) != STATUS_OK) {
                ULFS_Mkdir(full);
            }

            full[fi++] = '/';
            p++;
        } else {
            full[fi++] = *p;
            p++;
        }
    }
    /* Don't create the last component — that's the filename */
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

Status INITFS_Populate(void)
{
    if (INITFS_NUM_ENTRIES == 0) {
        HAL_UART_PutString("[INITFS] No embedded files\n");
        return STATUS_OK;
    }

    HAL_UART_PutString("[INITFS] Populating /storage (");
    HAL_UART_PutDec(INITFS_NUM_ENTRIES);
    HAL_UART_PutString(" files)...\n");

    uint32_t written = 0;
    uint32_t skipped = 0;

    ulfs_stat_t st_bench;
    if (ULFS_Stat("/bench", &st_bench) != STATUS_OK) ULFS_Mkdir("/bench");
    if (ULFS_Stat("/bench/results", &st_bench) != STATUS_OK) ULFS_Mkdir("/bench/results");

    for (uint32_t i = 0; i < INITFS_NUM_ENTRIES; i++) {
        const initfs_entry_t *e = &initfs_entries[i];
        
        if (e->path[0]=='b' && e->path[1]=='e' && e->path[2]=='n' && e->path[3]=='c') {
            skipped++;
            continue;
        }

        /* Build full path: "/storage/<rel_path>" */
        char path[128];
        const char *rp = e->path;
        const char *pfx = "/storage/";
        uint32_t pi = 0;
        while (*pfx) path[pi++] = *pfx++;
        while (*rp && pi < sizeof(path) - 1) path[pi++] = *rp++;
        path[pi] = '\0';

        /* Check if file already exists */
        ulfs_stat_t st;
        if (ULFS_Stat(path, &st) == STATUS_OK) {
            skipped++;
            continue;
        }

        /* Ensure parent directories exist */
        ensure_parent_dirs(e->path);

        /* Create and write the file */
        int fd = -1;
        Status s = ULFS_Open(path, ULFS_O_CREAT | ULFS_O_WRONLY, &fd);
        if (s != STATUS_OK) {
            HAL_UART_PutString("[INITFS] Failed to create: ");
            HAL_UART_PutString(path);
            HAL_UART_PutString("\n");
            continue;
        }

        uint32_t nwritten = 0;
        s = ULFS_Write(fd, e->data, e->size, &nwritten);
        ULFS_Close(fd);

        if (s == STATUS_OK && nwritten == e->size) {
            HAL_UART_PutString("[INITFS] Wrote ");
            HAL_UART_PutString(path);
            HAL_UART_PutString(" (");
            HAL_UART_PutDec(e->size);
            HAL_UART_PutString(" bytes)\n");
            written++;
        } else {
            HAL_UART_PutString("[INITFS] Write error: ");
            HAL_UART_PutString(path);
            HAL_UART_PutString("\n");
        }
    }

    HAL_UART_PutString("[INITFS] Done: ");
    HAL_UART_PutDec(written);
    HAL_UART_PutString(" written, ");
    HAL_UART_PutDec(skipped);
    HAL_UART_PutString(" skipped\n");

    return STATUS_OK;
}
