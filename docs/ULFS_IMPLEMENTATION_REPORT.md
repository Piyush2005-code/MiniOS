# ULFS Implementation Report — MiniOS

> **MiniOS v0.2 — ARM64 Unikernel for ML Inference**
> Ultra Lightweight File System (ULFS) Technical Report
> Based on: *"ULFS: Ultra Lightweight File System"*, Appl. Sci. 2024, 14, 3342

---

## 1. Executive Summary

The ULFS file system was integrated into MiniOS as a minimal, in-memory, block-structured storage subsystem providing persistent-within-session file and directory management for the ARM64 unikernel. The implementation is derived from the academic paper *"ULFS: Ultra Lightweight File System"* and adapted for bare-metal ARM64 operation. 

In MiniOS v0.2, the ULFS architecture was extended into a **Dual-Store Architecture**, natively supporting both a fast volatile RAM-disk (`/`) and a fully persistent NVRAM mount (`/storage`) backed by QEMU's `pflash1` secondary flash bank. The backing stores occupy 2x 2 MB of kernel heap (4 MB total), supporting up to 512 x 4 KB blocks per store. It exposes a unified Unix-like path API deeply integrated into the interactive shell, and introduces new `cp` and `mv` maintenance commands for seamless artifact migration across storage tiers.

**Build result:** Zero errors, `36080` bytes `.text`, well within the 256 KB SRS limit (DC-003).

---

## 2. Design Rationale and Paper Adaptation

### 2.1 Why ULFS?

The original ULFS paper (Appl. Sci. 2024, 14, 3342) designs a file system specifically for **unikernel environments** where:
- There is no OS kernel VFS layer to interface with
- Memory-mapped I/O replaces block-device I/O for zero-copy access
- The single-application model eliminates the need for locking primitives
- Fixed-size structures minimize heap fragmentation

These properties align perfectly with MiniOS's SRS requirements:
- **DC-002**: No runtime dynamic allocation during inference - ULFS pre-allocates its entire 2 MB store at boot
- **SRS minimalism principle**: Fixed structures, bitmap allocation, no dynamic memory
- **FR-021 / FR-022**: UART I/O interfaces - the FS shell commands output to UART

### 2.2 Key Adaptations from Paper

| Paper Design | MiniOS Adaptation |
|---|---|
| mmap() over host file | KMEM_Alloc() over 2 MB kernel heap slab |
| Host OS provides locking | Lock-free (single-threaded inference model) |
| VFS integration layer | Direct function calls - no POSIX shim |
| Dynamic backing file growth | Fixed 512-block allocation at init time |
| Python binding layer | Shell command integration via CMD_Register() |

---

## 3. File System Architecture

### 3.1 Block Layout

The 2 MB backing store is a flat array of 512 contiguous 4 KB blocks indexed by block-id (bid):

```
bid 0 : Superblock        -- magic, capacity metadata
bid 1 : Map Block 0       -- 512-bit allocation bitmap
bid 2 : Inode Block 0     -- 170 inodes, linked-list head
bid 3 : Root Dir Data     -- 128 directory entries for "/"
bid 4+: Dynamically allocated data, directory, and bid blocks
```

All block accesses use direct pointer arithmetic O(1):
```c
static inline uint8_t *bid_to_ptr(uint32_t bid) {
    return g_fs_base + (bid * 4096);
}
```

### 3.2 Global State (static, allocated at compile time)

```c
static uint8_t           *g_fs_base;                 // 2 MB heap slab pointer
static ulfs_superblock_t *g_sb;                      // Cached superblock
static bool               g_initialized;             // Init guard
static ulfs_fd_t          g_fdt[8];                  // Open file table
static uint32_t           g_cwd_ino;                 // CWD inode number
static char               g_cwd_path[128];           // CWD path string
```

---

## 4. Data Structures

### 4.1 Superblock -- 4096 bytes (bid 0)

```c
typedef struct {
    uint32_t magic;          // 0x554C4653 ("ULFS")
    uint32_t total_blocks;   // 512
    uint32_t block_size;     // 4096
    uint32_t inode_count;    // allocated inode count
    uint8_t  _pad[4080];     // pads to exactly one block
} __attribute__((packed)) ulfs_superblock_t;
```

### 4.2 Map Block -- 4096 bytes (bid 1)

```c
typedef struct {
    uint8_t bitmap[4096];    // 1 bit per block, LSB-first, 0=free 1=used
} __attribute__((packed)) ulfs_mapblock_t;
```

Tracks all 512 blocks in 64 bytes (remaining 4032 bytes available for expansion).
Bits 0-3 are pre-marked at init for the fixed reserved blocks.

### 4.3 Inode -- 24 bytes (170 fit per 4 KB block)

```c
typedef struct {
    uint8_t  type;         // 0=FREE  1=FILE  2=DIR
    uint8_t  _pad0[3];     // alignment
    uint32_t size;         // file size in bytes
    uint32_t bid_head;     // first (direct) data block bid
    uint32_t bid_block;    // indirect index block bid (0=none)
    uint8_t  _reserved[8]; // future use
} __attribute__((packed)) ulfs_inode_t;   // exactly 24 bytes
```

The 24-byte size is deliberate: `4096 / 24 = 170.67`, so 170 inodes fit with 16 bytes
reserved for the inode block header. This maximizes inode density per block.

### 4.4 Inode Block -- 4096 bytes (bid 2+)

```c
typedef struct {
    uint32_t     n_used;        // in-use inode count in this block
    uint32_t     ino_start;     // first inode number (global numbering)
    uint32_t     bid_prev;      // previous inode block bid (0=head)
    uint32_t     bid_next;      // next inode block bid (0=tail)
    ulfs_inode_t inodes[170];   // 170 x 24 = 4080 bytes
} __attribute__((packed)) ulfs_inode_block_t;  // 16 + 4080 = 4096 bytes
```

Inode blocks form a **singly-linked list** allowing unlimited inode count (bounded by
total disk capacity). The `ino_start` field provides O(1) index-to-inode mapping within
a block: `inode = &block->inodes[ino - block->ino_start]`.

### 4.5 Directory Entry -- 32 bytes (128 fit per 4 KB block)

```c
typedef struct {
    uint32_t ino;        // inode number (0 = free/deleted slot)
    char     name[28];   // null-terminated name (max 27 chars)
} __attribute__((packed)) ulfs_dirent_t;  // 32 bytes
```

**Density:** 4096 / 32 = 128 entries per block.
**Slot reuse:** Any entry with `ino == 0` is available for reuse (tombstone-free design).
Every directory reserves slots 0/1 for `.` and `..`.

### 4.6 Bid Block -- 4096 bytes (indirect index)

```c
typedef struct {
    uint32_t bids[1023];  // bids for file blocks 1..1023 (0=unallocated)
    uint32_t _pad;
} __attribute__((packed)) ulfs_bid_block_t;  // 4096 bytes
```

Single-level indirection: block 0 is direct (`inode->bid_head`), blocks 1-1023 are
stored in `bids[0..1022]` of the bid block. Maximum file size = 1024 blocks x 4 KB = **4 MB**.

### 4.7 File Descriptor

```c
typedef struct {
    uint32_t ino;    // inode number (0 = slot available)
    uint32_t pos;    // current read/write byte offset
    uint8_t  flags;  // open flags bitmask
} ulfs_fd_t;
```

Table: `g_fdt[8]` — maximum 8 simultaneously open files. `ino == 0` signals a free slot
(root inode 0 is never opened as a file).

---

## 5. Initialization Sequence — O(STORE_SIZE)

```
ULFS_Init() called from kernel_main() after KMEM_Init()

Step 1: g_fs_base = KMEM_Alloc(2097152, 64)    -- 2 MB, 64-byte aligned
Step 2: memset(g_fs_base, 0, 2097152)           -- zero entire store
Step 3: Superblock (bid 0)
          magic=0x554C4653, total_blocks=512, block_size=4096
Step 4: Map block (bid 1): set bits 0,1,2,3     -- reserve 4 bootstrap blocks
Step 5: Inode block 0 (bid 2)
          n_used=1, ino_start=0
          inodes[0] = { type=DIR, size=0, bid_head=3, bid_block=0 }
Step 6: Root dir data (bid 3)
          entry[0] = { ino=0, name="."  }        -- "." = root itself
          entry[1] = { ino=0, name=".." }        -- ".." = root (no parent)
          root_inode->size = 2 * 32 = 64 bytes
Step 7: memset(g_fdt, 0)                        -- clear open file table
Step 8: g_cwd_ino = 0; g_cwd_path = "/"
Step 9: g_initialized = true
        HAL_UART_PutString("[ULFS] Initialized...")
```

---

## 6. Core Algorithms

### 6.1 Block Allocation — `bid_alloc()` — O(N/8)

```
Input:  out_bid (pointer)
Output: STATUS_OK + *out_bid, or STATUS_ERROR_OUT_OF_MEMORY

mb = get_mapblock(bid=1)
FOR bid = 0 TO 511:
  byte = mb->bitmap[bid / 8]
  IF (byte & (1 << (bid % 8))) == 0:
    SET bit: mb->bitmap[bid/8] |= (1 << (bid%8))
    *out_bid = bid
    RETURN STATUS_OK
RETURN STATUS_ERROR_OUT_OF_MEMORY
```

Worst case: 64 byte comparisons (512 bits / 8). In practice, the first free block
near the current high-water mark is found quickly.

### 6.2 Inode Allocation — `inode_alloc()` — O(k x 170)

```
Walk inode block linked list:
  FOR each inode block (bid_head=2, then bid_next chain):
    FOR i = 0 TO 169:
      IF inodes[i].type == ULFS_TYPE_FREE:
        *out_ino = ino_start + i
        ib->n_used++
        memset(&inodes[i], 0)
        RETURN STATUS_OK
    IF bid_next == 0:
      bid_alloc(&new_bid)            -- get a new block
      new_ib = get_inode_block(new_bid)
      new_ib->ino_start = ib->ino_start + 170
      link: ib->bid_next = new_bid
      RETURN first inode in new_ib
```

For the 512-block FS with typical usage, k=1 (single inode block holds 170 inodes).

### 6.3 Block Indexing — `file_bid_for_offset()` — O(1)

```
block_index = offset / 4096

IF block_index == 0:
  // Direct: inode->bid_head
  IF bid_head==0 AND allocate: bid_alloc(&tmp); bid_head=tmp
  *out_bid = bid_head

ELSE:
  idx = block_index - 1      // 0-based into bid_block.bids[]
  IF idx >= 1023: RETURN NOT_SUPPORTED  // > 4 MB file
  IF bid_block==0 AND allocate: bid_alloc(&tmp); bid_block=tmp
  bb = get_bid_block(bid_block)
  IF bb->bids[idx]==0 AND allocate: bid_alloc(&tmp); bb->bids[idx]=tmp
  *out_bid = bb->bids[idx]
```

At most 2 pointer dereferences -- truly O(1) for any file size up to 4 MB.

### 6.4 Directory Lookup — `dirent_lookup()` — O(128)

```
dir_inode = inode_get(dir_ino)
dirblock = get_dir_block(dir_inode->bid_head)
FOR i = 0 TO 127:
  IF dirblock[i].ino != 0 AND strcmp(dirblock[i].name, name) == 0:
    *out_ino = dirblock[i].ino
    RETURN STATUS_OK
RETURN STATUS_ERROR_INVALID_ARGUMENT
```

Linear scan over at most 128 fixed-size entries. Bounded and predictable.

### 6.5 Path Resolution — `path_resolve()` — O(depth x 128)

```
split path on '/' into parts[] (max 8)
cur_ino = (absolute path) ? 0 : g_cwd_ino
parent_ino = cur_ino

FOR each part in parts:
  IF part == ".":  continue
  IF part == "..": lookup ".." in cur_ino -> cur_ino  (or stay at root)
  ELSE:            dirent_lookup(cur_ino, part, &child_ino)
                   cur_ino = child_ino
  parent_ino = cur_ino (before last step)

*out_ino = cur_ino
```

Maximum depth 8, each step scans 128 entries: **max 1024 string comparisons** total.

### 6.6 Read / Write — O(count / 4096)

Both iterate block by block:
1. `file_bid_for_offset(inode, pos, allocate)` -> blk_bid (O(1))
2. Compute `blk_off = pos % 4096` and `chunk = min(4096 - blk_off, remaining)`
3. `memcpy` between user buffer and `bid_to_ptr(blk_bid) + blk_off`
4. Advance `pos` and repeat

Write auto-allocates blocks. Read stops at `inode->size`.

---

## 7. Shell Commands

All wired into the command framework via `FS_RegisterCommands()`:

| Command | Handler | ULFS API Used | Notes |
|---------|---------|--------------|-------|
| `ls [path]` | `cmd_ls` | `ULFS_Readdir` | `[d]` for dirs, `[f]` for files with size |
| `cd <path>` | `cmd_cd` | `ULFS_ChDir` | No args = go to `/` |
| `pwd` | `cmd_pwd` | `ULFS_GetCwd` | Always shows current path |
| `mkdir <name>` | `cmd_mkdir` | `ULFS_Mkdir` | Creates with `.` and `..` |
| `touch <name>` | `cmd_touch` | `ULFS_Create` | Silent if already exists |
| `cat <file>` | `cmd_cat` | `ULFS_Open/Read/Close` | Non-printable shown as `.` |
| `write <file> <text...>` | `cmd_write` | `ULFS_Open/Write/Close` | O_CREAT|O_TRUNC semantics |
| `rm <name>` | `cmd_rm` | `ULFS_Unlink` | Refuses non-empty dirs |
| `stat <name>` | `cmd_stat` | `ULFS_Stat` | Shows ino, type, size, blocks |
| `df` | `cmd_df` | `ULFS_GetStats` | Total/Used/Free + usage % |

---

## 8. Memory Breakdown

| Region | Size |
|--------|------|
| ULFS backing store (from KMEM) | 2,097,152 bytes (2 MB) |
| Reserved blocks (0-3) | 4 x 4096 = 16,384 bytes |
| Available for files/dirs | 508 x 4096 = 2,080,768 bytes |
| Open file table (g_fdt) | 8 x 9 = 72 bytes (BSS) |
| CWD path buffer | 128 bytes (BSS) |
| Code (.text contribution) | ~4 KB (ulfs.c + fs_cmds.c) |

**Heap impact:** 2 MB from a 498 MB KMEM heap = 0.4%.

---

## 9. SRS Traceability

| SRS ID | Requirement | Status |
|--------|-------------|--------|
| DC-001 | C11 standard, no stdlib | PASS |
| DC-002 | No runtime dynamic allocation | PASS -- single KMEM_Alloc at boot |
| DC-003 | Code size <= 256 KB | PASS -- total text = 32 KB |
| FR-015 | Error handling via Status codes | PASS -- all paths return Status |
| FR-020 | Memory usage statistics | PASS -- ULFS_GetStats() + df command |
| FR-021 | UART-based I/O interface | PASS -- all shell output via UART |
| FR-023 | System status reporting | PASS -- df, stat commands |
| PDR-002 | Deterministic allocation patterns | PASS -- bitmap scan is deterministic |
| SR-002 | Memory protection (no corruption) | PARTIAL -- bid bounds checked |

---

## 10. Known Limitations

| Limitation | Severity | Mitigation Path |
|-----------|---------|----------------|
| Max 126 files per directory | Low | Multi-block directories |
| Max file size 4 MB | Medium | Add double-indirect block level |
| Fixed 2 MB total capacity | Medium | Compile-time ULFS_TOTAL_BLOCKS |
| No timestamps (mtime/atime) | Low | 8 reserved bytes in inode available |
| CWD string not canonicalized on `..` | Low | Recompute full path on cd |

---

## 11. Build Verification

```bash
$ make clean && make

=== Build complete ===
   text    data     bss     dec     hex filename
  32768     128   10360   43256    a8f8 build/kernel.elf
```

Zero warnings, zero errors. Compiled with `-Wall -Wextra -Werror`.

---

## 12. Complexity Summary

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| `bid_alloc()` | O(N/8) | N=512, worst 64 iterations |
| `bid_free()` | O(1) | Bit clear + block zero |
| `inode_alloc()` | O(k x 170) | k=1 typical |
| `inode_get()` | O(k) | Inode block chain walk |
| `file_bid_for_offset()` | O(1) | Direct or 1-level indirect |
| `dirent_lookup()` | O(128) | Fixed block, linear scan |
| `path_resolve()` | O(d x 128) | d<=8, max 1024 ops |
| `ULFS_Read / Write` | O(count/4096) | Linear in block count |
| `ULFS_Init` | O(STORE_SIZE) | One-time boot cost |
| `ULFS_GetStats` | O(N) | Full bitmap scan |

All operations are deterministically bounded -- satisfying SRS PDR-001 and PDR-002.

---

## 13. Non-Volatile Storage Integration (Dual-Store)

In order to meet the SRS requirement for "4MB Flash minimum" and to provide true persistence across system reboots, MiniOS integrates a non-volatile flash storage segment alongside the volatile RAM-based ULFS store. Note that ULFS currently operates as a **Dual-Store Architecture** (for high-speed temporary storage), where the root directory (`/`) maps to a 2MB volatile RAM volume, and the `/storage` path acts as a fully managed persistent mount mapped directly to a 2MB Shadow Buffer bridging the physical NVRAM.

### 13.1 QEMU `pflash` Architecture
The non-volatile storage driver (`hal/flash.c`) directly interfaces with QEMU `virt` machine's secondary flash memory bank (`pflash1`) located at physical address **`0x04000000`**. The host system backs this hardware node to an automatically generated `flash.img` (64 MB) created by `scripts/run.sh`. Fast block reads are achieved using hardware optimized memcpy routines `HAL_Flash_ReadBuffer()`.

### 13.2 Intel CFI01 Flash Commands
Because QEMU models physical flash hardware, the memory strictly enforces ROM rules (writes cannot simply alter memory states via pointers). The storage layer implements **Intel CFI01 Sequences** to modify flash:
- **Erase:** `0x20` (Setup) followed by `0xD0` (Confirm)
- **Program:** `0x40` (Program Setup) followed by word-write
- **Ready Polling:** Continuous polling of the Status Register (`0x70`) until the Ready bit (`0x80`) triggers.

### 13.3 Flash Storage API (`kernel/storage.h`)
The `STORAGE_Init()` function mounts the flash layer during `KERNEL_Init()`:
1. Validates the existence of the `"MNOS"` signature byte.
2. If invalid or empty (e.g. freshly created `flash.img` zeroes), it executes `HAL_Flash_EraseSector(0)` and writes the magic string back.
3. Provides high-level `STORAGE_Read`, `STORAGE_Write`, and `STORAGE_EraseSector` to cleanly interact with 256KB block sectors natively aligned limits. 

---

*MiniOS Kernel/commands branch -- commit ce8d2cc / Non-Volatile integration*
*Toolchain: aarch64-elf-gcc 10+, ARM64 Cortex-A53, QEMU virt machine*
