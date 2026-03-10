/**
 * @file kmem.c
 * @brief Kernel Memory Management implementation for MiniOS
 *
 * Implements three allocation strategies:
 *   1. Bump Allocator  — permanent kernel allocations
 *   2. Arena Allocator  — resettable per-inference-cycle memory
 *   3. Pool Allocator   — fixed-size object pools
 *
 * The bump allocator works from the linker-defined heap region
 * (_heap_start to _heap_end). Arenas and pools are allocated
 * from the bump region and provide their own sub-allocation.
 *
 * All allocations are aligned to at least KMEM_MIN_ALIGN (8 bytes).
 * Tensor allocations use KMEM_TENSOR_ALIGN (64 bytes) for cache
 * line optimization on ARM64 Cortex-A series.
 *
 * @note Per SRS FR-016 through FR-020
 *
 * @complexity
 *   KMEM_Alloc:       O(1) bump
 *   KMEM_ArenaAlloc:  O(1) bump
 *   KMEM_ArenaReset:  O(1) pointer reset
 *   KMEM_PoolAlloc:   O(1) free-list pop
 *   KMEM_PoolFree:    O(1) free-list push
 */

#include "kernel/kmem.h"
#include "lib/string.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Linker-defined heap boundaries                                    */
/* ------------------------------------------------------------------ */
extern uint8_t _heap_start[];
extern uint8_t _heap_end[];

/* ------------------------------------------------------------------ */
/*  Arena structure (opaque in header)                                */
/* ------------------------------------------------------------------ */
struct kmem_arena {
    uint8_t *base;          /* Start of arena data region */
    uint8_t *current;       /* Current bump pointer */
    uint8_t *end;           /* End of arena data region */
    size_t   total;         /* Total capacity */
    size_t   used;          /* Bytes currently allocated */
    size_t   peak;          /* Peak usage watermark */
};

/* ------------------------------------------------------------------ */
/*  Pool structure (opaque in header)                                 */
/* ------------------------------------------------------------------ */

/** Free-list node embedded in each free block */
typedef struct pool_node {
    struct pool_node *next;
} pool_node_t;

struct kmem_pool {
    pool_node_t *free_list; /* Head of free-list */
    uint8_t     *base;      /* Start of pool data */
    size_t       elem_size; /* Size of each element (aligned) */
    size_t       capacity;  /* Total number of elements */
    size_t       used;      /* Currently allocated elements */
};

/* ------------------------------------------------------------------ */
/*  Global heap state                                                 */
/* ------------------------------------------------------------------ */
static struct {
    uint8_t *base;          /* Heap start */
    uint8_t *current;       /* Current bump pointer */
    uint8_t *end;           /* Heap end */
    size_t   total;         /* Total size */
    size_t   used;          /* Bytes allocated */
    size_t   peak;          /* Peak watermark */
    uint32_t alloc_count;   /* Number of allocations */
    bool     initialized;   /* Init flag */

    /* Track arena/pool aggregate stats */
    size_t   arena_total;
    size_t   arena_used;
    size_t   pool_total;
    size_t   pool_used;
} heap;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Align a pointer up to the given alignment
 */
static inline uintptr_t align_up(uintptr_t addr, size_t alignment)
{
    return (addr + alignment - 1) & ~(alignment - 1);
}

/**
 * @brief Check if a value is a power of 2
 */
static inline bool is_power_of_2(size_t v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

/* ------------------------------------------------------------------ */
/*  Core Heap (Bump Allocator)                                        */
/* ------------------------------------------------------------------ */

Status KMEM_Init(void)
{
    uintptr_t start = (uintptr_t)_heap_start;
    uintptr_t end   = (uintptr_t)_heap_end;

    if (start >= end || start == 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    /* Ensure start is page-aligned (should be, from linker) */
    start = align_up(start, 4096);

    heap.base       = (uint8_t *)start;
    heap.current    = (uint8_t *)start;
    heap.end        = (uint8_t *)end;
    heap.total      = end - start;
    heap.used       = 0;
    heap.peak       = 0;
    heap.alloc_count = 0;
    heap.initialized = true;
    heap.arena_total = 0;
    heap.arena_used  = 0;
    heap.pool_total  = 0;
    heap.pool_used   = 0;

    HAL_UART_PutString("[KMEM] Heap initialized: ");
    HAL_UART_PutHex((uint64_t)start);
    HAL_UART_PutString(" - ");
    HAL_UART_PutHex((uint64_t)end);
    HAL_UART_PutString(" (");
    HAL_UART_PutDec(heap.total / 1024);
    HAL_UART_PutString(" KB)\n");

    return STATUS_OK;
}

void *KMEM_Alloc(size_t size, size_t alignment)
{
    if (!heap.initialized || size == 0) {
        return NULL;
    }

    /* Enforce minimum alignment and power-of-2 */
    if (alignment < KMEM_MIN_ALIGN) {
        alignment = KMEM_MIN_ALIGN;
    }
    if (!is_power_of_2(alignment)) {
        return NULL;
    }

    uintptr_t current = (uintptr_t)heap.current;
    uintptr_t aligned = align_up(current, alignment);
    uintptr_t new_end = aligned + size;

    if (new_end > (uintptr_t)heap.end) {
        HAL_UART_PutString("[KMEM] ERROR: Out of memory! Requested ");
        HAL_UART_PutDec(size);
        HAL_UART_PutString(" bytes\n");
        return NULL;
    }

    heap.current = (uint8_t *)new_end;
    heap.used += (new_end - current);  /* Include alignment padding */
    heap.alloc_count++;

    if (heap.used > heap.peak) {
        heap.peak = heap.used;
    }

    return (void *)aligned;
}

size_t KMEM_GetFreeSpace(void)
{
    if (!heap.initialized) return 0;
    return (size_t)((uintptr_t)heap.end - (uintptr_t)heap.current);
}

void KMEM_GetStats(kmem_stats_t *stats)
{
    if (stats == NULL) return;

    stats->heap_total   = heap.total;
    stats->heap_used    = heap.used;
    stats->heap_peak    = heap.peak;
    stats->alloc_count  = heap.alloc_count;
    stats->arena_total  = heap.arena_total;
    stats->arena_used   = heap.arena_used;
    stats->pool_total   = heap.pool_total;
    stats->pool_used    = heap.pool_used;
}

/* ------------------------------------------------------------------ */
/*  Arena Allocator                                                   */
/* ------------------------------------------------------------------ */

kmem_arena_t *KMEM_ArenaCreate(size_t size)
{
    if (size == 0) return NULL;

    /* Allocate the arena header */
    kmem_arena_t *arena = (kmem_arena_t *)KMEM_Alloc(
        sizeof(kmem_arena_t), KMEM_MIN_ALIGN);
    if (arena == NULL) return NULL;

    /* Allocate the arena data region, cache-line aligned */
    uint8_t *data = (uint8_t *)KMEM_Alloc(size, KMEM_CACHE_LINE);
    if (data == NULL) return NULL;

    arena->base    = data;
    arena->current = data;
    arena->end     = data + size;
    arena->total   = size;
    arena->used    = 0;
    arena->peak    = 0;

    heap.arena_total += size;

    HAL_UART_PutString("[KMEM] Arena created: ");
    HAL_UART_PutDec(size / 1024);
    HAL_UART_PutString(" KB at ");
    HAL_UART_PutHex((uint64_t)(uintptr_t)data);
    HAL_UART_PutString("\n");

    return arena;
}

void *KMEM_ArenaAlloc(kmem_arena_t *arena, size_t size, size_t alignment)
{
    if (arena == NULL || size == 0) return NULL;

    if (alignment < KMEM_MIN_ALIGN) {
        alignment = KMEM_MIN_ALIGN;
    }

    uintptr_t current = (uintptr_t)arena->current;
    uintptr_t aligned = align_up(current, alignment);
    uintptr_t new_end = aligned + size;

    if (new_end > (uintptr_t)arena->end) {
        return NULL;
    }

    arena->current = (uint8_t *)new_end;
    arena->used += (new_end - current);

    /* Track in global stats */
    heap.arena_used += (new_end - current);

    if (arena->used > arena->peak) {
        arena->peak = arena->used;
    }

    return (void *)aligned;
}

void KMEM_ArenaReset(kmem_arena_t *arena)
{
    if (arena == NULL) return;

    /* Update global stats before reset */
    heap.arena_used -= arena->used;

    arena->current = arena->base;
    arena->used    = 0;
}

size_t KMEM_ArenaGetUsed(const kmem_arena_t *arena)
{
    if (arena == NULL) return 0;
    return arena->used;
}

size_t KMEM_ArenaGetTotal(const kmem_arena_t *arena)
{
    if (arena == NULL) return 0;
    return arena->total;
}

/* ------------------------------------------------------------------ */
/*  Pool Allocator                                                    */
/* ------------------------------------------------------------------ */

kmem_pool_t *KMEM_PoolCreate(size_t elem_size, size_t count)
{
    if (elem_size == 0 || count == 0) return NULL;

    /* Ensure element is large enough to hold a free-list node
     * and aligned to KMEM_MIN_ALIGN */
    if (elem_size < sizeof(pool_node_t)) {
        elem_size = sizeof(pool_node_t);
    }
    elem_size = align_up(elem_size, KMEM_MIN_ALIGN);

    /* Allocate pool header */
    kmem_pool_t *pool = (kmem_pool_t *)KMEM_Alloc(
        sizeof(kmem_pool_t), KMEM_MIN_ALIGN);
    if (pool == NULL) return NULL;

    /* Allocate pool data, cache-line aligned */
    size_t total_size = elem_size * count;
    uint8_t *data = (uint8_t *)KMEM_Alloc(total_size, KMEM_CACHE_LINE);
    if (data == NULL) return NULL;

    pool->base      = data;
    pool->elem_size = elem_size;
    pool->capacity  = count;
    pool->used      = 0;

    /* Build free list */
    pool->free_list = NULL;
    for (size_t i = 0; i < count; i++) {
        pool_node_t *node = (pool_node_t *)(data + i * elem_size);
        node->next = pool->free_list;
        pool->free_list = node;
    }

    heap.pool_total += total_size;

    HAL_UART_PutString("[KMEM] Pool created: ");
    HAL_UART_PutDec(count);
    HAL_UART_PutString(" x ");
    HAL_UART_PutDec(elem_size);
    HAL_UART_PutString(" bytes at ");
    HAL_UART_PutHex((uint64_t)(uintptr_t)data);
    HAL_UART_PutString("\n");

    return pool;
}

void *KMEM_PoolAlloc(kmem_pool_t *pool)
{
    if (pool == NULL || pool->free_list == NULL) {
        return NULL;
    }

    pool_node_t *node = pool->free_list;
    pool->free_list = node->next;
    pool->used++;

    heap.pool_used += pool->elem_size;

    return (void *)node;
}

void KMEM_PoolFree(kmem_pool_t *pool, void *ptr)
{
    if (pool == NULL || ptr == NULL) return;

    pool_node_t *node = (pool_node_t *)ptr;
    node->next = pool->free_list;
    pool->free_list = node;
    pool->used--;

    heap.pool_used -= pool->elem_size;
}

size_t KMEM_PoolGetUsed(const kmem_pool_t *pool)
{
    if (pool == NULL) return 0;
    return pool->used;
}

/* ------------------------------------------------------------------ */
/*  Tensor Memory (Cache-aligned convenience wrappers)                */
/* ------------------------------------------------------------------ */

void *KMEM_TensorAlloc(kmem_arena_t *arena, size_t size)
{
    return KMEM_ArenaAlloc(arena, size, KMEM_TENSOR_ALIGN);
}

void *KMEM_TensorAllocZeroed(kmem_arena_t *arena, size_t size)
{
    void *ptr = KMEM_ArenaAlloc(arena, size, KMEM_TENSOR_ALIGN);
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}
