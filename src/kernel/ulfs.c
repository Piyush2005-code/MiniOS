/**
 * @file ulfs.c
 * @brief ULFS — Ultra Lightweight File System Implementation
 *
 * In-memory file system for MiniOS based on the ULFS design from:
 *   "ULFS: Ultra Lightweight File System" (Appl. Sci. 2024, 14, 3342)
 *
 * The backing store is a flat 2 MB region (512 × 4 KB blocks) allocated
 * from the KMEM heap at boot time. All "disk I/O" is direct memory access
 * via pointer arithmetic on this region — matching the paper's mmap-based
 * design philosophy (zero-copy, no page-fault overhead).
 *
 * === Data Structure Summary ===
 *
 *  bid 0 : Superblock        (ulfs_superblock_t)
 *  bid 1 : Map Block 0       (ulfs_mapblock_t — 512-bit bitmap)
 *  bid 2 : Inode Block 0     (ulfs_inode_block_t — 170 inodes)
 *  bid 3 : Root Dir Data     (ulfs_dirent_t[128])
 *  bid 4+: Dynamically allocated
 *
 * === Algorithm Complexity ===
 *
 *  bid_alloc()               O(n/8) bitmap scan  ~ O(64) for 512 blocks
 *  inode_alloc()             O(k) where k = # inode blocks (usually 1)
 *  bid_for_file_offset()     O(1) for direct block; O(1) for indirect
 *  dirent_lookup()           O(n) where n = # entries in directory
 *  path_resolve()            O(depth × entries_per_dir)
 *
 * @note SRS DC-002: no dynamic allocation after boot — all space pre-reserved.
 * @note SRS DC-001: C11, no POSIX / standard library.
 */

#include "kernel/ulfs.h"
#include "kernel/kmem.h"
#include "kernel/storage.h"
#include "lib/string.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Internal state                                                    */
/* ------------------------------------------------------------------ */

#define ULFS_STORE_VOL      0
#define ULFS_STORE_NVRAM    1
#define ULFS_NUM_STORES     2

/** Backing store pointers [0]=Volatile [1]=NVRAM */
static uint8_t *g_fs_base[ULFS_NUM_STORES] = {NULL, NULL};

/** Cached superblocks */
static ulfs_superblock_t *g_sb[ULFS_NUM_STORES] = {NULL, NULL};

/** Dirty flags for syncing NVRAM back to flash */
static bool g_is_dirty[ULFS_NUM_STORES] = {false, false};

/** The store index currently being operated upon internally */
static uint32_t g_current_store = 0;

/** Whether ULFS base system has been initialized */
static bool g_initialized = false;

/** Open file descriptor table */
static ulfs_fd_t g_fdt[ULFS_MAX_OPEN_FILES];

/** Current working directory (store and inode mapping) */
static uint32_t g_cwd_store = ULFS_STORE_VOL;
static uint32_t g_cwd_ino = 0;   /* 0 = root */

/** Current working directory absolute string */
static char g_cwd_path[ULFS_PATH_MAX] = "/";

/* ------------------------------------------------------------------ */
/*  Pointer helpers: bid → memory address                            */
/* ------------------------------------------------------------------ */

/** Convert a block-id to a raw byte pointer */
static inline uint8_t *bid_to_ptr(uint32_t bid)
{
    return g_fs_base[g_current_store] + (bid * ULFS_BLOCK_SIZE);
}

static inline ulfs_superblock_t *get_superblock(void)
{
    return g_sb[g_current_store];
}

static inline ulfs_mapblock_t *get_mapblock(uint32_t map_bid)
{
    return (ulfs_mapblock_t *)bid_to_ptr(map_bid);
}

static inline ulfs_inode_block_t *get_inode_block(uint32_t iblk_bid)
{
    return (ulfs_inode_block_t *)bid_to_ptr(iblk_bid);
}

static inline ulfs_dirent_t *get_dir_block(uint32_t data_bid)
{
    return (ulfs_dirent_t *)bid_to_ptr(data_bid);
}

static inline ulfs_bid_block_t *get_bid_block(uint32_t bb_bid)
{
    return (ulfs_bid_block_t *)bid_to_ptr(bb_bid);
}

/* ------------------------------------------------------------------ */
/*  String helpers (avoid standard library)                         */
/* ------------------------------------------------------------------ */

static uint32_t ulfs_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int ulfs_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void ulfs_strncpy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void ulfs_strcat(char *dst, const char *src, uint32_t max)
{
    uint32_t dlen = ulfs_strlen(dst);
    uint32_t i = 0;
    while (dlen + i < max - 1 && src[i]) {
        dst[dlen + i] = src[i]; i++;
    }
    dst[dlen + i] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Bitmap helpers (map block)                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Test if a block is allocated in the map block.
 *
 * @complexity O(1)
 */
static bool bitmap_test(const ulfs_mapblock_t *mb, uint32_t bid)
{
    /* Each byte holds 8 bits; LSB = lower bid */
    return (mb->bitmap[bid / 8] & (1U << (bid % 8))) != 0;
}

/**
 * @brief Mark a block as allocated in the map block.
 *
 * @complexity O(1)
 */
static void bitmap_set(ulfs_mapblock_t *mb, uint32_t bid)
{
    mb->bitmap[bid / 8] |= (uint8_t)(1U << (bid % 8));
}

/**
 * @brief Mark a block as free in the map block.
 *
 * @complexity O(1)
 */
static void bitmap_clear(ulfs_mapblock_t *mb, uint32_t bid)
{
    mb->bitmap[bid / 8] &= (uint8_t)~(1U << (bid % 8));
}

/**
 * @brief Allocate a free block from the bitmap.
 *
 * Linear scan of the bitmap; uses 8-bit step with bit-level check.
 * Pre-marks the block as allocated.
 *
 * @param[out] out_bid  Receives the allocated block id.
 * @return STATUS_OK or STATUS_ERROR_OUT_OF_MEMORY (disk full).
 *
 * @complexity O(N/8) where N = ULFS_TOTAL_BLOCKS
 */
static Status bid_alloc(uint32_t *out_bid)
{
    ulfs_mapblock_t *mb = get_mapblock(ULFS_BID_MAPBLOCK0);

    for (uint32_t bid = 0; bid < ULFS_TOTAL_BLOCKS; bid++) {
        if (!bitmap_test(mb, bid)) {
            bitmap_set(mb, bid);
            memset(bid_to_ptr(bid), 0, ULFS_BLOCK_SIZE);
            *out_bid = bid;
            return STATUS_OK;
        }
    }
    return STATUS_ERROR_OUT_OF_MEMORY;
}

/**
 * @brief Free a previously allocated block.
 *
 * @complexity O(1)
 */
static void bid_free(uint32_t bid)
{
    if (bid < ULFS_TOTAL_BLOCKS) {
        ulfs_mapblock_t *mb = get_mapblock(ULFS_BID_MAPBLOCK0);
        bitmap_clear(mb, bid);
        /* Zero the block content for cleanliness */
        memset(bid_to_ptr(bid), 0, ULFS_BLOCK_SIZE);
    }
}

/* ------------------------------------------------------------------ */
/*  Inode allocation                                                 */
/* ------------------------------------------------------------------ */

#define MARK_DIRTY() do { g_is_dirty[g_current_store] = true; } while(0)

/**
 * @brief Allocate a new inode.
 *
 * Scans inode blocks (linked list) for a free slot.
 * Allocates a new inode block if all existing slots are full.
 *
 * @param[out] out_ino  Receives the new inode number.
 * @return STATUS_OK or error code.
 *
 * @complexity O(k × 170) where k = number of inode blocks
 */
static Status inode_alloc(uint32_t *out_ino)
{
    uint32_t iblk_bid = ULFS_BID_INODE0;

    while (iblk_bid != 0) {
        ulfs_inode_block_t *ib = get_inode_block(iblk_bid);

        /* Scan for a free inode in this block */
        for (uint32_t i = 0; i < ULFS_INODES_PER_BLOCK; i++) {
            if (ib->inodes[i].type == ULFS_TYPE_FREE) {
                *out_ino = ib->ino_start + i;
                ib->n_used++;
                /* Zero the inode content (type still FREE; caller sets type) */
                memset(&ib->inodes[i], 0, sizeof(ulfs_inode_t));
                return STATUS_OK;
            }
        }

        if (ib->bid_next != 0) {
            iblk_bid = ib->bid_next;
        } else {
            /* All existing inode blocks full — allocate a new one */
            uint32_t new_bid;
            Status s = bid_alloc(&new_bid);
            if (s != STATUS_OK) return s;

            ulfs_inode_block_t *new_ib = get_inode_block(new_bid);
            memset(new_ib, 0, ULFS_BLOCK_SIZE);
            new_ib->ino_start = ib->ino_start + ULFS_INODES_PER_BLOCK;
            new_ib->bid_prev  = iblk_bid;
            new_ib->bid_next  = 0;

            /* Link new block into the chain */
            ib->bid_next = new_bid;

            /* Allocate first inode in new block */
            new_ib->n_used = 1;
            *out_ino = new_ib->ino_start;
            return STATUS_OK;
        }
    }
    return STATUS_ERROR_OUT_OF_MEMORY;
}

/**
 * @brief Lookup an inode by inode number.
 *
 * Walks the inode block chain until the block containing ino is found.
 *
 * @param[in]  ino       Inode number to look up.
 * @param[out] out_inode Pointer to the ulfs_inode_t in the backing store.
 *
 * @complexity O(k) where k = number of inode blocks
 */
static Status inode_get(uint32_t ino, ulfs_inode_t **out_inode)
{
    uint32_t iblk_bid = ULFS_BID_INODE0;

    while (iblk_bid != 0) {
        ulfs_inode_block_t *ib = get_inode_block(iblk_bid);

        if (ino >= ib->ino_start &&
            ino < ib->ino_start + ULFS_INODES_PER_BLOCK) {
            *out_inode = &ib->inodes[ino - ib->ino_start];
            return STATUS_OK;
        }
        iblk_bid = ib->bid_next;
    }
    return STATUS_ERROR_INVALID_ARGUMENT;
}

/**
 * @brief Free an inode (mark type = FREE, decrement n_used).
 *
 * @complexity O(k)
 */
static Status inode_free(uint32_t ino)
{
    uint32_t iblk_bid = ULFS_BID_INODE0;

    while (iblk_bid != 0) {
        ulfs_inode_block_t *ib = get_inode_block(iblk_bid);

        if (ino >= ib->ino_start &&
            ino < ib->ino_start + ULFS_INODES_PER_BLOCK) {
            uint32_t idx = ino - ib->ino_start;
            memset(&ib->inodes[idx], 0, sizeof(ulfs_inode_t));
            if (ib->n_used > 0) ib->n_used--;
            return STATUS_OK;
        }
        iblk_bid = ib->bid_next;
    }
    return STATUS_ERROR_INVALID_ARGUMENT;
}

/* ------------------------------------------------------------------ */
/*  Block indexing for file offsets                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Get the storage block (bid) for a given byte offset in a file.
 *
 * - Block 0 offset (0..4095)   → inode.bid_head (direct)
 * - Block n ≥ 1 offset         → inode.bid_block[n-1] (indirect)
 *
 * If @p allocate is true, missing blocks are allocated.
 *
 * @param[in]  inode     The file inode.
 * @param[in]  offset    Byte offset within the file.
 * @param[in]  allocate  If true, allocate missing blocks.
 * @param[out] out_bid   The data block bid for this offset.
 *
 * @complexity O(1) — direct access for block 0; O(1) for indirect
 *             (single bid_block read). Writing to block_index ≥ 1023
 *             is not supported (file size limit = 1024 × 4 KB = 4 MB).
 */
static Status file_bid_for_offset(ulfs_inode_t *inode,
                                   uint32_t offset,
                                   bool allocate,
                                   uint32_t *out_bid)
{
    uint32_t block_index = offset / ULFS_BLOCK_SIZE;

    if (block_index == 0) {
        /* Direct block */
        if (inode->bid_head == 0) {
            if (!allocate) return STATUS_ERROR_INVALID_ARGUMENT;
            uint32_t new_bid;
            Status s = bid_alloc(&new_bid);
            if (s != STATUS_OK) return s;
            inode->bid_head = new_bid;
            memset(bid_to_ptr(inode->bid_head), 0, ULFS_BLOCK_SIZE);
        }
        *out_bid = inode->bid_head;
        return STATUS_OK;
    }

    /* Indirect block: index within the bid_block (1-based) */
    uint32_t idx = block_index - 1;

    if (idx >= ULFS_BIDS_PER_BIDBLOCK) {
        /* File would exceed 4 MB — not supported */
        return STATUS_ERROR_NOT_SUPPORTED;
    }

    /* Ensure the bid_block exists */
    if (inode->bid_block == 0) {
        if (!allocate) return STATUS_ERROR_INVALID_ARGUMENT;
        uint32_t new_bb;
        Status s = bid_alloc(&new_bb);
        if (s != STATUS_OK) return s;
        inode->bid_block = new_bb;
        memset(bid_to_ptr(inode->bid_block), 0, ULFS_BLOCK_SIZE);
    }

    ulfs_bid_block_t *bb = get_bid_block(inode->bid_block);

    if (bb->bids[idx] == 0) {
        if (!allocate) return STATUS_ERROR_INVALID_ARGUMENT;
        uint32_t new_data;
        Status s = bid_alloc(&new_data);
        if (s != STATUS_OK) return s;
        bb->bids[idx] = new_data;
        memset(bid_to_ptr(bb->bids[idx]), 0, ULFS_BLOCK_SIZE);
    }

    *out_bid = bb->bids[idx];
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Directory helpers                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Look up a name in a directory, return inode number.
 *
 * Scans the directory's data block (bid_head) for a matching entry.
 * Only supports directories that fit in a single 4 KB block (128 entries).
 *
 * @param[in]  dir_ino    Inode number of the directory.
 * @param[in]  name       Entry name to find.
 * @param[out] out_ino    Set to found inode number on success.
 *
 * @return STATUS_OK if found, STATUS_ERROR_INVALID_ARGUMENT if not found.
 *
 * @complexity O(128) — linear scan of fixed-size directory block
 */
static Status dirent_lookup(uint32_t dir_ino, const char *name,
                             uint32_t *out_ino)
{
    ulfs_inode_t *dir_inode;
    Status s = inode_get(dir_ino, &dir_inode);
    if (s != STATUS_OK) return s;

    if (dir_inode->type != ULFS_TYPE_DIR) return STATUS_ERROR_INVALID_ARGUMENT;
    if (dir_inode->bid_head == 0)          return STATUS_ERROR_INVALID_ARGUMENT;

    ulfs_dirent_t *dirblock = get_dir_block(dir_inode->bid_head);

    for (uint32_t i = 0; i < ULFS_DIRENTS_PER_BLOCK; i++) {
        if (dirblock[i].ino != 0 &&
            ulfs_strcmp(dirblock[i].name, name) == 0) {
            *out_ino = dirblock[i].ino;
            return STATUS_OK;
        }
    }
    return STATUS_ERROR_INVALID_ARGUMENT;
}

/**
 * @brief Add a directory entry in a directory.
 *
 * Finds the first free (ino==0) slot in the directory's data block.
 *
 * @complexity O(128)
 */
static Status dirent_add(uint32_t dir_ino, const char *name, uint32_t entry_ino)
{
    ulfs_inode_t *dir_inode;
    Status s = inode_get(dir_ino, &dir_inode);
    if (s != STATUS_OK) return s;

    /* Allocate a data block for the directory if it has none */
    if (dir_inode->bid_head == 0) {
        uint32_t new_bid;
        s = bid_alloc(&new_bid);
        if (s != STATUS_OK) return s;
        dir_inode->bid_head = new_bid;
        memset(bid_to_ptr(dir_inode->bid_head), 0, ULFS_BLOCK_SIZE);
    }

    ulfs_dirent_t *dirblock = get_dir_block(dir_inode->bid_head);

    for (uint32_t i = 0; i < ULFS_DIRENTS_PER_BLOCK; i++) {
        if (dirblock[i].ino == 0) {
            dirblock[i].ino = entry_ino;
            ulfs_strncpy(dirblock[i].name, name, ULFS_NAME_MAX);
            dir_inode->size += sizeof(ulfs_dirent_t);
            return STATUS_OK;
        }
    }
    return STATUS_ERROR_OUT_OF_MEMORY; /* directory is full */
}

/**
 * @brief Remove a directory entry by name.
 *
 * @complexity O(128)
 */
static Status dirent_remove(uint32_t dir_ino, const char *name)
{
    ulfs_inode_t *dir_inode;
    Status s = inode_get(dir_ino, &dir_inode);
    if (s != STATUS_OK) return s;

    if (dir_inode->bid_head == 0) return STATUS_ERROR_INVALID_ARGUMENT;

    ulfs_dirent_t *dirblock = get_dir_block(dir_inode->bid_head);

    for (uint32_t i = 0; i < ULFS_DIRENTS_PER_BLOCK; i++) {
        if (dirblock[i].ino != 0 &&
            ulfs_strcmp(dirblock[i].name, name) == 0) {
            dirblock[i].ino = 0;
            memset(dirblock[i].name, 0, ULFS_NAME_MAX);
            if (dir_inode->size >= sizeof(ulfs_dirent_t))
                dir_inode->size -= sizeof(ulfs_dirent_t);
            return STATUS_OK;
        }
    }
    return STATUS_ERROR_INVALID_ARGUMENT;
}

/* ------------------------------------------------------------------ */
/*  Path resolution                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Tokenise a ULFS path into components.
 *
 * Splits @p path on '/' in-place, writing pointers into @p parts.
 * Handles both absolute ("/foo/bar") and relative ("foo/bar") paths.
 *
 * @param[in]  path      Path string (will be modified in place).
 * @param[out] parts     Array of component string pointers.
 * @param[in]  max_parts Maximum number of components.
 * @param[out] n_parts   Number of parts filled.
 * @param[out] absolute  Set true if path started with '/'.
 */
static void path_split(char *path, char **parts, uint32_t max_parts,
                       uint32_t *n_parts)
{
    *n_parts  = 0;
    char *p = path;
    while (*p == '/') p++;  /* skip leading slashes */

    while (*p && *n_parts < max_parts) {
        parts[(*n_parts)++] = p;
        while (*p && *p != '/') p++;
        if (*p == '/') { *p = '\0'; p++; }
        while (*p == '/') p++;
    }
}

/**
 * @brief Resolve a path to an inode number.
 *
 * Handles absolute paths ("/...") and relative paths relative to cwd.
 * Also handles "." (stay) and ".." (parent) components.
 *
 * @param[in]  path      Path to resolve (copied internally, not modified).
 * @param[out] out_ino   Inode number for the resolved entry.
 * @param[out] out_parent_ino  Inode of the parent directory (optional).
 * @param[out] out_name  Basename of the last component (optional).
 *
 * @complexity O(depth × entries_per_dir)
 */
static Status path_resolve(const char *path, uint32_t *out_ino,
                            uint32_t *out_parent_ino, const char **out_name)
{
    if (!path || path[0] == '\0') return STATUS_ERROR_INVALID_ARGUMENT;

    char buf[ULFS_PATH_MAX];
    /* Build absolute path */
    if (path[0] == '/') {
        ulfs_strncpy(buf, path, ULFS_PATH_MAX);
    } else {
        ulfs_strncpy(buf, g_cwd_path, ULFS_PATH_MAX);
        if (buf[ulfs_strlen(buf) - 1] != '/') {
            ulfs_strcat(buf, "/", ULFS_PATH_MAX);
        }
        ulfs_strcat(buf, path, ULFS_PATH_MAX);
    }

    g_current_store = ULFS_STORE_VOL;
    char *target_path = buf;

    /* Detect mount point /storage */
    if (buf[0] == '/' && buf[1] == 's' && buf[2] == 't' && buf[3] == 'o' && 
        buf[4] == 'r' && buf[5] == 'a' && buf[6] == 'g' && buf[7] == 'e') 
    {
        if (buf[8] == '/' || buf[8] == '\0') {
            g_current_store = ULFS_STORE_NVRAM;
            target_path = &buf[8];
            if (*target_path == '\0') {
                /* Safe because buf is local and has capacity */
                buf[8] = '/'; buf[9] = '\0';
                target_path = &buf[8];
            }
        }
    }

    char *parts[ULFS_MAX_DEPTH];
    uint32_t n_parts;
    path_split(target_path, parts, ULFS_MAX_DEPTH, &n_parts);

    /* Start from root of the resolved store */
    uint32_t cur_ino    = 0U;
    uint32_t parent_ino = cur_ino;

    if (n_parts == 0) {
        *out_ino = cur_ino;
        if (out_parent_ino) *out_parent_ino = cur_ino;
        if (out_name)       *out_name = "/";
        return STATUS_OK;
    }

    for (uint32_t i = 0; i < n_parts; i++) {
        const char *component = parts[i];
        parent_ino = cur_ino;

        if (ulfs_strcmp(component, ".") == 0) continue;
        else if (ulfs_strcmp(component, "..") == 0) {
            if (cur_ino == 0) continue;
            uint32_t pp;
            if (dirent_lookup(cur_ino, "..", &pp) == STATUS_OK) cur_ino = pp;
            else cur_ino = 0;
            parent_ino = cur_ino;
        } else {
            uint32_t child_ino;
            if (dirent_lookup(cur_ino, component, &child_ino) != STATUS_OK) {
                if (i == n_parts - 1) {
                    *out_ino = 0xFFFFFFFFU;
                    if (out_parent_ino) *out_parent_ino = cur_ino;
                    if (out_name)       *out_name = parts[i];
                    return STATUS_ERROR_INVALID_ARGUMENT;
                }
                return STATUS_ERROR_INVALID_ARGUMENT;
            }
            cur_ino = child_ino;
        }
        if (out_name) *out_name = parts[i];
    }

    *out_ino = cur_ino;
    if (out_parent_ino) *out_parent_ino = parent_ino;
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API — Initialization                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Format and initialize the ULFS backing store.
 *
 * Steps:
 *  1. Allocate 2 MB from KMEM heap (bump allocator, 64-byte aligned).
 *  2. Zero the entire region.
 *  3. Write superblock (bid 0).
 *  4. Mark bits 0..3 in the map block (bids 0-3 pre-allocated).
 *  5. Format the root inode block (bid 2) — allocate inode 0 as root dir.
 *  6. Allocate bid 3 as root dir data block.
 *  7. Add "." and ".." entries in root dir pointing to inode 0.
 *
 * @complexity O(ULFS_STORE_SIZE / 8) for the zero-pass.
 */
static void ULFS_FormatStore(uint32_t store_idx)
{
    g_current_store = store_idx;
    /* Initialize memory to 0xFF (match erased flash state to avoid boot penalty) */
    memset(g_fs_base[store_idx], 0xFF, ULFS_STORE_SIZE);

    /* Explicitly zero only the pre-allocated structures */
    memset(bid_to_ptr(ULFS_BID_SUPERBLOCK), 0, ULFS_BLOCK_SIZE);
    memset(bid_to_ptr(ULFS_BID_MAPBLOCK0), 0, ULFS_BLOCK_SIZE);
    memset(bid_to_ptr(ULFS_BID_INODE0), 0, ULFS_BLOCK_SIZE);
    memset(bid_to_ptr(ULFS_BID_ROOT_DIR), 0, ULFS_BLOCK_SIZE);

    ulfs_superblock_t *sb = get_superblock();
    sb->magic        = ULFS_MAGIC;
    sb->total_blocks = ULFS_TOTAL_BLOCKS;
    sb->block_size   = ULFS_BLOCK_SIZE;
    sb->inode_count  = 0;
    g_sb[store_idx]  = sb;

    ulfs_mapblock_t *mb = get_mapblock(ULFS_BID_MAPBLOCK0);
    bitmap_set(mb, 0);  
    bitmap_set(mb, 1);  
    bitmap_set(mb, 2);  
    bitmap_set(mb, 3);  

    ulfs_inode_block_t *ib0 = get_inode_block(ULFS_BID_INODE0);
    ib0->n_used    = (store_idx == ULFS_STORE_VOL) ? 2 : 1;           
    ib0->ino_start = 0;
    ib0->bid_prev  = 0;
    ib0->bid_next  = 0;

    ulfs_inode_t *root_inode = &ib0->inodes[0];
    root_inode->type     = ULFS_TYPE_DIR;
    root_inode->size     = 0;
    root_inode->bid_head = ULFS_BID_ROOT_DIR;   
    root_inode->bid_block = 0;

    if (store_idx == ULFS_STORE_VOL) {
        /* Add a dummy inode for /storage so it shows up in Readdir */
        ulfs_inode_t *storage_inode = &ib0->inodes[1];
        storage_inode->type     = ULFS_TYPE_DIR;
        storage_inode->size     = 0;
        storage_inode->bid_head = 0; /* empty, but path_resolve redirects anyway */   
        storage_inode->bid_block = 0;
    }

    ulfs_dirent_t *root_dir = get_dir_block(ULFS_BID_ROOT_DIR);
    root_dir[0].ino = 0;
    ulfs_strncpy(root_dir[0].name, ".", ULFS_NAME_MAX);
    root_dir[1].ino = 0;
    ulfs_strncpy(root_dir[1].name, "..", ULFS_NAME_MAX);
    
    if (store_idx == ULFS_STORE_VOL) {
        /* Add virtual /storage mount point entry so it appears in ls / */
        root_dir[2].ino = 1;
        ulfs_strncpy(root_dir[2].name, "storage", ULFS_NAME_MAX);
        root_inode->size = 3 * sizeof(ulfs_dirent_t);
        sb->inode_count = 2;
    } else {
        root_inode->size = 2 * sizeof(ulfs_dirent_t);
        sb->inode_count = 1;
    }
}

Status ULFS_Init(void)
{
    if (g_initialized) return STATUS_OK;

    g_fs_base[ULFS_STORE_VOL] = (uint8_t *)KMEM_Alloc(ULFS_STORE_SIZE, 64);
    g_fs_base[ULFS_STORE_NVRAM] = (uint8_t *)KMEM_Alloc(ULFS_STORE_SIZE, 64);
    if (!g_fs_base[0] || !g_fs_base[1]) return STATUS_ERROR_OUT_OF_MEMORY;

    ULFS_FormatStore(ULFS_STORE_VOL);

    /* Offset 0x40000 avoids "MNOS" boot sector in Flash */
    STORAGE_Read(0x40000, g_fs_base[ULFS_STORE_NVRAM], ULFS_STORE_SIZE);
    
    g_current_store = ULFS_STORE_NVRAM;
    g_sb[ULFS_STORE_NVRAM] = get_superblock();
    
    if (g_sb[ULFS_STORE_NVRAM]->magic != ULFS_MAGIC) {
        HAL_UART_PutString("[ULFS] Formatting NVRAM Shadow Buffer...\n");
        ULFS_FormatStore(ULFS_STORE_NVRAM);
        g_is_dirty[ULFS_STORE_NVRAM] = true;
        ULFS_Sync();
    } else {
        HAL_UART_PutString("[ULFS] NVRAM Store Recovered!\n");
    }

    memset(g_fdt, 0, sizeof(g_fdt));
    g_cwd_store = ULFS_STORE_VOL;
    g_cwd_ino = 0;
    ulfs_strncpy(g_cwd_path, "/", ULFS_PATH_MAX);

    g_initialized = true;
    HAL_UART_PutString("[ULFS] Initialized Dual-Store FS\n");
    return STATUS_OK;
}

void ULFS_Sync(void)
{
    if (!g_is_dirty[ULFS_STORE_NVRAM]) return;

    /* Flash writes are 256KB block-aligned sectors */
    for (uint32_t i = 0; i < (ULFS_STORE_SIZE / 262144); i++) {
        uint32_t flash_offset = 0x40000 + (i * 262144);
        STORAGE_EraseSector(flash_offset);
        STORAGE_Write(flash_offset, g_fs_base[ULFS_STORE_NVRAM] + (i * 262144), 262144);
    }
    g_is_dirty[ULFS_STORE_NVRAM] = false;
}

/* ------------------------------------------------------------------ */
/*  Public API — File Creation / Open / Close                        */
/* ------------------------------------------------------------------ */

Status ULFS_Create(const char *path)
{
    if (!g_initialized) return STATUS_ERROR_NOT_INITIALIZED;
    if (!path)          return STATUS_ERROR_INVALID_ARGUMENT;

    /* Resolve path to find parent directory */
    uint32_t existing_ino, parent_ino;
    const char *basename;
    Status s = path_resolve(path, &existing_ino, &parent_ino, &basename);

    if (s == STATUS_OK && existing_ino != 0xFFFFFFFFU) {
        /* Already exists */
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    /* basename is the new file name, parent_ino is the containing dir */
    uint32_t new_ino;
    s = inode_alloc(&new_ino);
    if (s != STATUS_OK) return s;

    ulfs_inode_t *inode;
    s = inode_get(new_ino, &inode);
    if (s != STATUS_OK) return s;

    inode->type      = ULFS_TYPE_FILE;
    inode->size      = 0;
    inode->bid_head  = 0;
    inode->bid_block = 0;

    /* Add entry in parent directory */
    s = dirent_add(parent_ino, basename, new_ino);
    if (s != STATUS_OK) {
        inode_free(new_ino);
        return s;
    }

    g_sb[g_current_store]->inode_count++;
    MARK_DIRTY();
    return STATUS_OK;
}

Status ULFS_Open(const char *path, uint8_t flags, int *fd_out)
{
    if (!g_initialized) return STATUS_ERROR_NOT_INITIALIZED;
    if (!path || !fd_out) return STATUS_ERROR_INVALID_ARGUMENT;

    /* Handle O_CREAT: create if not exists */
    uint32_t ino, parent_ino;
    const char *basename;
    Status s = path_resolve(path, &ino, &parent_ino, &basename);

    if (s != STATUS_OK || ino == 0xFFFFFFFFU) {
        /* Not found */
        if (flags & ULFS_O_CREAT) {
            s = ULFS_Create(path);
            if (s != STATUS_OK) return s;
            s = path_resolve(path, &ino, NULL, NULL);
            if (s != STATUS_OK) return s;
        } else {
            return STATUS_ERROR_INVALID_ARGUMENT;
        }
    }

    /* Find a free fd slot */
    int fd = -1;
    for (int i = 0; i < (int)ULFS_MAX_OPEN_FILES; i++) {
        if (g_fdt[i].ino == 0) { fd = i; break; }
    }
    if (fd < 0) return STATUS_ERROR_OUT_OF_MEMORY;

    /* Get inode and verify type */
    ulfs_inode_t *inode;
    s = inode_get(ino, &inode);
    if (s != STATUS_OK) return s;

    if (inode->type != ULFS_TYPE_FILE) return STATUS_ERROR_INVALID_ARGUMENT;

    g_fdt[fd].store_idx = g_current_store;
    g_fdt[fd].ino   = ino;
    g_fdt[fd].pos   = (flags & ULFS_O_TRUNC) ? 0U : 0U;
    g_fdt[fd].flags = flags;

    /* Truncate if requested */
    if (flags & ULFS_O_TRUNC) {
        /* Free data blocks */
        if (inode->bid_head != 0) {
            bid_free(inode->bid_head);
            inode->bid_head = 0;
        }
        if (inode->bid_block != 0) {
            ulfs_bid_block_t *bb = get_bid_block(inode->bid_block);
            for (uint32_t i = 0; i < ULFS_BIDS_PER_BIDBLOCK; i++) {
                if (bb->bids[i]) { bid_free(bb->bids[i]); bb->bids[i] = 0; }
            }
            bid_free(inode->bid_block);
            inode->bid_block = 0;
        }
        inode->size = 0;
        MARK_DIRTY();
    }

    *fd_out = fd;
    return STATUS_OK;
}

Status ULFS_Close(int fd)
{
    if (fd < 0 || fd >= (int)ULFS_MAX_OPEN_FILES) return STATUS_ERROR_INVALID_ARGUMENT;
    if (g_fdt[fd].ino == 0) return STATUS_ERROR_INVALID_ARGUMENT;
    g_fdt[fd].ino   = 0;
    g_fdt[fd].pos   = 0;
    g_fdt[fd].flags = 0;
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API — Read / Write                                        */
/* ------------------------------------------------------------------ */

Status ULFS_Read(int fd, void *buf, uint32_t count, uint32_t *nread)
{
    if (!g_initialized) return STATUS_ERROR_NOT_INITIALIZED;
    if (fd < 0 || fd >= (int)ULFS_MAX_OPEN_FILES) return STATUS_ERROR_INVALID_ARGUMENT;
    if (!buf || !nread) return STATUS_ERROR_INVALID_ARGUMENT;
    if (g_fdt[fd].ino == 0) return STATUS_ERROR_INVALID_ARGUMENT;

    g_current_store = g_fdt[fd].store_idx;

    ulfs_inode_t *inode;
    Status s = inode_get(g_fdt[fd].ino, &inode);
    if (s != STATUS_OK) return s;

    uint32_t pos   = g_fdt[fd].pos;
    uint32_t avail = (pos < inode->size) ? (inode->size - pos) : 0U;
    uint32_t to_read = (count < avail) ? count : avail;

    *nread = 0;
    uint8_t *dst = (uint8_t *)buf;

    while (*nread < to_read) {
        uint32_t blk_bid;
        s = file_bid_for_offset(inode, pos, false, &blk_bid);
        if (s != STATUS_OK) break;

        uint8_t *blk_ptr = bid_to_ptr(blk_bid);
        uint32_t blk_off = pos % ULFS_BLOCK_SIZE;
        uint32_t chunk   = ULFS_BLOCK_SIZE - blk_off;
        if (chunk > to_read - *nread) chunk = to_read - *nread;

        memcpy(dst + *nread, blk_ptr + blk_off, chunk);
        *nread += chunk;
        pos    += chunk;
    }

    g_fdt[fd].pos = pos;
    return STATUS_OK;
}

Status ULFS_Write(int fd, const void *buf, uint32_t count, uint32_t *nwritten)
{
    if (!g_initialized) return STATUS_ERROR_NOT_INITIALIZED;
    if (fd < 0 || fd >= (int)ULFS_MAX_OPEN_FILES) return STATUS_ERROR_INVALID_ARGUMENT;
    if (!buf || !nwritten) return STATUS_ERROR_INVALID_ARGUMENT;
    if (g_fdt[fd].ino == 0) return STATUS_ERROR_INVALID_ARGUMENT;

    g_current_store = g_fdt[fd].store_idx;

    ulfs_inode_t *inode;
    Status s = inode_get(g_fdt[fd].ino, &inode);
    if (s != STATUS_OK) return s;

    uint32_t pos = g_fdt[fd].pos;
    *nwritten = 0;
    const uint8_t *src = (const uint8_t *)buf;

    while (*nwritten < count) {
        uint32_t blk_bid;
        s = file_bid_for_offset(inode, pos, true, &blk_bid);
        if (s != STATUS_OK) break;

        uint8_t *blk_ptr = bid_to_ptr(blk_bid);
        uint32_t blk_off = pos % ULFS_BLOCK_SIZE;
        uint32_t chunk   = ULFS_BLOCK_SIZE - blk_off;
        if (chunk > count - *nwritten) chunk = count - *nwritten;

        memcpy(blk_ptr + blk_off, src + *nwritten, chunk);
        *nwritten += chunk;
        pos       += chunk;

        if (pos > inode->size) inode->size = pos;
    }

    if (*nwritten > 0) MARK_DIRTY();
    g_fdt[fd].pos = pos;
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API — Directory Operations                                */
/* ------------------------------------------------------------------ */

Status ULFS_Mkdir(const char *path)
{
    if (!g_initialized) return STATUS_ERROR_NOT_INITIALIZED;
    if (!path)          return STATUS_ERROR_INVALID_ARGUMENT;

    uint32_t existing_ino, parent_ino;
    const char *basename;
    Status s = path_resolve(path, &existing_ino, &parent_ino, &basename);

    if (s == STATUS_OK && existing_ino != 0xFFFFFFFFU) {
        return STATUS_ERROR_INVALID_ARGUMENT; /* Already exists */
    }

    /* Allocate new inode for the directory */
    uint32_t new_ino;
    s = inode_alloc(&new_ino);
    if (s != STATUS_OK) return s;

    ulfs_inode_t *inode;
    s = inode_get(new_ino, &inode);
    if (s != STATUS_OK) { inode_free(new_ino); return s; }

    inode->type      = ULFS_TYPE_DIR;
    inode->size      = 0;
    inode->bid_head  = 0;
    inode->bid_block = 0;

    /* Initialize directory with "." and ".." */
    uint32_t data_bid;
    s = bid_alloc(&data_bid);
    if (s != STATUS_OK) { inode_free(new_ino); return s; }

    memset(bid_to_ptr(data_bid), 0, ULFS_BLOCK_SIZE);
    inode->bid_head = data_bid;

    ulfs_dirent_t *dir_data = get_dir_block(data_bid);
    dir_data[0].ino = new_ino;
    ulfs_strncpy(dir_data[0].name, ".", ULFS_NAME_MAX);
    dir_data[1].ino = parent_ino;
    ulfs_strncpy(dir_data[1].name, "..", ULFS_NAME_MAX);
    inode->size = 2 * sizeof(ulfs_dirent_t);

    /* Add entry in parent directory */
    s = dirent_add(parent_ino, basename, new_ino);
    if (s != STATUS_OK) {
        inode_free(new_ino);
        return s;
    }

    g_sb[g_current_store]->inode_count++;
    MARK_DIRTY();
    return STATUS_OK;
}

Status ULFS_Readdir(const char *path, ulfs_readdir_cb_t callback, void *user_data)
{
    if (!g_initialized || !callback) return STATUS_ERROR_NOT_INITIALIZED;

    uint32_t dir_ino;
    Status s = path_resolve(path, &dir_ino, NULL, NULL);
    if (s != STATUS_OK || dir_ino == 0xFFFFFFFFU) return STATUS_ERROR_INVALID_ARGUMENT;

    ulfs_inode_t *dir_inode;
    s = inode_get(dir_ino, &dir_inode);
    if (s != STATUS_OK) return s;

    if (dir_inode->type != ULFS_TYPE_DIR)  return STATUS_ERROR_INVALID_ARGUMENT;
    if (dir_inode->bid_head == 0)          return STATUS_OK; /* empty */

    ulfs_dirent_t *dir_data = get_dir_block(dir_inode->bid_head);

    for (uint32_t i = 0; i < ULFS_DIRENTS_PER_BLOCK; i++) {
        if (dir_data[i].ino == 0) continue;

        ulfs_inode_t *entry_inode;
        s = inode_get(dir_data[i].ino, &entry_inode);
        if (s != STATUS_OK) continue;

        ulfs_stat_t st;
        st.ino    = dir_data[i].ino;
        st.type   = entry_inode->type;
        st.size   = entry_inode->size;
        st.blocks = (entry_inode->size + ULFS_BLOCK_SIZE - 1) / ULFS_BLOCK_SIZE;

        callback(dir_data[i].name, &st, user_data);
    }

    return STATUS_OK;
}

Status ULFS_Stat(const char *path, ulfs_stat_t *st)
{
    if (!g_initialized) return STATUS_ERROR_NOT_INITIALIZED;
    if (!path || !st)   return STATUS_ERROR_INVALID_ARGUMENT;

    uint32_t ino;
    Status s = path_resolve(path, &ino, NULL, NULL);
    if (s != STATUS_OK || ino == 0xFFFFFFFFU) return STATUS_ERROR_INVALID_ARGUMENT;

    ulfs_inode_t *inode;
    s = inode_get(ino, &inode);
    if (s != STATUS_OK) return s;

    st->ino    = ino;
    st->type   = inode->type;
    st->size   = inode->size;
    st->blocks = (inode->size + ULFS_BLOCK_SIZE - 1) / ULFS_BLOCK_SIZE;

    return STATUS_OK;
}

Status ULFS_Unlink(const char *path)
{
    if (!g_initialized) return STATUS_ERROR_NOT_INITIALIZED;
    if (!path)          return STATUS_ERROR_INVALID_ARGUMENT;

    uint32_t ino, parent_ino;
    const char *basename;
    Status s = path_resolve(path, &ino, &parent_ino, &basename);
    if (s != STATUS_OK || ino == 0xFFFFFFFFU) return STATUS_ERROR_INVALID_ARGUMENT;

    ulfs_inode_t *inode;
    s = inode_get(ino, &inode);
    if (s != STATUS_OK) return s;

    /* For directories: check they are empty (only "." and "..") */
    if (inode->type == ULFS_TYPE_DIR) {
        if (inode->bid_head != 0) {
            ulfs_dirent_t *dir_data = get_dir_block(inode->bid_head);
            uint32_t real_entries = 0;
            for (uint32_t i = 0; i < ULFS_DIRENTS_PER_BLOCK; i++) {
                if (dir_data[i].ino == 0) continue;
                if (ulfs_strcmp(dir_data[i].name, ".") == 0) continue;
                if (ulfs_strcmp(dir_data[i].name, "..") == 0) continue;
                real_entries++;
            }
            if (real_entries > 0) return STATUS_ERROR_NOT_SUPPORTED; /* not empty */
        }
    }

    /* Free the inode's data blocks */
    if (inode->bid_head != 0) {
        bid_free(inode->bid_head);
        inode->bid_head = 0;
    }
    if (inode->bid_block != 0) {
        ulfs_bid_block_t *bb = get_bid_block(inode->bid_block);
        for (uint32_t i = 0; i < ULFS_BIDS_PER_BIDBLOCK; i++) {
            if (bb->bids[i]) { bid_free(bb->bids[i]); bb->bids[i] = 0; }
        }
        bid_free(inode->bid_block);
        inode->bid_block = 0;
    }

    /* Remove entry from parent directory */
    dirent_remove(parent_ino, basename);

    /* Free the inode */
    inode_free(ino);
    if (g_sb[g_current_store]->inode_count > 0) g_sb[g_current_store]->inode_count--;

    MARK_DIRTY();
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API — CWD and Navigation                                  */
/* ------------------------------------------------------------------ */

Status ULFS_ChDir(const char *path)
{
    if (!g_initialized) return STATUS_ERROR_NOT_INITIALIZED;
    if (!path)          return STATUS_ERROR_INVALID_ARGUMENT;

    uint32_t ino;
    Status s = path_resolve(path, &ino, NULL, NULL);
    if (s != STATUS_OK || ino == 0xFFFFFFFFU) return STATUS_ERROR_INVALID_ARGUMENT;

    ulfs_inode_t *inode;
    s = inode_get(ino, &inode);
    if (s != STATUS_OK) return s;

    if (inode->type != ULFS_TYPE_DIR) return STATUS_ERROR_INVALID_ARGUMENT;

    g_cwd_ino = ino;

    /* Rebuild CWD path string */
    if (path[0] == '/') {
        ulfs_strncpy(g_cwd_path, path, ULFS_PATH_MAX);
        /* Normalize trailing slash */
        uint32_t len = ulfs_strlen(g_cwd_path);
        if (len > 1 && g_cwd_path[len - 1] == '/') g_cwd_path[len - 1] = '\0';
    } else {
        /* Relative — append to current path */
        if (g_cwd_path[ulfs_strlen(g_cwd_path) - 1] != '/') {
            ulfs_strcat(g_cwd_path, "/", ULFS_PATH_MAX);
        }
        ulfs_strcat(g_cwd_path, path, ULFS_PATH_MAX);
        /* Trim trailing slash */
        uint32_t len = ulfs_strlen(g_cwd_path);
        if (len > 1 && g_cwd_path[len - 1] == '/') g_cwd_path[len - 1] = '\0';
    }

    return STATUS_OK;
}

Status ULFS_GetCwd(char *buf, uint32_t size)
{
    if (!buf || size == 0) return STATUS_ERROR_INVALID_ARGUMENT;
    ulfs_strncpy(buf, g_cwd_path, size);
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API — Statistics                                          */
/* ------------------------------------------------------------------ */

void ULFS_GetStats(uint32_t *total_blocks, uint32_t *used_blocks,
                   uint32_t *free_blocks)
{
    if (!g_initialized) {
        if (total_blocks) *total_blocks = 0;
        if (used_blocks)  *used_blocks  = 0;
        if (free_blocks)  *free_blocks  = 0;
        return;
    }

    ulfs_mapblock_t *mb = get_mapblock(ULFS_BID_MAPBLOCK0);
    uint32_t used = 0;

    for (uint32_t bid = 0; bid < ULFS_TOTAL_BLOCKS; bid++) {
        if (bitmap_test(mb, bid)) used++;
    }

    if (total_blocks) *total_blocks = ULFS_TOTAL_BLOCKS;
    if (used_blocks)  *used_blocks  = used;
    if (free_blocks)  *free_blocks  = ULFS_TOTAL_BLOCKS - used;
}
