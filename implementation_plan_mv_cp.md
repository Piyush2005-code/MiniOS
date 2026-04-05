# Dual-Store ULFS & Non-Volatile Mount Path Implementation

The objective is to implement a dual-storage file system architecture for MiniOS:
1. The root file system `/` remains an entirely volatile, RAM-only store.
2. The `/storage` path becomes a dedicated mount point pointing to the persistent NVRAM (QEMU pflash).
3. Any files modified within `/storage` are flushed back to the 4MB QEMU `flash.img` persistently.
4. Addition of `cp` (copy) and `mv` (move) commands to transition files between the volatile and non-volatile contexts.

## User Review Required

> [!IMPORTANT]
> Because writing directly to Flash memory requires block-level sector wiping (256KB chunks at a time), we cannot safely rewrite single directory entries byte-by-byte instantly on QEMU's `pflash1` model. 
> Therefore, we will employ a **Shadow Buffer Pattern**: 
> 1. At boot, we will cleanly copy the persistent NVRAM flash into a 2MB RAM buffer (`store[1]`).
> 2. All file I/O operations inside `/storage` will act on this RAM buffer at lightning speed.
> 3. We will provide an automatic `ULFS_Sync()` function that seamlessly detects if `/storage` was modified and subsequently commits the whole 2MB buffer safely to the underlying Flash Memory in the background.

## Proposed Changes

---

### File System Core (`src/kernel/ulfs.c` & `include/kernel/ulfs.h`)

We will adapt the single-instance ULFS to be an array-based multi-instance module.
- We will define `g_stores[2]`, where index 0 is RAM and index 1 is Flash.
- **Boot Loading**: `ULFS_Init()` will initialize `g_stores[0]` as usual. It will then `STORAGE_Read` 2MB of flash into `g_stores[1]`. If `g_stores[1]` has the `ULFS_MAGIC` signature, it successfully recovers the files from previous reboots! If not, it formats a new ULFS layout in the shadow buffer and syncs it.
- **Path Resolution**: `path_resolve()` will dynamically map string prefixes. If a user requests an absolute path starting with `/storage`, the kernel strips the prefix and passes the rest of the path down to `store_idx = 1`. 
- **Syncing**: Expose `ULFS_Sync()`. When invoked, it checks a dirty-flag on the NVRAM store. If dirty, it performs 8 consecutive `STORAGE_EraseSector` and `STORAGE_Write` operations on 256KB boundaries to commit the whole NVRAM FS back to QEMU flash.

#### [MODIFY] `include/kernel/ulfs.h`
- Add `ULFS_Sync(void)`.
- Update FD struct internally.

#### [MODIFY] `src/kernel/ulfs.c`
- Modify structural functions (`bid_alloc`, `dirent_lookup`, etc.) to accept a `store_idx` parameter.
- Implement Mount-Point path splicing inside `path_resolve()`.
- Add `ULFS_Sync()` implementation that utilizes `kernel/storage.h` APIs.

---

### Command Framework (`src/kernel/fs_cmds.c`)

We need utilities capable of streaming data seamlessly across the two filesystems.
- Add `cmd_cp(char *args)`: Opens the source file, calculates file size, reads out into a temporary `KMEM_Alloc` buffer, creates the destination file, and writes the chunk. 
- Add `cmd_mv(char *args)`: Exact identical behavior to `cp`, immediately followed by a `rm` of the source file.
- We will wire both commands into `FS_RegisterCommands()`.
- Ensure all commands (`cp`, `mv`, `write`, `rm`, `mkdir`) explicitly call `ULFS_Sync()` immediately after modifying the FS to guarantee auto-saving behavior for `/storage`.

#### [MODIFY] `src/kernel/fs_cmds.c`
- Add `cp` logic block.
- Add `mv` logic block.

## Verification Plan

### Automated Tests
1. Compile Unikernel using `make clean && make`.
2. Ensure Zero warnings regarding pointer alignment or missing symbols.

### Manual Verification
1. Boot MiniOS.
2. Execute `mkdir /storage/docs` and `write /storage/docs/hello.txt "Persistent Text!"`.
3. Halt the Emulation.
4. Reboot MiniOS entirely.
5. Execute `cat /storage/docs/hello.txt` to verify the persistence.
6. Test volatile constraints by `write /temp.txt "Lost data"` and verifying `/temp.txt` does *not* survive a restart.
7. Test crossover operations: `cp /storage/docs/hello.txt /volatile.txt` inside an active runtime.
