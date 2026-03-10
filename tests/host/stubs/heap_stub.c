/**
 * @file heap_stub.c
 * @brief Provides fake _heap_start/_heap_end linker arrays for host tests.
 *
 * kmem.c expects:
 *   extern uint8_t _heap_start[];
 *   extern uint8_t _heap_end[];
 *
 * We declare a single large buffer and use linker-script-free sections
 * to ensure _heap_end's address > _heap_start's address.
 *
 * Approach: Place both symbols in dedicated sections so the linker
 * orders them correctly (section "heap_stub_start" < "heap_stub_end").
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define FAKE_HEAP_SIZE  (1024 * 1024)    /* 1 MB */

/*
 * Use two adjacent arrays in the same section to guarantee ordering.
 * The linker within a section appends definitions in source order.
 * We put both in .bss so they end up page-aligned and zeroed.
 */
uint8_t _heap_start[FAKE_HEAP_SIZE] __attribute__((section(".heap_stub")));
uint8_t _heap_end[1]               __attribute__((section(".heap_stub")));

/**
 * Call before each test to zero the heap buffer.
 * _heap_start and _heap_end are fixed array addresses; KMEM_Init reads
 * (uintptr_t)_heap_start and (uintptr_t)_heap_end to derive heap bounds.
 * Since _heap_end is placed immediately after _heap_start[] in .heap_stub,
 * its address equals &_heap_start[FAKE_HEAP_SIZE], giving a 1 MB heap.
 */
void heap_stub_reset(void)
{
    memset(_heap_start, 0, FAKE_HEAP_SIZE);
}
