/**
 * @file mem.h
 * @brief Memory Manager interface for MiniOS
 *
 * Provides a bump allocator for static, pre-execution memory
 * allocation. All memory is allocated from a contiguous heap
 * region defined by the linker script.
 *
 * Design constraints (per SRS):
 *   - DC-002: No runtime dynamic allocation (malloc/free semantics)
 *   - FR-016: All memory allocated before execution begins
 *   - FR-018: 64-byte cache-line alignment for tensors
 *   - FR-020: Memory usage statistics
 *
 * @complexity Time: O(1) for all operations
 *             Space: Heap region defined by linker
 */

#ifndef MINIOS_KERNEL_MEM_H
#define MINIOS_KERNEL_MEM_H

#include "status.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Configuration                                                     */
/* ------------------------------------------------------------------ */

/** Default alignment for general allocations (cache-line) */
#define MEM_DEFAULT_ALIGNMENT 64

/** Minimum alignment (must be power of 2) */
#define MEM_MIN_ALIGNMENT 8

/* ------------------------------------------------------------------ */
/*  Memory statistics                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
  size_t total_bytes;     /**< Total heap size                  */
  size_t used_bytes;      /**< Currently allocated bytes        */
  size_t free_bytes;      /**< Remaining free bytes             */
  size_t peak_used_bytes; /**< High-water mark                  */
  uint32_t alloc_count;   /**< Number of allocations made       */
  size_t wasted_bytes;    /**< Bytes lost to alignment padding  */
} MemStats;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the memory manager
 *
 * Sets up the bump allocator over the given region.
 * Typically called with linker-provided _heap_start / _heap_end.
 *
 * @param[in] heap_base  Start of the heap region
 * @param[in] heap_size  Size of the heap in bytes
 * @return STATUS_OK on success
 */
Status MEM_Init(void *heap_base, size_t heap_size);

/**
 * @brief Allocate memory with specified alignment
 *
 * Bump-allocates from the heap. Never freed individually;
 * the entire heap can be reset with MEM_Reset().
 *
 * @param[in] size       Number of bytes to allocate
 * @param[in] alignment  Alignment requirement (must be power of 2)
 * @return Pointer to allocated memory, or NULL if insufficient space
 */
void *MEM_Alloc(size_t size, size_t alignment);

/**
 * @brief Allocate tensor memory (64-byte aligned)
 *
 * Convenience wrapper for tensor allocations per FR-018.
 *
 * @param[in] size  Number of bytes to allocate
 * @return Pointer to 64-byte-aligned memory, or NULL
 */
void *MEM_AllocTensor(size_t size);

/**
 * @brief Reset the allocator (free all allocations)
 *
 * Resets the bump pointer to the start. Used between
 * model loads to reclaim all tensor memory.
 *
 * @warning All previously returned pointers become invalid.
 */
void MEM_Reset(void);

/**
 * @brief Get remaining free bytes
 * @return Number of unallocated bytes
 */
size_t MEM_GetFreeBytes(void);

/**
 * @brief Get currently used bytes
 * @return Number of allocated bytes (including alignment waste)
 */
size_t MEM_GetUsedBytes(void);

/**
 * @brief Get peak memory usage
 * @return High-water mark of used bytes
 */
size_t MEM_GetPeakUsage(void);

/**
 * @brief Get full memory statistics
 * @param[out] stats  Pointer to MemStats struct to fill
 * @return STATUS_OK on success
 */
Status MEM_GetStats(MemStats *stats);

/**
 * @brief Print memory statistics via UART
 */
void MEM_PrintStats(void);

/* ------------------------------------------------------------------ */
/*  Utility functions (no libc dependency)                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Copy memory from src to dst
 * @param[out] dst  Destination pointer
 * @param[in]  src  Source pointer
 * @param[in]  n    Number of bytes to copy
 */
void MEM_Copy(void *dst, const void *src, size_t n);

/**
 * @brief Fill memory with a byte value
 * @param[out] dst  Destination pointer
 * @param[in]  val  Byte value to fill
 * @param[in]  n    Number of bytes to fill
 */
void MEM_Set(void *dst, uint8_t val, size_t n);

/**
 * @brief Compare two memory regions
 * @param[in] a  First region
 * @param[in] b  Second region
 * @param[in] n  Number of bytes to compare
 * @return 0 if equal, non-zero otherwise
 */
int MEM_Compare(const void *a, const void *b, size_t n);

#endif /* MINIOS_KERNEL_MEM_H */
