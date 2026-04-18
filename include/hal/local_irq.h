/**
 * @file local_irq.h
 * @brief ARM Local Interrupt Controller driver for Raspberry Pi 4B
 *
 * The BCM2711 (Pi 4B) has an ARM Local Peripheral block at 0xFF800000
 * that handles per-core interrupts that bypass the GIC400 distributor.
 *
 * Critically, the ARM Generic Timer PPIs (nCNTPNSIRQ, nCNTVIRQ, etc.)
 * are routed through THIS controller — NOT through the GIC — on Pi 4B.
 *
 * Register reference:
 *   BCM2711 ARM Peripherals datasheet, Chapter 6 "ARM Peripherals"
 *   https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 *
 * @note Only used when PLATFORM_RPI4 is defined.
 * @note Per SRS FR-004: Hardware interrupt management
 */

#ifndef MINIOS_HAL_LOCAL_IRQ_H
#define MINIOS_HAL_LOCAL_IRQ_H

#include "types.h"
#include "status.h"

#ifdef PLATFORM_RPI4

/* ------------------------------------------------------------------ */
/*  ARM Local Peripheral base address (Pi 4B)                         */
/* ------------------------------------------------------------------ */
#define ARM_LOCAL_BASE              0xFF800000UL

/* ------------------------------------------------------------------ */
/*  Per-core Timer Interrupt Control registers                        */
/*  Offset 0x40 + (core * 4)                                          */
/*  Controls which timer IRQs are forwarded to the CPU interface       */
/* ------------------------------------------------------------------ */
#define ARM_LOCAL_TIMER_CNTL(n)     (ARM_LOCAL_BASE + 0x40U + ((n) * 4U))

/*
 * Bits in ARM_LOCAL_TIMER_CNTL(n):
 *   bit 0: nCNTPSIRQ  — Physical Secure Timer IRQ enable
 *   bit 1: nCNTPNSIRQ — Physical Non-Secure Timer IRQ enable  ← we use this
 *   bit 2: nCNTHPIRQ  — Hypervisor Physical Timer IRQ enable
 *   bit 3: nCNTVIRQ   — Virtual Timer IRQ enable
 */
#define LOCAL_TIMER_CNTL_PHYS_SECURE    (1U << 0)
#define LOCAL_TIMER_CNTL_PHYS_NONSEC    (1U << 1)   /* nCNTPNSIRQ */
#define LOCAL_TIMER_CNTL_HYP_PHYS      (1U << 2)
#define LOCAL_TIMER_CNTL_VIRT          (1U << 3)

/* ------------------------------------------------------------------ */
/*  Per-core IRQ Source registers (read-only)                         */
/*  Offset 0x60 + (core * 4)                                          */
/*  Read to find out which timer fired for core N                      */
/* ------------------------------------------------------------------ */
#define ARM_LOCAL_IRQ_SRC(n)        (ARM_LOCAL_BASE + 0x60U + ((n) * 4U))

/*
 * Bits in ARM_LOCAL_IRQ_SRC(n):
 *   bit 0: nCNTPSIRQ pending
 *   bit 1: nCNTPNSIRQ pending  ← our physical timer
 *   bit 2: nCNTHPIRQ pending
 *   bit 3: nCNTVIRQ pending
 *   bit 4: Mailbox 0 pending
 *   bit 8: GPU IRQ (only core 0 or 1)
 *   bit 9: PMU IRQ
 */
#define LOCAL_IRQ_SRC_PHYS_NONSEC   (1U << 1)   /* nCNTPNSIRQ */

/* ------------------------------------------------------------------ */
/*  Core Mailbox write-set registers (for inter-core signaling)        */
/* ------------------------------------------------------------------ */
#define ARM_LOCAL_MAILBOX_SET(core, mbox) \
    (ARM_LOCAL_BASE + 0x80U + ((core) * 16U) + ((mbox) * 4U))

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the ARM Local Interrupt Controller for core 0
 *
 * Enables the non-secure physical timer IRQ (nCNTPNSIRQ) on core 0.
 * This routes the ARM Generic Timer interrupt through the local
 * controller so it reaches the CPU without going via the GIC.
 *
 * @return STATUS_OK on success
 */
Status HAL_LocalIRQ_Init(void);

/**
 * @brief Check if the non-secure physical timer IRQ is pending on core 0
 *
 * Read-only check of ARM_LOCAL_IRQ_SRC(0) for the PHYS_NONSEC bit.
 *
 * @return true if the generic physical timer IRQ is pending
 */
bool HAL_LocalIRQ_IsTimerPending(void);

/**
 * @brief Read the raw IRQ source register for core N
 *
 * @param[in] core  Core number (0–3)
 * @return Raw value of ARM_LOCAL_IRQ_SRC(core)
 */
uint32_t HAL_LocalIRQ_GetSource(uint32_t core);

#endif /* PLATFORM_RPI4 */

#endif /* MINIOS_HAL_LOCAL_IRQ_H */
