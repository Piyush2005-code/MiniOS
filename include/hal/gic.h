/**
 * @file gic.h
 * @brief GICv2 (Generic Interrupt Controller) driver for MiniOS
 *
 * Minimal GICv2 driver targeting both QEMU virt and Raspberry Pi 4B.
 * Provides interrupt enable/disable, acknowledge, and end-of-interrupt
 * operations needed for timer-driven scheduling and I/O.
 *
 * GICv2 memory map:
 *   QEMU virt:         Distributor 0x08000000, CPU I/F 0x08010000
 *   Raspberry Pi 4B:   GIC400 Distributor 0xFF841000, CPU I/F 0xFF842000
 *
 * @note On Pi 4B, the ARM Generic Timer PPI (IRQ 30) is routed via the
 *       ARM Local Interrupt Controller (see local_irq.h / local_irq.c),
 *       NOT via the GIC distributor. The GIC is still initialized for
 *       any future SPI (shared peripheral) interrupts.
 *
 * @note Per SRS FR-004: Hardware interrupts with configurable priorities
 */

#ifndef MINIOS_HAL_GIC_H
#define MINIOS_HAL_GIC_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  GICv2 Base Addresses (platform-conditional)                       */
/* ------------------------------------------------------------------ */
#ifdef PLATFORM_RPI4
/*
 * BCM2711 (Pi 4B): ARM GIC400 — a GICv2-compatible controller.
 * Physical addresses from BCM2711 ARM Peripherals datasheet, §6.3.
 */
#  define GIC_DIST_BASE     0xFF841000UL    /* GIC400 Distributor */
#  define GIC_CPU_BASE      0xFF842000UL    /* GIC400 CPU Interface */
#else
/*
 * QEMU virt machine GICv2.
 */
#  define GIC_DIST_BASE     0x08000000UL
#  define GIC_CPU_BASE      0x08010000UL
#endif

/* ------------------------------------------------------------------ */
/*  Distributor Register Offsets                                      */
/* ------------------------------------------------------------------ */
#define GICD_CTLR           0x000   /* Distributor Control */
#define GICD_TYPER          0x004   /* Interrupt Controller Type */
#define GICD_ISENABLER      0x100   /* Set-Enable (array, 32 IRQs each) */
#define GICD_ICENABLER      0x180   /* Clear-Enable */
#define GICD_ISPENDR        0x200   /* Set-Pending */
#define GICD_ICPENDR        0x280   /* Clear-Pending */
#define GICD_IPRIORITYR     0x400   /* Priority (byte-accessible) */
#define GICD_ITARGETSR      0x800   /* Target processor (byte-accessible) */
#define GICD_ICFGR          0xC00   /* Configuration (edge/level) */

/* ------------------------------------------------------------------ */
/*  CPU Interface Register Offsets                                    */
/* ------------------------------------------------------------------ */
#define GICC_CTLR           0x000   /* CPU Interface Control */
#define GICC_PMR            0x004   /* Priority Mask */
#define GICC_BPR            0x008   /* Binary Point */
#define GICC_IAR            0x00C   /* Interrupt Acknowledge */
#define GICC_EOIR           0x010   /* End of Interrupt */

/* ------------------------------------------------------------------ */
/*  Well-known IRQ IDs (PPIs on QEMU virt)                            */
/* ------------------------------------------------------------------ */
#define IRQ_TIMER_PHYS      30      /* Non-secure Physical Timer PPI */
#define IRQ_TIMER_VIRT      27      /* Virtual Timer PPI */
#define IRQ_SPURIOUS        1023    /* Spurious interrupt */

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the GICv2 distributor and CPU interface
 *
 * Enables the distributor, sets priority mask to accept all
 * priorities, and enables the CPU interface.
 *
 * @return STATUS_OK on success
 */
Status HAL_GIC_Init(void);

/**
 * @brief Enable a specific interrupt ID
 * @param[in] irq_id  The interrupt ID (0-1019)
 */
void HAL_GIC_EnableIRQ(uint32_t irq_id);

/**
 * @brief Disable a specific interrupt ID
 * @param[in] irq_id  The interrupt ID (0-1019)
 */
void HAL_GIC_DisableIRQ(uint32_t irq_id);

/**
 * @brief Set priority for an interrupt
 * @param[in] irq_id   The interrupt ID
 * @param[in] priority  Priority value (0=highest, 255=lowest)
 */
void HAL_GIC_SetPriority(uint32_t irq_id, uint8_t priority);

/**
 * @brief Acknowledge an interrupt (read IAR)
 * @return The interrupt ID from IAR (check for IRQ_SPURIOUS)
 */
uint32_t HAL_GIC_Acknowledge(void);

/**
 * @brief Signal end of interrupt processing
 * @param[in] irq_id  The value returned by HAL_GIC_Acknowledge()
 */
void HAL_GIC_EndOfInterrupt(uint32_t irq_id);

#endif /* MINIOS_HAL_GIC_H */
