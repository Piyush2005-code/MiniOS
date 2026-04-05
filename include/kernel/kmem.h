/**
 * @file kmem.h
 * @brief Kernel Memory Management API for MiniOS
 *
 * Provides three allocation strategies optimized for ML inference:
 *
 *   1. Bump Allocator  — Fast, permanent allocations (thread stacks,
 *                         kernel structures). No individual free.
 *
 *   2. Arena Allocator  — Resettable regions for per-inference-cycle
 *                         tensor memory. Bulk free via arena reset.
 *                         (Per SRS FR-016, FR-017)
 *
 *   3. Pool Allocator   — Fixed-size object pools for uniform
 *                         structures (operator descriptors, etc.)
 *                         Individual alloc/free with O(1) cost.
 *
 * All allocators support cache-line alignment (64 bytes) for
 * ARM64 NEON SIMD performance. (Per SRS FR-018)
 *
 * Memory statistics are tracked for monitoring. (Per SRS FR-020)
 *
 * @note Per SRS FR-016 through FR-020
 */

#ifndef MINIOS_KERNEL_KMEM_H
#define MINIOS_KERNEL_KMEM_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

/** ARM64 cache line size (Cortex-A53/A72) */
#define KMEM_CACHE_LINE         64

/** Minimum alignment for general allocations */
#define KMEM_MIN_ALIGN          8

/** Default alignment for tensor allocations (cache-line) */
#define KMEM_TENSOR_ALIGN       KMEM_CACHE_LINE

/* ------------------------------------------------------------------ */
/*  Opaque types (implementations in kmem.c)                          */
/* ------------------------------------------------------------------ */

/** Arena allocator — resettable bulk allocator */
typedef struct kmem_arena kmem_arena_t;

/** Pool allocator — fixed-size block allocator */
typedef struct kmem_pool kmem_pool_t;

/* ------------------------------------------------------------------ */
/*  Memory Statistics                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Global memory usage statistics
 * @note Per SRS FR-020
 */
typedef struct {
    size_t heap_total;          /**< Total heap size in bytes */
    size_t heap_used;           /**< Currently allocated bytes */
    size_t heap_peak;           /**< Peak allocation watermark */
    uint32_t alloc_count;       /**< Number of allocations performed */
    size_t arena_total;         /**< Total arena memory allocated */
    size_t arena_used;          /**< Currently used arena memory */
    size_t pool_total;          /**< Total pool memory allocated */
    size_t pool_used;           /**< Currently used pool memory */
} kmem_stats_t;

/* ------------------------------------------------------------------ */
/*  Core Heap API (Bump Allocator)                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the kernel memory manager
 *
 * Uses the heap region defined by the linker (_heap_start, _heap_end).
 * Must be called after MMU initialization and before any allocations.
 *
 * @return STATUS_OK on success
 *         STATUS_ERROR_INVALID_ARGUMENT if heap region is invalid
 */
Status KMEM_Init(void);

/**
 * @brief Allocate memory from the kernel heap (bump allocator)
 *
 * Allocations are permanent — individual free is not supported.
 * Use arenas or pools for reclaimable memory.
 *
 * @param[in] size       Number of bytes to allocate
 * @param[in] alignment  Required alignment (must be power of 2, >= 8)
 * @return Pointer to allocated memory, or NULL if out of memory
 *
 * @complexity Time: O(1), Space: O(size + alignment padding)
 */
void *KMEM_Alloc(size_t size, size_t alignment);

/**
 * @brief Get the remaining free space in the heap
 * @return Free bytes available for allocation
 */
size_t KMEM_GetFreeSpace(void);

/**
 * @brief Get current memory statistics
 * @param[out] stats  Structure to fill with current statistics
 */
void KMEM_GetStats(kmem_stats_t *stats);

/* ------------------------------------------------------------------ */
/*  Arena Allocator (Resettable Region)                               */
/*  Per SRS FR-016: Pre-execution memory allocation                   */
/*  Per SRS FR-017: Tensor memory reuse via arena reset               */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a new memory arena of the given size
 *
 * The arena is allocated from the main heap. All arena allocations
 * come from this pre-reserved region.
 *
 * @param[in] size  Total size of the arena in bytes
 * @return Pointer to arena, or NULL on failure
 */
kmem_arena_t *KMEM_ArenaCreate(size_t size);

/**
 * @brief Allocate from an arena with specified alignment
 *
 * @param[in] arena      The arena to allocate from
 * @param[in] size       Bytes to allocate
 * @param[in] alignment  Required alignment (power of 2)
 * @return Pointer to allocated memory, or NULL if arena exhausted
 *
 * @complexity Time: O(1)
 */
void *KMEM_ArenaAlloc(kmem_arena_t *arena, size_t size, size_t alignment);

/**
 * @brief Reset an arena, freeing all its allocations at once
 *
 * After reset, the arena can be reused for the next inference cycle.
 * This is the primary mechanism for tensor memory reuse.
 *
 * @param[in] arena  The arena to reset
 *
 * @complexity Time: O(1)
 */
void KMEM_ArenaReset(kmem_arena_t *arena);

/**
 * @brief Get the number of bytes currently used in an arena
 * @param[in] arena  The arena to query
 * @return Bytes currently allocated
 */
size_t KMEM_ArenaGetUsed(const kmem_arena_t *arena);

/**
 * @brief Get the total capacity of an arena
 * @param[in] arena  The arena to query
 * @return Total arena size in bytes
 */
size_t KMEM_ArenaGetTotal(const kmem_arena_t *arena);

/* ------------------------------------------------------------------ */
/*  Pool Allocator (Fixed-Size Blocks)                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a fixed-size block pool
 *
 * Pre-allocates a contiguous region and divides it into uniform
 * blocks. Alloc/free operations are O(1) using a free list.
 *
 * @param[in] elem_size  Size of each element (will be rounded up to alignment)
 * @param[in] count      Number of elements in the pool
 * @return Pointer to pool, or NULL on failure
 */
kmem_pool_t *KMEM_PoolCreate(size_t elem_size, size_t count);

/**
 * @brief Allocate one element from a pool
 * @param[in] pool  The pool to allocate from
 * @return Pointer to element, or NULL if pool exhausted
 *
 * @complexity Time: O(1)
 */
void *KMEM_PoolAlloc(kmem_pool_t *pool);

/**
 * @brief Return an element to a pool
 * @param[in] pool  The pool the element belongs to
 * @param[in] ptr   Pointer previously returned by KMEM_PoolAlloc
 *
 * @complexity Time: O(1)
 */
void KMEM_PoolFree(kmem_pool_t *pool, void *ptr);

/**
 * @brief Get the number of elements currently allocated from a pool
 * @param[in] pool  The pool to query
 * @return Number of allocated elements
 */
size_t KMEM_PoolGetUsed(const kmem_pool_t *pool);

/* ------------------------------------------------------------------ */
/*  Tensor Memory (Convenience wrappers)                              */
/*  Per SRS FR-018: Cache-aware memory layout (64-byte alignment)     */
/* ------------------------------------------------------------------ */

/**
 * @brief Allocate cache-aligned tensor memory from an arena
 *
 * Ensures 64-byte alignment for optimal NEON SIMD access and
 * cache performance on ARM64.
 *
 * @param[in] arena  Arena to allocate from (tensor lifetime arena)
 * @param[in] size   Tensor size in bytes
 * @return Pointer to aligned tensor memory, or NULL on failure
 */
void *KMEM_TensorAlloc(kmem_arena_t *arena, size_t size);

/**
 * @brief Allocate zeroed tensor memory from an arena
 *
 * Same as KMEM_TensorAlloc but memory is zero-initialized.
 *
 * @param[in] arena  Arena to allocate from
 * @param[in] size   Tensor size in bytes
 * @return Pointer to zeroed aligned memory, or NULL on failure
 */
void *KMEM_TensorAllocZeroed(kmem_arena_t *arena, size_t size);

#endif /* MINIOS_KERNEL_KMEM_H */
