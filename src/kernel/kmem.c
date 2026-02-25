/**
 * @file kmem.c
 * @brief Kernel bump allocator implementation
 */

#include "kernel/kmem.h"
#include "hal/uart.h"
#include "lib/string.h"

/* ------------------------------------------------------------------ */
/*  Linker symbols                                                     */
/* ------------------------------------------------------------------ */
extern uint8_t _heap_start[];
extern uint8_t _heap_end[];

/* ------------------------------------------------------------------ */
/*  Heap state                                                         */
/* ------------------------------------------------------------------ */
static struct {
    uint8_t *base;
    uint8_t *current;
    uint8_t *end;
    size_t   total;
    size_t   used;
    size_t   peak;
    size_t   alloc_count;
    bool     initialized;
} heap;

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

Status KMEM_Init(void)
{
    if (heap.initialized) {
        return STATUS_OK;
    }

    heap.base        = _heap_start;
    heap.current     = _heap_start;
    heap.end         = _heap_end;
    heap.total       = (size_t)(_heap_end - _heap_start);
    heap.used        = 0;
    heap.peak        = 0;
    heap.alloc_count = 0;
    heap.initialized = true;

    HAL_UART_PutString("[KMEM] Heap initialised: 0x");
    HAL_UART_PutHex((uint64_t)(uintptr_t)heap.base);
    HAL_UART_PutString(" - 0x");
    HAL_UART_PutHex((uint64_t)(uintptr_t)heap.end);
    HAL_UART_PutString(" (");
    HAL_UART_PutDec((uint32_t)(heap.total / 1024));
    HAL_UART_PutString(" KB)\n");

    return STATUS_OK;
}

void *KMEM_Alloc(size_t size, size_t alignment)
{
    if (!heap.initialized || size == 0) {
        return NULL;
    }

    if (alignment < KMEM_MIN_ALIGN) {
        alignment = KMEM_MIN_ALIGN;
    }

    /*
     * Align the current pointer upward.
     *
     * For a power-of-two alignment `a` the mask trick is:
     *   aligned = (ptr + a - 1) & ~(a - 1)
     *
     * This works because (a-1) is all ones in the lower bits, so ~(a-1)
     * clears them.  Adding (a-1) before masking handles the "already
     * aligned" case (no change) and the "needs padding" case correctly.
     */
    uintptr_t mask  = (uintptr_t)(alignment - 1u);
    uintptr_t ptr   = ((uintptr_t)heap.current + mask) & ~mask;

    uint8_t *alloc_start = (uint8_t *)ptr;
    uint8_t *alloc_end   = alloc_start + size;

    if (alloc_end > heap.end) {
        HAL_UART_PutString("[KMEM] OOM: requested ");
        HAL_UART_PutDec((uint32_t)size);
        HAL_UART_PutString(" bytes\n");
        return NULL;
    }

    heap.current     = alloc_end;
    heap.used        = (size_t)(heap.current - heap.base);
    heap.alloc_count++;

    if (heap.used > heap.peak) {
        heap.peak = heap.used;
    }

    return (void *)alloc_start;
}

size_t KMEM_GetFreeSpace(void)
{
    if (!heap.initialized) return 0;
    return (size_t)(heap.end - heap.current);
}

Status KMEM_GetStats(kmem_stats_t *stats)
{
    if (stats == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    if (!heap.initialized) {
        return STATUS_ERROR_NOT_INITIALIZED;
    }

    stats->heap_total  = heap.total;
    stats->heap_used   = heap.used;
    stats->heap_free   = (size_t)(heap.end - heap.current);
    stats->alloc_count = heap.alloc_count;
    stats->peak_usage  = heap.peak;

    return STATUS_OK;
}
