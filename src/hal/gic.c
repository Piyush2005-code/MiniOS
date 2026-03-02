/**
 * @file gic.c
 * @brief GICv2 driver implementation for MiniOS
 *
 * Minimal GICv2 (Generic Interrupt Controller v2) driver for
 * QEMU virt machine. Configures distributor and CPU interface
 * to handle the ARM Generic Timer PPI (INTID 30).
 *
 * @note Per SRS FR-004: Hardware interrupt management
 *
 * @complexity All operations: Time O(1), Space O(1)
 */

#include "hal/gic.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Register access helpers                                           */
/* ------------------------------------------------------------------ */

static inline void gic_dist_write(uint32_t offset, uint32_t value)
{
    REG32(GIC_DIST_BASE + offset) = value;
}

static inline uint32_t gic_dist_read(uint32_t offset)
{
    return REG32(GIC_DIST_BASE + offset);
}

static inline void gic_cpu_write(uint32_t offset, uint32_t value)
{
    REG32(GIC_CPU_BASE + offset) = value;
}

static inline uint32_t gic_cpu_read(uint32_t offset)
{
    return REG32(GIC_CPU_BASE + offset);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status HAL_GIC_Init(void)
{
    HAL_UART_PutString("[GIC ] Initializing GICv2...\n");

    /*
     * Step 1: Disable distributor while configuring
     *   GICD_CTLR bit 0 = Enable
     */
    gic_dist_write(GICD_CTLR, 0);

    /*
     * Step 2: Read number of supported interrupts (for info)
     *   GICD_TYPER bits [4:0] = ITLinesNumber
     *   Max IRQs = 32 * (ITLinesNumber + 1)
     */
    uint32_t typer = gic_dist_read(GICD_TYPER);
    uint32_t max_irqs = 32 * ((typer & 0x1F) + 1);

    HAL_UART_PutString("[GIC ] Max IRQs: ");
    HAL_UART_PutDec(max_irqs);
    HAL_UART_PutString("\n");

    /*
     * Step 3: Set default priority for all IRQs to medium (0xA0)
     *   GICD_IPRIORITYR is byte-accessible
     *   We write 32-bit words (4 IRQs at a time)
     */
    for (uint32_t i = 0; i < max_irqs / 4; i++) {
        gic_dist_write(GICD_IPRIORITYR + i * 4, 0xA0A0A0A0);
    }

    /*
     * Step 4: Target all SPIs to CPU 0
     *   GICD_ITARGETSR is byte-accessible
     *   PPIs (0-31) have fixed targets (read-only), skip them
     *   SPIs start at INTID 32
     */
    for (uint32_t i = 32 / 4; i < max_irqs / 4; i++) {
        gic_dist_write(GICD_ITARGETSR + i * 4, 0x01010101);
    }

    /*
     * Step 5: Configure all SPIs as level-triggered
     *   GICD_ICFGR: 2 bits per IRQ, 0b00 = level, 0b10 = edge
     *   PPIs (ICFGR[0-1]) are implementation-defined
     */
    for (uint32_t i = 2; i < max_irqs / 16; i++) {
        gic_dist_write(GICD_ICFGR + i * 4, 0);
    }

    /*
     * Step 6: Enable distributor
     */
    gic_dist_write(GICD_CTLR, 1);

    /*
     * Step 7: Configure CPU interface
     *   GICC_PMR = 0xFF → accept all priority levels
     *   GICC_BPR = 0    → all priority bits used for preemption
     *   GICC_CTLR = 1   → enable CPU interface
     */
    gic_cpu_write(GICC_PMR, 0xFF);
    gic_cpu_write(GICC_BPR, 0);
    gic_cpu_write(GICC_CTLR, 1);

    HAL_UART_PutString("[GIC ] GICv2 initialized\n");
    return STATUS_OK;
}

void HAL_GIC_EnableIRQ(uint32_t irq_id)
{
    if (irq_id >= 1020) return;

    uint32_t reg_idx = irq_id / 32;
    uint32_t bit     = irq_id % 32;

    gic_dist_write(GICD_ISENABLER + reg_idx * 4, (1U << bit));
}

void HAL_GIC_DisableIRQ(uint32_t irq_id)
{
    if (irq_id >= 1020) return;

    uint32_t reg_idx = irq_id / 32;
    uint32_t bit     = irq_id % 32;

    gic_dist_write(GICD_ICENABLER + reg_idx * 4, (1U << bit));
}

void HAL_GIC_SetPriority(uint32_t irq_id, uint8_t priority)
{
    if (irq_id >= 1020) return;

    /*
     * GICD_IPRIORITYR is byte-accessible.
     * We read the 32-bit word, modify the target byte, write back.
     */
    uint32_t word_offset = (irq_id / 4) * 4;
    uint32_t byte_shift  = (irq_id % 4) * 8;

    uint32_t val = gic_dist_read(GICD_IPRIORITYR + word_offset);
    val &= ~(0xFFU << byte_shift);
    val |= ((uint32_t)priority << byte_shift);
    gic_dist_write(GICD_IPRIORITYR + word_offset, val);
}

uint32_t HAL_GIC_Acknowledge(void)
{
    return gic_cpu_read(GICC_IAR);
}

void HAL_GIC_EndOfInterrupt(uint32_t irq_id)
{
    gic_cpu_write(GICC_EOIR, irq_id);
}
