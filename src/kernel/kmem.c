/**
 * @file kmem.c
 * @brief Kernel bump allocator implementation
 *
 * NOTE: Initial implementation — alignment calculation is incorrect.
 *       See KMEM_Alloc for the known bug (to be fixed).
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
     * BUG: alignment upward computation is wrong.
     *
     * The intent is to advance `ptr` to the next multiple of `alignment`,
     * but the code below adds `alignment` unconditionally when any
     * misalignment is detected — it should add only the padding needed.
     *
     * Example: ptr=0x09, alignment=8
     *   Correct:  0x09 + (8 - (9 % 8)) = 0x09 + 7 = 0x10
     *   Buggy:    0x09 + 8             = 0x11  (still misaligned!)
     */
    uintptr_t ptr = (uintptr_t)heap.current;
    if ((ptr % alignment) != 0) {
        ptr = ptr + alignment;      /* <-- BUG: should be ptr + (alignment - (ptr % alignment)) */
    }

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
