/**
 * @file mem.c
 * @brief Memory Manager implementation for MiniOS
 *
 * Bump allocator implementation. All allocations advance a cursor
 * through a contiguous heap region. Individual free is not supported;
 * the entire heap can be reset between model loads.
 *
 * @note Per SRS FR-016 (pre-execution alloc), FR-018 (64-byte align),
 *       FR-020 (usage stats), DC-002 (no dynamic alloc)
 *
 * @complexity Time: O(1) for alloc/free/stats
 */

#include "kernel/mem.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Internal state                                                    */
/* ------------------------------------------------------------------ */

static uint8_t *s_heap_base = NULL;
static uint8_t *s_heap_current = NULL;
static uint8_t *s_heap_end = NULL;
static size_t s_total_size = 0;
static size_t s_peak_usage = 0;
static uint32_t s_alloc_count = 0;
static size_t s_wasted_bytes = 0;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

/**
 * Align a pointer upward to the given alignment.
 * alignment must be a power of 2.
 */
static inline uintptr_t align_up(uintptr_t addr, size_t alignment) {
  return (addr + alignment - 1) & ~(alignment - 1);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status MEM_Init(void *heap_base, size_t heap_size) {
  if (heap_base == NULL || heap_size == 0) {
    return STATUS_ERROR_INVALID_ARGUMENT;
  }

  s_heap_base = (uint8_t *)heap_base;
  s_heap_current = s_heap_base;
  s_heap_end = s_heap_base + heap_size;
  s_total_size = heap_size;
  s_peak_usage = 0;
  s_alloc_count = 0;
  s_wasted_bytes = 0;

  HAL_UART_PutString("[MEM ] Heap initialized at ");
  HAL_UART_PutHex((uint64_t)(uintptr_t)s_heap_base);
  HAL_UART_PutString(" size=");
  HAL_UART_PutDec(heap_size / 1024);
  HAL_UART_PutString(" KB\n");

  return STATUS_OK;
}

void *MEM_Alloc(size_t size, size_t alignment) {
  if (size == 0 || s_heap_base == NULL) {
    return NULL;
  }

  /* Ensure minimum alignment */
  if (alignment < MEM_MIN_ALIGNMENT) {
    alignment = MEM_MIN_ALIGNMENT;
  }

  /* Align the current pointer upward */
  uintptr_t aligned = align_up((uintptr_t)s_heap_current, alignment);
  size_t padding = aligned - (uintptr_t)s_heap_current;
  uint8_t *result = (uint8_t *)aligned;

  /* Check if we have enough space */
  if (result + size > s_heap_end) {
    HAL_UART_PutString("[MEM ] ERROR: Out of memory! Requested ");
    HAL_UART_PutDec(size);
    HAL_UART_PutString(" bytes, available ");
    HAL_UART_PutDec((size_t)(s_heap_end - s_heap_current));
    HAL_UART_PutString("\n");
    return NULL;
  }

  /* Advance the cursor */
  s_heap_current = result + size;
  s_wasted_bytes += padding;
  s_alloc_count++;

  /* Update peak */
  size_t used = (size_t)(s_heap_current - s_heap_base);
  if (used > s_peak_usage) {
    s_peak_usage = used;
  }

  return (void *)result;
}

void *MEM_AllocTensor(size_t size) {
  return MEM_Alloc(size, MEM_DEFAULT_ALIGNMENT);
}

void MEM_Reset(void) {
  s_heap_current = s_heap_base;
  s_alloc_count = 0;
  s_wasted_bytes = 0;
  /* Note: peak_usage is NOT reset — it tracks lifetime peak */
}

size_t MEM_GetFreeBytes(void) {
  if (s_heap_current == NULL || s_heap_end == NULL)
    return 0;
  return (size_t)(s_heap_end - s_heap_current);
}

size_t MEM_GetUsedBytes(void) {
  if (s_heap_current == NULL || s_heap_base == NULL)
    return 0;
  return (size_t)(s_heap_current - s_heap_base);
}

size_t MEM_GetPeakUsage(void) { return s_peak_usage; }

Status MEM_GetStats(MemStats *stats) {
  if (stats == NULL) {
    return STATUS_ERROR_INVALID_ARGUMENT;
  }

  stats->total_bytes = s_total_size;
  stats->used_bytes = MEM_GetUsedBytes();
  stats->free_bytes = MEM_GetFreeBytes();
  stats->peak_used_bytes = s_peak_usage;
  stats->alloc_count = s_alloc_count;
  stats->wasted_bytes = s_wasted_bytes;

  return STATUS_OK;
}

void MEM_PrintStats(void) {
  HAL_UART_PutString("[MEM ] ---- Memory Statistics ----\n");
  HAL_UART_PutString("[MEM ]   Total:     ");
  HAL_UART_PutDec(s_total_size / 1024);
  HAL_UART_PutString(" KB\n");

  HAL_UART_PutString("[MEM ]   Used:      ");
  HAL_UART_PutDec(MEM_GetUsedBytes() / 1024);
  HAL_UART_PutString(" KB\n");

  HAL_UART_PutString("[MEM ]   Free:      ");
  HAL_UART_PutDec(MEM_GetFreeBytes() / 1024);
  HAL_UART_PutString(" KB\n");

  HAL_UART_PutString("[MEM ]   Peak:      ");
  HAL_UART_PutDec(s_peak_usage / 1024);
  HAL_UART_PutString(" KB\n");

  HAL_UART_PutString("[MEM ]   Allocs:    ");
  HAL_UART_PutDec(s_alloc_count);
  HAL_UART_PutString("\n");

  HAL_UART_PutString("[MEM ]   Waste:     ");
  HAL_UART_PutDec(s_wasted_bytes);
  HAL_UART_PutString(" bytes (alignment padding)\n");
}

/* ------------------------------------------------------------------ */
/*  Utility functions                                                 */
/* ------------------------------------------------------------------ */

void MEM_Copy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;

  while (n--) {
    *d++ = *s++;
  }
}

void MEM_Set(void *dst, uint8_t val, size_t n) {
  uint8_t *d = (uint8_t *)dst;

  while (n--) {
    *d++ = val;
  }
}

int MEM_Compare(const void *a, const void *b, size_t n) {
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;

  while (n--) {
    if (*pa != *pb) {
      return (int)*pa - (int)*pb;
    }
    pa++;
    pb++;
  }
  return 0;
}
