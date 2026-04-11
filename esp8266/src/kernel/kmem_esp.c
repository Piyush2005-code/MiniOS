/**
 * @file kmem_esp.c
 * @brief Kernel Memory Manager — Bump Allocator for MiniOS-ESP8266
 *
 * Provides a 24 KB fixed heap inside dRAM. All allocations are permanent
 * bump-pointer (no individual free). The ArenaCreate/ArenaReset API
 * is kept for source compatibility but maps to the same bump allocator.
 *
 * Memory layout within the 24 KB KMEM_HEAP_SIZE:
 *   [bump_base ... bump_current ... bump_end]
 *
 * Total ESP8266 usable dRAM: ~80 KB
 *   Wi-Fi stack + lwIP: ~30 KB (reserved by SDK)
 *   KMEM heap:           24 KB (this module)
 *   Stack + SDK misc:    ~26 KB remaining
 */

#include "kernel/kmem.h"
#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Static heap storage                                               */
/* ------------------------------------------------------------------ */

/* Placed in dRAM (not IRAM) — 24 KB */
static uint8_t g_heap_storage[KMEM_HEAP_SIZE] __attribute__((aligned(4)));

/* ------------------------------------------------------------------ */
/*  Heap state                                                        */
/* ------------------------------------------------------------------ */

static struct {
    uint8_t *base;
    uint8_t *current;
    uint8_t *end;
    uint32_t used;
    uint32_t peak;
    uint32_t alloc_count;
    bool     initialized;
} g_heap;

/* ------------------------------------------------------------------ */
/*  Internal: align pointer upward                                    */
/* ------------------------------------------------------------------ */

static ICACHE_FLASH_ATTR uint8_t *align_up(uint8_t *ptr, uint32_t align)
{
    if (align <= 1) return ptr;
    uint32_t addr = (uint32_t)(uintptr_t)ptr;
    uint32_t rem  = addr & (align - 1);
    if (rem != 0) addr += (align - rem);
    return (uint8_t *)(uintptr_t)addr;
}

/* ------------------------------------------------------------------ */
/*  KMEM_Init                                                         */
/* ------------------------------------------------------------------ */

Status ICACHE_FLASH_ATTR KMEM_Init(void)
{
    g_heap.base        = g_heap_storage;
    g_heap.current     = align_up(g_heap_storage, KMEM_MIN_ALIGN);
    g_heap.end         = g_heap_storage + KMEM_HEAP_SIZE;
    g_heap.used        = 0;
    g_heap.peak        = 0;
    g_heap.alloc_count = 0;
    g_heap.initialized = true;

    HAL_UART_PutString("[KMEM] init: ");
    HAL_UART_PutDec(KMEM_HEAP_SIZE / 1024);
    HAL_UART_PutString(" KB heap at 0x");
    HAL_UART_PutHex((uint32_t)(uintptr_t)g_heap.base);
    HAL_UART_PutString("\n");

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  KMEM_Alloc                                                        */
/* ------------------------------------------------------------------ */

void *ICACHE_FLASH_ATTR KMEM_Alloc(uint32_t size, uint32_t alignment)
{
    if (!g_heap.initialized || size == 0) return (void *)0;
    if (alignment < KMEM_MIN_ALIGN) alignment = KMEM_MIN_ALIGN;

    /* Round alignment to next power of 2 */
    uint32_t align = 1;
    while (align < alignment) align <<= 1;

    uint8_t *aligned = align_up(g_heap.current, align);
    uint8_t *next    = aligned + size;

    if (next > g_heap.end) {
        HAL_UART_PutString("[KMEM] OOM: need ");
        HAL_UART_PutDec(size);
        HAL_UART_PutString(" bytes, free=");
        HAL_UART_PutDec(KMEM_GetFreeSpace());
        HAL_UART_PutString("\n");
        return (void *)0;
    }

    g_heap.current = next;
    g_heap.used    = (uint32_t)(g_heap.current - g_heap.base);
    if (g_heap.used > g_heap.peak) {
        g_heap.peak = g_heap.used;
    }
    g_heap.alloc_count++;

    return (void *)aligned;
}

/* ------------------------------------------------------------------ */
/*  KMEM_GetFreeSpace                                                 */
/* ------------------------------------------------------------------ */

uint32_t ICACHE_FLASH_ATTR KMEM_GetFreeSpace(void)
{
    if (!g_heap.initialized) return 0;
    return (uint32_t)(g_heap.end - g_heap.current);
}

/* ------------------------------------------------------------------ */
/*  KMEM_GetStats                                                     */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR KMEM_GetStats(kmem_stats_t *stats)
{
    if (!stats) return;
    stats->heap_total   = KMEM_HEAP_SIZE;
    stats->heap_used    = g_heap.used;
    stats->heap_peak    = g_heap.peak;
    stats->alloc_count  = g_heap.alloc_count;
    stats->arena_total  = 0;
    stats->arena_used   = 0;
    stats->pool_total   = 0;
    stats->pool_used    = 0;
}

/* ------------------------------------------------------------------ */
/*  Arena API (thin wrappers — source compatibility with ARM64)       */
/* ------------------------------------------------------------------ */

kmem_arena_t *ICACHE_FLASH_ATTR KMEM_ArenaCreate(uint32_t size)
{
    /* Allocate arena header + backing storage from the bump allocator */
    kmem_arena_t *arena = (kmem_arena_t *)KMEM_Alloc(sizeof(kmem_arena_t),
                                                      KMEM_MIN_ALIGN);
    if (!arena) return (kmem_arena_t *)0;

    uint8_t *data = (uint8_t *)KMEM_Alloc(size, KMEM_TENSOR_ALIGN);
    if (!data) return (kmem_arena_t *)0;

    arena->base    = data;
    arena->current = data;
    arena->end     = data + size;
    arena->total   = size;
    arena->used    = 0;

    return arena;
}

void *ICACHE_FLASH_ATTR KMEM_ArenaAlloc(kmem_arena_t *arena, uint32_t size, uint32_t align)
{
    if (!arena || size == 0) return (void *)0;
    if (align < KMEM_MIN_ALIGN) align = KMEM_MIN_ALIGN;

    uint8_t *aligned = align_up(arena->current, align);
    uint8_t *next    = aligned + size;

    if (next > arena->end) return (void *)0;

    arena->current  = next;
    arena->used    += (uint32_t)(next - aligned);

    return (void *)aligned;
}

void ICACHE_FLASH_ATTR KMEM_ArenaReset(kmem_arena_t *arena)
{
    if (!arena) return;
    arena->current = arena->base;
    arena->used    = 0;
}

uint32_t ICACHE_FLASH_ATTR KMEM_ArenaGetUsed(const kmem_arena_t *arena)
{
    return arena ? arena->used : 0;
}

uint32_t ICACHE_FLASH_ATTR KMEM_ArenaGetTotal(const kmem_arena_t *arena)
{
    return arena ? arena->total : 0;
}
