/**
 * @file gic.c
 * @brief GICv2 interrupt controller driver
 *
 * KNOWN ISSUE (Feb 26): CPU interface initialisation is incomplete.
 * GICC_PMR and GICC_BPR are not configured, so the CPU interface
 * rejects all interrupts regardless of distributor state.
 * Fix pending.
 */

#include "hal/gic.h"
#include "hal/uart.h"
#include "types.h"


Status HAL_GIC_Init(void)
{
    HAL_UART_PutString("[GIC ] Initialising GICv2...\n");

    /* Read number of supported IRQs */
    uint32_t typer   = REG32(GIC_DIST_BASE + GICD_TYPER);
    uint32_t it_lines = (typer & 0x1Fu) + 1u;
    uint32_t max_irqs = it_lines * 32u;

    HAL_UART_PutString("[GIC ] Max IRQs: ");
    HAL_UART_PutDec(max_irqs);
    HAL_UART_PutString("\n");

    /* ---- Distributor setup ---- */
    /* Disable distributor before configuration */
    REG32(GIC_DIST_BASE + GICD_CTLR) = 0;

    /* Set all SPI priorities to 0xA0 (mid-range) */
    for (uint32_t i = 8; i < (max_irqs / 4); i++) {
        REG32(GIC_DIST_BASE + GICD_IPRIORITYR0 + i * 4u) = 0xA0A0A0A0u;
    }

    /* Route all SPIs to CPU 0 */
    for (uint32_t i = 8; i < (max_irqs / 4); i++) {
        REG32(GIC_DIST_BASE + GICD_ITARGETSR0 + i * 4u) = 0x01010101u;
    }

    /* Configure SPIs as level-triggered */
    for (uint32_t i = 2; i < (max_irqs / 16); i++) {
        REG32(GIC_DIST_BASE + GICD_ICFGR0 + i * 4u) = 0x0u;
    }

    /* Enable distributor */
    REG32(GIC_DIST_BASE + GICD_CTLR) = 1u;

    /* ---- CPU interface setup ---- */
    /*
     * TODO: Set GICC_PMR to 0xFF to allow all interrupt priorities through.
     *       Set GICC_BPR to 0 for no preemption grouping.
     *       Without these, the CPU interface will drop all interrupts.
     */

    /* Enable CPU interface */
    REG32(GIC_CPU_BASE + GICC_CTLR) = 1u;

    HAL_UART_PutString("[GIC ] GICv2 initialised (CPU interface incomplete)\n");
    return STATUS_OK;
}

void HAL_GIC_EnableIRQ(uint32_t irq)
{
    uint32_t reg = irq / 32u;
    uint32_t bit = irq % 32u;
    REG32(GIC_DIST_BASE + GICD_ISENABLER0 + reg * 4u) = (1u << bit);
}

void HAL_GIC_DisableIRQ(uint32_t irq)
{
    uint32_t reg = irq / 32u;
    uint32_t bit = irq % 32u;
    REG32(GIC_DIST_BASE + GICD_ICENABLER0 + reg * 4u) = (1u << bit);
}

uint32_t HAL_GIC_Acknowledge(void)
{
    return REG32(GIC_CPU_BASE + GICC_IAR);
}

void HAL_GIC_EndOfInterrupt(uint32_t iar)
{
    REG32(GIC_CPU_BASE + GICC_EOIR) = iar;
}
