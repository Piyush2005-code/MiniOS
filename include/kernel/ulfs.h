/**
 * @file ulfs.h
 * @brief ULFS — Ultra Lightweight File System for MiniOS
 *
 * An in-memory file system derived from the design described in:
 *   "ULFS: Ultra Lightweight File System" (Appl. Sci. 2024, 14, 3342)
 *
 * Design principles (adapted for bare-metal ARM64):
 *  - Fixed 4 KB blocks identified by block-id (bid)
 *  - Lock-free: single-threaded unikernel, no need for mutexes
 *  - Bitmap allocation via map-blocks (1 bit per block)
 *  - Fixed-size 24-byte inodes (170 per inode-block)
 *  - Indirect block indexing for files > 4 KB
 *  - Fixed-size 32-byte directory entries (128 per block)
 *  - Backing store carved from KMEM heap at init time
 *
 * Block layout (after ULFS_Init):
 *   bid 0 : Superblock
 *   bid 1 : Map Block 0 (bitmap for bids 0..4095)
 *   bid 2 : First Inode Block  (inodes 0..169)
 *   bid 3 : Root directory data block
 *   bid 4+: Dynamically allocated data / directory / bid blocks
 *
 * @note FR-021 (UART I/O), DC-002 (static allocation), SRS minimalism.
 */

#ifndef MINIOS_KERNEL_ULFS_H
#define MINIOS_KERNEL_ULFS_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Configuration constants                                           */
/* ------------------------------------------------------------------ */

/** Total number of 4 KB blocks in the file system backing store */
#define ULFS_TOTAL_BLOCKS     512U

/** Block size in bytes */
#define ULFS_BLOCK_SIZE       4096U

/** Total backing store size (512 × 4 KB = 2 MB) */
#define ULFS_STORE_SIZE       (ULFS_TOTAL_BLOCKS * ULFS_BLOCK_SIZE)

/** Magic number written to the superblock */
#define ULFS_MAGIC            0x554C4653U   /* "ULFS" */

/** Number of inodes per inode-block */
#define ULFS_INODES_PER_BLOCK 170U

/** Number of directory entries per block */
#define ULFS_DIRENTS_PER_BLOCK 128U

/** Maximum number of block-ids (bids) in a single bid-block (indirect) */
#define ULFS_BIDS_PER_BIDBLOCK 1023U

/** Maximum filename length (including null terminator) */
#define ULFS_NAME_MAX         28U

/** Maximum number of simultaneously open file descriptors */
#define ULFS_MAX_OPEN_FILES   8U

/** Maximum path length */
#define ULFS_PATH_MAX         128U

/** Maximum directory nesting depth */
#define ULFS_MAX_DEPTH        8U

/* ------------------------------------------------------------------ */
/*  Well-known block ids                                              */
/* ------------------------------------------------------------------ */

#define ULFS_BID_SUPERBLOCK   0U  /**< Superblock always at bid 0 */
#define ULFS_BID_MAPBLOCK0    1U  /**< First map block at bid 1   */
#define ULFS_BID_INODE0       2U  /**< First inode block at bid 2 */
#define ULFS_BID_ROOT_DIR     3U  /**< Root directory data block  */

/* ------------------------------------------------------------------ */
/*  Inode types                                                       */
/* ------------------------------------------------------------------ */

#define ULFS_TYPE_FREE   0U   /**< Inode slot is unused */
#define ULFS_TYPE_FILE   1U   /**< Regular file         */
#define ULFS_TYPE_DIR    2U   /**< Directory            */

/* ------------------------------------------------------------------ */
/*  On-disk / in-memory block structures                              */
/* ------------------------------------------------------------------ */

/**
 * @brief ULFS Superblock (bid 0, 4096 bytes, only first 16 bytes used)
 *
 * Sits at the start of the backing store. Identifies the FS and records
 * the configured capacity.
 */
typedef struct {
    uint32_t magic;          /**< ULFS_MAGIC = 0x554C4653              */
    uint32_t total_blocks;   /**< Total number of 4 KB blocks           */
    uint32_t block_size;     /**< Block size in bytes (always 4096)     */
    uint32_t inode_count;    /**< Number of inodes allocated so far     */
    uint8_t  _pad[ULFS_BLOCK_SIZE - 16]; /**< Padding to fill 4 KB     */
} __attribute__((packed)) ulfs_superblock_t;

/**
 * @brief ULFS Map Block — bitmap tracking block allocation
 *
 * Each bit represents one block: 0 = free, 1 = allocated.
 * With 4096 bytes = 32768 bits we can track 32768 blocks,
 * but we only use ULFS_TOTAL_BLOCKS (512) of them.
 */
typedef struct {
    uint8_t bitmap[ULFS_BLOCK_SIZE]; /**< 1 bit per block, LSB first  */
} __attribute__((packed)) ulfs_mapblock_t;

/**
 * @brief ULFS Inode — 24 bytes, 170 fit in one inode-block
 *
 * Tracks a single file or directory.
 */
typedef struct {
    uint8_t  type;       /**< ULFS_TYPE_FREE / FILE / DIR              */
    uint8_t  _pad0[3];   /**< Alignment padding                         */
    uint32_t size;       /**< File size in bytes                        */
    uint32_t bid_head;   /**< First data block bid (0 = none)           */
    uint32_t bid_block;  /**< Indirect index block bid (0 = none)       */
    uint8_t  _reserved[8]; /**< Future use                             */
} __attribute__((packed)) ulfs_inode_t;   /* exactly 24 bytes */

/**
 * @brief ULFS Inode Block — holds 170 inodes in a 4096-byte block
 *
 * Inode blocks are chained as a linked list when more inodes are needed.
 */
typedef struct {
    uint32_t     n_used;               /**< Number of in-use inodes   */
    uint32_t     ino_start;            /**< First inode number here   */
    uint32_t     bid_prev;             /**< Previous inode-block bid  */
    uint32_t     bid_next;             /**< Next inode-block bid (0=end)*/
    ulfs_inode_t inodes[ULFS_INODES_PER_BLOCK]; /**< 170 × 24 = 4080  */
} __attribute__((packed)) ulfs_inode_block_t;   /* 16 + 4080 = 4096 bytes */

/**
 * @brief ULFS Directory Entry — 32 bytes, 128 fit in one 4 KB block
 */
typedef struct {
    uint32_t ino;                /**< Inode number (0 = free slot)     */
    char     name[ULFS_NAME_MAX]; /**< Null-terminated filename         */
} __attribute__((packed)) ulfs_dirent_t;   /* exactly 32 bytes */

/**
 * @brief ULFS Bid Block — holds up to 1023 block-ids for indirect access
 *
 * Used when a file needs more than 1 data block (size > 4 KB).
 * Stores bids for blocks 1..1022 (block 0 is stored directly in the inode).
 */
typedef struct {
    uint32_t bids[ULFS_BIDS_PER_BIDBLOCK]; /**< 1023 × 4 = 4092 bytes */
    uint32_t _pad;                          /**< 4 bytes padding         */
} __attribute__((packed)) ulfs_bid_block_t; /* 4096 bytes */

/* ------------------------------------------------------------------ */
/*  File descriptor                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Open file descriptor returned by ULFS_Open / ULFS_Create
 */
typedef struct {
    uint32_t store_idx;/**< Store index (0=volatile, 1=non-volatile)   */
    uint32_t ino;      /**< Inode number of this file (0 = slot free)  */
    uint32_t pos;      /**< Current read/write byte position            */
    uint8_t  flags;    /**< Open flags: bit0=read, bit1=write           */
} ulfs_fd_t;

#define ULFS_O_RDONLY  0x01U
#define ULFS_O_WRONLY  0x02U
#define ULFS_O_RDWR    0x03U
#define ULFS_O_CREAT   0x10U   /**< Create if not exists (use with ULFS_Open) */
#define ULFS_O_TRUNC   0x20U   /**< Truncate existing file on open            */

/* ------------------------------------------------------------------ */
/*  Stat structure                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t ino;
    uint8_t  type;     /**< ULFS_TYPE_FILE or ULFS_TYPE_DIR             */
    uint32_t size;     /**< File size in bytes                          */
    uint32_t blocks;   /**< Number of 4 KB blocks used                  */
} ulfs_stat_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the ULFS file system.
 *
 * Allocates ULFS_STORE_SIZE bytes from the kernel heap, formats the
 * superblock, map block, root inode-block, and root directory block.
 * Must be called after KMEM_Init() and before SCHED_Start().
 *
 * @return STATUS_OK on success, STATUS_ERROR_OUT_OF_MEMORY if heap
 *         allocation fails.
 *
 * @complexity O(1) — fixed-size format pass.
 */
Status ULFS_Init(void);

/**
 * @brief Sync the Non-Volatile file system to underlying Flash Memory.
 *
 * Checks if the NVRAM shadow buffer has been modified. If so, it
 * actively commits the entire 2 MB memory slab to QEMU pflash1.
 */
void ULFS_Sync(void);

/**
 * @brief Open (and optionally create) a file.
 *
 * @param[in]  path   Absolute or relative path.
 * @param[in]  flags  ULFS_O_* combination.
 * @param[out] fd_out Pointer to receive file descriptor index (0..ULFS_MAX_OPEN_FILES-1).
 *
 * @return STATUS_OK, STATUS_ERROR_NOT_INITIALIZED, STATUS_ERROR_INVALID_ARGUMENT,
 *         STATUS_ERROR_OUT_OF_MEMORY (no free fd / no disk space).
 */
Status ULFS_Open(const char *path, uint8_t flags, int *fd_out);

/**
 * @brief Close a file descriptor.
 *
 * @param[in] fd  File descriptor index returned by ULFS_Open.
 * @return STATUS_OK or STATUS_ERROR_INVALID_ARGUMENT.
 */
Status ULFS_Close(int fd);

/**
 * @brief Read bytes from an open file.
 *
 * @param[in]  fd      File descriptor.
 * @param[out] buf     Destination buffer.
 * @param[in]  count   Bytes to read.
 * @param[out] nread   Bytes actually read (may be < count at EOF).
 *
 * @return STATUS_OK or error code.
 */
Status ULFS_Read(int fd, void *buf, uint32_t count, uint32_t *nread);

/**
 * @brief Write bytes to an open file.
 *
 * Automatically allocates new data blocks as needed.
 *
 * @param[in]  fd      File descriptor.
 * @param[in]  buf     Source buffer.
 * @param[in]  count   Bytes to write.
 * @param[out] nwritten Bytes actually written.
 *
 * @return STATUS_OK or error code.
 */
Status ULFS_Write(int fd, const void *buf, uint32_t count, uint32_t *nwritten);

/**
 * @brief Create a new empty file at the given path.
 *
 * @param[in] path  Absolute or relative path (file must not already exist).
 * @return STATUS_OK or error code.
 */
Status ULFS_Create(const char *path);

/**
 * @brief Create a new directory.
 *
 * @param[in] path  Absolute or relative path.
 * @return STATUS_OK or error code.
 */
Status ULFS_Mkdir(const char *path);

/**
 * @brief Remove a file or empty directory.
 *
 * @param[in] path  Absolute or relative path.
 * @return STATUS_OK or STATUS_ERROR_NOT_SUPPORTED (dir not empty).
 */
Status ULFS_Unlink(const char *path);

/**
 * @brief Iterate over directory entries.
 *
 * Scans the directory at @p path. For each non-free entry, calls
 * the callback with the entry's name and inode info.
 *
 * @param[in] path       Directory path.
 * @param[in] callback   Called for each entry: (name, stat, user_data).
 * @param[in] user_data  Passed through to callback.
 *
 * @return STATUS_OK or error code.
 */
typedef void (*ulfs_readdir_cb_t)(const char *name,
                                   const ulfs_stat_t *st,
                                   void *user_data);
Status ULFS_Readdir(const char *path, ulfs_readdir_cb_t callback,
                    void *user_data);

/**
 * @brief Get file/directory status.
 *
 * @param[in]  path   Absolute or relative path.
 * @param[out] st     Stat structure to fill.
 *
 * @return STATUS_OK or error code.
 */
Status ULFS_Stat(const char *path, ulfs_stat_t *st);

/**
 * @brief Change the current working directory.
 *
 * @param[in] path  Absolute or relative path to a directory.
 * @return STATUS_OK or STATUS_ERROR_INVALID_ARGUMENT.
 */
Status ULFS_ChDir(const char *path);

/**
 * @brief Get the current working directory path.
 *
 * @param[out] buf   Buffer to receive null-terminated path string.
 * @param[in]  size  Buffer size in bytes.
 *
 * @return STATUS_OK. Always writes at least "/" into buf.
 */
Status ULFS_GetCwd(char *buf, uint32_t size);

/**
 * @brief Get file system usage statistics.
 *
 * @param[out] total_blocks  Total blocks in FS.
 * @param[out] used_blocks   Blocks currently allocated.
 * @param[out] free_blocks   Blocks available.
 */
void ULFS_GetStats(uint32_t *total_blocks, uint32_t *used_blocks,
                   uint32_t *free_blocks);

#endif /* MINIOS_KERNEL_ULFS_H */
