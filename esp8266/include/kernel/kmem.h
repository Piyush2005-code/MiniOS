/**
 * @file kmem.h
 * @brief Kernel Memory Manager API for MiniOS-ESP8266
 *
 * Provides a simple bump allocator over a fixed 24 KB heap carved from
 * the ESP8266's ~80 KB dRAM. No arena/pool tiers — the original 3-tier
 * design assumed hundreds of MB and is completely impractical here.
 *
 * Same public API names as the ARM64 kmem.h for source compatibility.
 */

#ifndef MINIOS_ESP8266_KERNEL_KMEM_H
#define MINIOS_ESP8266_KERNEL_KMEM_H

#include "types.h"
#include "status.h"
#include "../../user_config.h"

/* ------------------------------------------------------------------ */
/*  Alignment constants (ESP8266 Xtensa — no NEON SIMD)              */
/* ------------------------------------------------------------------ */

#define KMEM_MIN_ALIGN      4    /* Minimum pointer alignment (32-bit) */
#define KMEM_TENSOR_ALIGN   4    /* Tensor buffers (no cache-line need) */

/* ------------------------------------------------------------------ */
/*  Stats structure                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t heap_total;
    uint32_t heap_used;
    uint32_t heap_peak;
    uint32_t alloc_count;
    uint32_t arena_total;
    uint32_t arena_used;
    uint32_t pool_total;
    uint32_t pool_used;
} kmem_stats_t;

/* ------------------------------------------------------------------ */
/*  Opaque arena type (thin wrapper — same name as ARM64 original)   */
/* ------------------------------------------------------------------ */

typedef struct kmem_arena {
    uint8_t *base;
    uint8_t *current;
    uint8_t *end;
    uint32_t total;
    uint32_t used;
} kmem_arena_t;

/* ------------------------------------------------------------------ */
/*  Core Bump Allocator API                                           */
/* ------------------------------------------------------------------ */

Status   KMEM_Init(void);
void    *KMEM_Alloc(uint32_t size, uint32_t alignment);
uint32_t KMEM_GetFreeSpace(void);
void     KMEM_GetStats(kmem_stats_t *stats);

/* ------------------------------------------------------------------ */
/*  Arena Allocator API (thin wrappers — source compatibility)        */
/* ------------------------------------------------------------------ */

kmem_arena_t *KMEM_ArenaCreate(uint32_t size);
void         *KMEM_ArenaAlloc(kmem_arena_t *arena, uint32_t size, uint32_t align);
void          KMEM_ArenaReset(kmem_arena_t *arena);
uint32_t      KMEM_ArenaGetUsed(const kmem_arena_t *arena);
uint32_t      KMEM_ArenaGetTotal(const kmem_arena_t *arena);

/* ------------------------------------------------------------------ */
/*  Tensor convenience wrappers                                       */
/* ------------------------------------------------------------------ */

static inline void *KMEM_TensorAlloc(kmem_arena_t *arena, uint32_t size)
{
    return KMEM_ArenaAlloc(arena, size, KMEM_TENSOR_ALIGN);
}

static inline void *KMEM_TensorAllocZeroed(kmem_arena_t *arena, uint32_t size)
{
    void *p = KMEM_ArenaAlloc(arena, size, KMEM_TENSOR_ALIGN);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (uint32_t i = 0; i < size; i++) b[i] = 0;
    }
    return p;
}

#endif /* MINIOS_ESP8266_KERNEL_KMEM_H */
