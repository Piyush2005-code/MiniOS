/**
 * @file minifs.h
 * @brief MiniOS minimal in-memory filesystem
 *
 * A RAM-only filesystem. No disk. No flash.
 * Data lives in the kernel heap (bump allocator).
 *
 * Layout:
 *   /
 *   ├── tmp/      (scratch space)
 *   └── exec/     (incoming model weights land here)
 *
 * Per SRS FR-021: communication interface for model loading.
 */

#ifndef MINIOS_FS_MINIFS_H
#define MINIOS_FS_MINIFS_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Limits                                                            */
/* ------------------------------------------------------------------ */

/** Max files that can exist in a single directory */
#define MFS_MAX_FILES_PER_DIR   8

/** Max directories under root */
#define MFS_MAX_DIRS            4

/** Max characters in a file or directory name */
#define MFS_NAME_MAX            32

/** Max bytes a single file can hold (256 KB) */
#define MFS_MAX_FILE_SIZE       (256U * 1024U)

/* ------------------------------------------------------------------ */
/*  File descriptor                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    char     name[MFS_NAME_MAX]; /**< Filename e.g. "model.bin"       */
    uint8_t *data;               /**< Pointer into kernel heap         */
    uint32_t size;               /**< Number of bytes stored           */
    bool     in_use;             /**< Is this slot occupied?           */
} mfs_file_t;

/* ------------------------------------------------------------------ */
/*  Directory descriptor                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char       name[MFS_NAME_MAX];              /**< Dir name e.g. "exec" */
    mfs_file_t files[MFS_MAX_FILES_PER_DIR];   /**< Files in this dir    */
    uint32_t   file_count;                      /**< How many files exist */
    bool       in_use;                          /**< Is this slot used?   */
} mfs_dir_t;

/* ------------------------------------------------------------------ */
/*  Root filesystem                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    mfs_dir_t dirs[MFS_MAX_DIRS]; /**< All directories       */
    uint32_t  dir_count;           /**< How many dirs exist   */
} mfs_root_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the filesystem.
 * Must be called once in kernel_main() before SCHED_Start().
 * @return STATUS_OK always
 */
Status MFS_Init(void);

/**
 * @brief Create a directory under root.
 * Example: MFS_MkDir("exec") creates /exec
 * @param name Directory name (no slashes)
 * @return STATUS_OK on success
 */
Status MFS_MkDir(const char *name);

/**
 * @brief Write data into a file in a directory.
 * Allocates space from the kernel bump heap and copies data.
 * If the file already exists, it is overwritten.
 *
 * @param dir_name   Directory name e.g. "exec"
 * @param file_name  Filename e.g. "model.bin"
 * @param data       Pointer to data bytes to write
 * @param size       Number of bytes
 * @return STATUS_OK on success
 *         STATUS_ERROR_OUT_OF_MEMORY if heap is full
 *         STATUS_ERROR_POOL_EXHAUSTED if directory is full
 */
Status MFS_Write(const char *dir_name, const char *file_name,
                 const uint8_t *data, uint32_t size);

/**
 * @brief Read a file — returns pointer into heap, no copy.
 *
 * @param dir_name   Directory name
 * @param file_name  Filename
 * @param out_data   Set to point at the file's data
 * @param out_size   Set to file's size in bytes
 * @return STATUS_OK on success
 *         STATUS_ERROR_INVALID_ARGUMENT if not found
 */
Status MFS_Read(const char *dir_name, const char *file_name,
                const uint8_t **out_data, uint32_t *out_size);

/**
 * @brief Print all files in a directory to UART.
 * @param dir_name Directory to list
 */
void MFS_ListDir(const char *dir_name);

/**
 * @brief Print all directories in root to UART.
 */
void MFS_ListRoot(void);

/**
 * @brief Get pointer to the global filesystem root.
 * Used internally by the network module.
 */
mfs_root_t *MFS_GetRoot(void);

#endif /* MINIOS_FS_MINIFS_H */