/**
 * @file arch.h
 * @brief ARM64 architecture helpers (inline assembly)
 *
 * Provides minimal inline wrappers for ARM64 system registers and
 * common synchronisation primitives needed by the kernel.
 *
 * All functions are marked static inline and incur zero overhead
 * compared to writing the assembly by hand.
 */

#ifndef MINIOS_ARCH_H
#define MINIOS_ARCH_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  IRQ control                                                        */
/* ------------------------------------------------------------------ */

/** Enable IRQ exceptions at the current EL. */
static inline void arch_enable_irq(void)
{
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

/** Disable IRQ exceptions at the current EL. */
static inline void arch_disable_irq(void)
{
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

/**
 * @brief Save IRQ state and disable IRQs.
 * @return Saved DAIF value; pass to arch_irq_restore().
 */
static inline uint64_t arch_irq_save(void)
{
    uint64_t flags;
    __asm__ volatile(
        "mrs %0, daif   \n"
        "msr daifset, #2\n"
        : "=r"(flags) :: "memory"
    );
    return flags;
}

/**
 * @brief Restore previously saved IRQ state.
 * @param flags Value returned by arch_irq_save().
 */
static inline void arch_irq_restore(uint64_t flags)
{
    __asm__ volatile("msr daif, %0" :: "r"(flags) : "memory");
}

/* ------------------------------------------------------------------ */
/*  Memory barriers                                                    */
/* ------------------------------------------------------------------ */

/** Data Synchronisation Barrier — wait for all memory accesses. */
static inline void arch_dsb(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

/** Instruction Synchronisation Barrier — flush pipeline. */
static inline void arch_isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}

/* ------------------------------------------------------------------ */
/*  System register reads                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Read the current exception level.
 * @return 1, 2, or 3.
 */
static inline uint32_t arch_get_el(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return (uint32_t)((el >> 2) & 0x3);
}

#endif /* MINIOS_ARCH_H */
