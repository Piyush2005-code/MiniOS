/**
 * @file arch_stub.c
 * @brief Stub arch functions for host compilation.
 * arch.h uses ARM64 inline asm which doesn't compile on x86_64.
 * These out-of-line stubs match arch.h's signatures but are no-ops.
 */

#include <stdint.h>

uint64_t arch_irq_save(void)          { return 0; }
void     arch_irq_restore(uint64_t f) { (void)f; }
void     arch_enable_irq(void)        {}
void     arch_disable_irq(void)       {}
void     arch_wfe(void)               {}
void     arch_wfi(void)               {}
void     arch_dsb(void)               {}
void     arch_isb(void)               {}
uint32_t arch_get_el(void)            { return 1; }
