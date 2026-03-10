/**
 * @file hal/arch.h (host stub)
 * @brief Host-compatible replacement for ARM64 arch.h
 * All functions are no-ops or call extern stubs instead of inline asm.
 */
#ifndef MINIOS_HAL_ARCH_H
#define MINIOS_HAL_ARCH_H

#include <stdint.h>

/* Provided by arch_stub.c */
extern uint64_t arch_irq_save(void);
extern void     arch_irq_restore(uint64_t flags);
extern void     arch_enable_irq(void);
extern void     arch_disable_irq(void);
extern void     arch_wfe(void);
extern void     arch_wfi(void);
extern void     arch_dsb(void);
extern void     arch_isb(void);
extern uint32_t arch_get_el(void);

static inline void arch_dmb_sy(void)   {}
static inline void arch_dsb_sy(void)   {}
static inline void arch_isb_sy(void)   {}

#endif /* MINIOS_HAL_ARCH_H */
