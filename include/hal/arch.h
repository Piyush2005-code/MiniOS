/**
 * @file arch.h
 * @brief ARM64 architecture-specific inline helpers for MiniOS
 *
 * Provides low-level CPU control primitives: interrupt masking,
 * memory barriers, cache operations, and power management hints.
 *
 * These are used throughout the kernel for critical sections,
 * synchronization, and hardware interaction.
 */

#ifndef MINIOS_HAL_ARCH_H
#define MINIOS_HAL_ARCH_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  Interrupt Control (DAIF register)                                 */
/*  D=Debug, A=SError, I=IRQ, F=FIQ                                  */
/*  daifset/daifclr immediate: bit 3=D, bit 2=A, bit 1=I, bit 0=F    */
/* ------------------------------------------------------------------ */

/**
 * @brief Enable IRQ interrupts (unmask DAIF.I)
 */
static inline void arch_enable_irq(void)
{
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

/**
 * @brief Disable IRQ interrupts (mask DAIF.I)
 */
static inline void arch_disable_irq(void)
{
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

/**
 * @brief Save current DAIF flags and disable IRQs
 * @return Previous DAIF register value (for later restore)
 */
static inline uint64_t arch_irq_save(void)
{
    uint64_t flags;
    __asm__ volatile(
        "mrs %0, daif\n"
        "msr daifset, #2"
        : "=r"(flags)
        :
        : "memory"
    );
    return flags;
}

/**
 * @brief Restore DAIF flags (re-enable IRQs if they were enabled)
 * @param[in] flags Previously saved DAIF value from arch_irq_save()
 */
static inline void arch_irq_restore(uint64_t flags)
{
    __asm__ volatile("msr daif, %0" :: "r"(flags) : "memory");
}

/* ------------------------------------------------------------------ */
/*  Memory Barriers                                                   */
/* ------------------------------------------------------------------ */

/** Data Synchronization Barrier — full system */
static inline void arch_dsb(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

/** Instruction Synchronization Barrier */
static inline void arch_isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}

/** Data Memory Barrier — full system */
static inline void arch_dmb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

/* ------------------------------------------------------------------ */
/*  Power Management Hints                                            */
/* ------------------------------------------------------------------ */

/** Wait For Event — low-power idle until event or interrupt */
static inline void arch_wfe(void)
{
    __asm__ volatile("wfe");
}

/** Wait For Interrupt — low-power idle until interrupt */
static inline void arch_wfi(void)
{
    __asm__ volatile("wfi");
}

/** Send Event — wake up cores in WFE state */
static inline void arch_sev(void)
{
    __asm__ volatile("sev");
}

/* ------------------------------------------------------------------ */
/*  System Register Accessors                                         */
/* ------------------------------------------------------------------ */

/** Read the current Exception Level (0–3) */
static inline uint32_t arch_get_el(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return (uint32_t)((el >> 2) & 0x3);
}

/** Read MPIDR_EL1 for core identification */
static inline uint64_t arch_get_core_id(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr & 0xFF;
}

#endif /* MINIOS_HAL_ARCH_H */
