/**
 * @file initfs.h
 * @brief Initial filesystem population — embeds host files into /storage at boot.
 *
 * At build time, scripts/embed_storage.py converts files in src/storage/
 * into C byte arrays (build/gen/initfs_data.c). At boot, INITFS_Populate()
 * writes them into the ULFS /storage mount (NVRAM store).
 *
 * Files that already exist in /storage are skipped to avoid overwriting
 * user modifications across reboots.
 */

#ifndef MINIOS_KERNEL_INITFS_H
#define MINIOS_KERNEL_INITFS_H

#include "status.h"

/**
 * @brief Populate /storage with files embedded at build time.
 *
 * Must be called after ULFS_Init() and STORAGE_Init() have completed.
 *
 * @return STATUS_OK on success (all files written or already exist).
 */
Status INITFS_Populate(void);

#endif /* MINIOS_KERNEL_INITFS_H */
