/**
 * @file gic.h
 * @brief GICv2 interrupt controller driver interface
 *
 * Targets the ARM Generic Interrupt Controller v2.  On the QEMU virt
 * machine the GIC is at:
 *   Distributor (GICD): 0x08000000
 *   CPU Interface (GICC): 0x08010000
 *
 * Only one CPU is supported (unikernel).
 */

#ifndef MINIOS_GIC_H
#define MINIOS_GIC_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Base addresses                                                     */
/* ------------------------------------------------------------------ */
#define GIC_DIST_BASE   0x08000000UL
#define GIC_CPU_BASE    0x08010000UL

/* ---- Distributor registers (offsets from GIC_DIST_BASE) ---- */
#define GICD_CTLR        0x000   /* Distributor Control Register        */
#define GICD_TYPER       0x004   /* Interrupt Controller Type Register  */
#define GICD_ISENABLER0  0x100   /* Interrupt Set-Enable (SGIs/PPIs)   */
#define GICD_ISENABLER_N 0x104   /* Interrupt Set-Enable (SPIs, n≥1)   */
#define GICD_ICENABLER0  0x180   /* Interrupt Clear-Enable (SGIs/PPIs)  */
#define GICD_ICENABLER_N 0x184   /* Interrupt Clear-Enable (SPIs, n≥1)  */
#define GICD_IPRIORITYR0 0x400   /* Interrupt Priority Registers        */
#define GICD_ITARGETSR0  0x800   /* Interrupt Target Registers          */
#define GICD_ICFGR0      0xC00   /* Interrupt Configuration Registers   */

/* ---- CPU interface registers (offsets from GIC_CPU_BASE) ---- */
#define GICC_CTLR        0x000   /* CPU Interface Control Register      */
#define GICC_PMR         0x004   /* Interrupt Priority Mask Register    */
#define GICC_BPR         0x008   /* Binary Point Register               */
#define GICC_IAR         0x00C   /* Interrupt Acknowledge Register      */
#define GICC_EOIR        0x010   /* End of Interrupt Register           */

/* Physical timer PPI INTID */
#define IRQ_TIMER_PHYS   30u

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise GICv2 distributor and CPU interface.
 * @return STATUS_OK on success.
 */
Status HAL_GIC_Init(void);

/**
 * @brief Enable forwarding of a specific interrupt ID.
 * @param irq INTID (0–287 on QEMU virt).
 */
void HAL_GIC_EnableIRQ(uint32_t irq);

/**
 * @brief Disable forwarding of a specific interrupt ID.
 * @param irq INTID.
 */
void HAL_GIC_DisableIRQ(uint32_t irq);

/**
 * @brief Acknowledge the highest-priority pending interrupt.
 * @return IAR value (lower 10 bits = INTID).
 */
uint32_t HAL_GIC_Acknowledge(void);

/**
 * @brief Signal end of interrupt handling.
 * @param iar Value returned by HAL_GIC_Acknowledge().
 */
void HAL_GIC_EndOfInterrupt(uint32_t iar);

#endif /* MINIOS_GIC_H */
