/**
 * @file kmem.h
 * @brief Kernel memory manager — bump allocator interface
 *
 * Provides a simple bump (linear) allocator over the kernel heap region
 * defined by the linker symbols `_heap_start` and `_heap_end`.
 *
 * Design constraints (Sprint 1):
 *   - No free operation — lifetime of allocations matches the kernel.
 *   - Allocations are alignment-padded on request.
 *   - Statistics are tracked for diagnostic output.
 *
 * Planned extensions (Sprint 2+):
 *   - Arena allocator: resettable per-inference regions.
 *   - Pool allocator:  fixed-size object recycling.
 */

#ifndef MINIOS_KMEM_H
#define MINIOS_KMEM_H

#include "types.h"
#include "status.h"

/** Cache-line size on Cortex-A53 */
#define KMEM_CACHE_LINE  64u
/** Minimum allocation alignment (pointer-sized) */
#define KMEM_MIN_ALIGN    8u

/**
 * @brief Heap usage statistics snapshot.
 */
typedef struct {
    size_t heap_total;   /**< Total heap bytes available */
    size_t heap_used;    /**< Bytes allocated so far     */
    size_t heap_free;    /**< Bytes still available       */
    size_t alloc_count;  /**< Number of allocations made  */
    size_t peak_usage;   /**< Highest watermark observed  */
} kmem_stats_t;

/* ------------------------------------------------------------------ */
/*  Bump allocator API                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise the kernel heap.
 *
 * Must be called once before any KMEM_Alloc call.
 * Uses linker symbols: _heap_start, _heap_end.
 *
 * @return STATUS_OK on success.
 */
Status KMEM_Init(void);

/**
 * @brief Allocate a block from the kernel heap.
 *
 * @param size      Bytes to allocate (must be > 0).
 * @param alignment Power-of-two alignment (0 or 1 = KMEM_MIN_ALIGN).
 * @return Pointer to allocated block, or NULL on failure.
 */
void *KMEM_Alloc(size_t size, size_t alignment);

/**
 * @brief Return free bytes remaining in the heap.
 */
size_t KMEM_GetFreeSpace(void);

/**
 * @brief Populate a statistics snapshot.
 * @param[out] stats Caller-supplied struct to fill.
 */
Status KMEM_GetStats(kmem_stats_t *stats);

#endif /* MINIOS_KMEM_H */
