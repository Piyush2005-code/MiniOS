#include <stdint.h>

#include "kernel/kmem.h"

uint64_t SCHED_GetUptime(void) {
    return 12345ULL;
}

uint64_t DAEMON_GetWallSeconds(void) {
    return 3661ULL;
}

uint32_t SCHED_GetThreadCount(void) {
    return 2U;
}

void KMEM_GetStats(kmem_stats_t *stats) {
    if (stats == NULL) {
        return;
    }
    stats->heap_total = 1024U * 1024U;
    stats->heap_used = 256U * 1024U;
    stats->heap_peak = 300U * 1024U;
    stats->alloc_count = 42U;
    stats->arena_total = 0U;
    stats->arena_used = 0U;
    stats->pool_total = 0U;
    stats->pool_used = 0U;
}
