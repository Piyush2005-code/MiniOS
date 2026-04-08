# ULFS — Ultra Lightweight File System for MiniOS

## Overview

Implement a **minimal, in-memory ULFS file system** adapted from the published paper
"ULFS: Ultra Lightweight File System for Unikernels" (Appl. Sci. 2024, 14, 3342) into
the existing MiniOS ARM64 unikernel codebase. The implementation uses memory-backed
storage (from the existing KMEM heap) instead of mmap-host-file backends, which is
appropriate for a bare-metal ARM64 target. File system commands (`ls`, `cd`, `mkdir`,
`touch`, `cat`, `write`, `rm`, `pwd`, `stat`) will be wired into the existing
`CMD_Dispatch` / shell framework.

---

## ULFS Architecture (from PDF)

### Key Design Principles (adapted for MiniOS)
1. **Block-based storage:** 4 KB blocks identified by block-id (bid)
2. **No locking:** single application/thread per unikernel — lock-free design
3. **Minimalist inodes:** fixed-size inode with just type, size, and block pointers
4. **Bitmap allocation:** map-blocks track free/used blocks
5. **Indirect indexing:** for files > 4 KB, a "bid block" holds up to 1023 next-block pointers
6. **Directory as fixed-length entries:** name + inode-number pairs in data blocks

### Data Structures (adapted)

```
Block 0 : Superblock        — total_blocks, block_size, magic
Block 1 : Map Block 0       — bitmap of 4096 blocks
Block 2 : Root inode block  — 170 inodes per block, linked list
Block 3+: Data / Dir / Bid blocks
```

**Inode (24 bytes, 170 per inode-block):**
```c
typedef struct {
    uint8_t  type;       // 0=free, 1=regular, 2=directory
    uint8_t  _pad[3];
    uint32_t size;       // file size in bytes
    uint32_t bid_head;   // first data block bid
    uint32_t bid_block;  // indirect index block bid (0=none)
    uint8_t  _reserved[8];
} ulfs_inode_t;          // 24 bytes
```

**Directory entry (32 bytes, 128 per block):**
```c
typedef struct {
    uint32_t ino;         // inode number (0=free)
    char     name[28];    // null-terminated filename
} ulfs_dirent_t;          // 32 bytes
```

**Inode block (4096 bytes):**
```c
typedef struct {
    uint32_t n_used;      // # of in-use inodes in this block
    uint32_t ino_start;   // first inode number in this block
    uint32_t bid_prev;    // previous inode block bid (linked list)
    uint32_t bid_next;    // next inode block bid (0=end)
    ulfs_inode_t inodes[170];  // 170 * 24 = 4080 bytes
} ulfs_inode_block_t;    // 4096 bytes (16 bytes meta + 4080 data)
```

---

## Proposed Changes

### New Subsystem: `fs/` (File System)

---

#### [NEW] `include/kernel/ulfs.h`
Public API header for the ULFS file system. Defines constants, data structures,
and all public functions (`ULFS_Init`, `ULFS_Open`, `ULFS_Read`, `ULFS_Write`,
`ULFS_Close`, `ULFS_Create`, `ULFS_Mkdir`, `ULFS_Unlink`, `ULFS_Readdir`,
`ULFS_Stat`, `ULFS_ChDir`, `ULFS_GetCwd`).

#### [NEW] `src/kernel/ulfs.c`
Implementation of the full ULFS file system. Uses `KMEM_Alloc` to carve out a fixed
backing store (default **2 MB** = 512 × 4 KB blocks) from the kernel heap at init time.
All block I/O is simple array-index operations over this memory region.

**Key algorithms:**
- `bid_alloc()` — linear scan of map-block bitmap → O(n/64) with 64-bit words
- `inode_alloc()` — scan inode blocks for free slot, allocate new inode block if needed
- `bid_to_ptr(bid)` — direct pointer arithmetic: `fs_base + bid * 4096`
- `file_bid_for_offset(ino, off)` — traverse bid-block chain for indirect access
- `dirent_lookup(dir_ino, name)` — linear scan of directory entries

---

#### [NEW] `include/kernel/fs_cmds.h`
Header declaring `FS_RegisterCommands()` which registers all file-system shell commands.

#### [NEW] `src/kernel/fs_cmds.c`
Implements shell command handlers and registers them via `CMD_Register`:

| Command | Behaviour |
|---------|-----------|
| `ls [path]` | List directory contents |
| `cd <path>` | Change current working directory |
| `pwd` | Print current working directory |
| `mkdir <name>` | Create directory |
| `touch <name>` | Create empty file |
| `cat <file>` | Print file contents |
| `write <file> <text>` | Write text to a file (overwrites) |
| `rm <name>` | Remove a file or empty directory |
| `stat <name>` | Show inode info for a file |
| `df` | Show file system usage (blocks used/free) |

---

#### [MODIFY] `src/kernel/main.c`
Add ULFS init call and `FS_RegisterCommands()` call before `SCHED_Start()`.

#### [MODIFY] `include/kernel/kapi.h`
Include `ulfs.h` and `fs_cmds.h` in the master kernel API header.

#### [MODIFY] `Makefile`
Add `src/kernel/ulfs.c` and `src/kernel/fs_cmds.c` to `C_SRCS`.

#### [MODIFY] `PROJECT_DOCUMENTATION.md`
Add Section 13 (or renumber from 13): File System subsystem documentation.

---

## Sizing & Memory

| Item | Size |
|------|------|
| Backing store | 2 MB (512 blocks × 4 KB) |
| Max files | ~170 (first inode block only, expandable) |
| Max filename | 27 chars |
| Open file table | 8 simultaneous open files |
| CWD path buffer | 128 bytes |

The 2 MB backing store is a tiny fraction of the 498 MB heap available, keeping the
system memory-safe and within the SRS constraint (DC-002: no runtime malloc during
inference; ULFS is initialized at boot before the scheduler starts).

---

## Verification Plan

### Build Verification
```bash
make clean && make
```
Expected: zero errors, kernel.elf produced.

### Functional Shell Tests (manual via `make run`)
```
miniOS> df
miniOS> ls
miniOS> mkdir test
miniOS> cd test
miniOS> pwd
miniOS> touch hello
miniOS> write hello world
miniOS> cat hello
miniOS> stat hello
miniOS> rm hello
miniOS> cd ..
miniOS> ls
```

---

## Open Questions / Notes

> [!NOTE]
> The SRS explicitly excludes "General-purpose filesystems" from scope (Section 1.3).
> However, ULFS is justified here as a *minimal in-memory* FS used for ML model
> staging and logging — consistent with the unikernel philosophy and lightweight
> enough to satisfy the SRS minimalism principle.

> [!IMPORTANT]
> The shell's `CMD_LINE_MAX = 80` chars limits `write` command payload. This is
> sufficient for a demo. The limit can be extended later.
