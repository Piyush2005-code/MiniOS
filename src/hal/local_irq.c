/**
 * @file local_irq.c
 * @brief ARM Local Interrupt Controller driver for Raspberry Pi 4B
 *
 * The BCM2711 ARM Local Peripheral block at 0xFF800000 handles per-core
 * timer interrupts independently of the GIC400. On Pi 4B, the ARM Generic
 * Timer PPI (nCNTPNSIRQ, IRQ 30 in GIC terms) is routed via this block.
 *
 * We must enable the timer IRQ here for core 0 so that the timer
 * interrupt reaches the CPU and is visible in our exception handler.
 *
 * @note Only compiled when PLATFORM_RPI4 is defined.
 * @complexity Time: O(1), Space: O(1)
 */

#ifdef PLATFORM_RPI4

#include "hal/local_irq.h"
#include "hal/uart.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Register access helpers                                           */
/* ------------------------------------------------------------------ */

static inline void local_write(uint32_t addr, uint32_t value)
{
    REG32(addr) = value;
}

static inline uint32_t local_read(uint32_t addr)
{
    return REG32(addr);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status HAL_LocalIRQ_Init(void)
{
    HAL_UART_PutString("[LIRQ] Initializing ARM Local IRQ controller...\n");

    /*
     * Enable nCNTPNSIRQ (non-secure physical timer IRQ) for core 0.
     *
     * ARM_LOCAL_TIMER_CNTL(0) controls which timer events are forwarded
     * as IRQs to core 0's CPU interface. Setting bit 1 enables the
     * non-secure physical timer interrupt — this is CNTP_TVAL_EL0 / IRQ 30
     * in GIC PPI terms.
     *
     * Without this, the timer fires but the interrupt never reaches
     * the CPU on real Pi 4B hardware.
     */
    local_write(ARM_LOCAL_TIMER_CNTL(0), LOCAL_TIMER_CNTL_PHYS_NONSEC);

    HAL_UART_PutString("[LIRQ] Core 0 nCNTPNSIRQ enabled\n");
    HAL_UART_PutString("[LIRQ] TIMER_CNTL(0) = ");
    HAL_UART_PutHex(local_read(ARM_LOCAL_TIMER_CNTL(0)));
    HAL_UART_PutString("\n");

    return STATUS_OK;
}

bool HAL_LocalIRQ_IsTimerPending(void)
{
    return (local_read(ARM_LOCAL_IRQ_SRC(0)) & LOCAL_IRQ_SRC_PHYS_NONSEC) != 0;
}

uint32_t HAL_LocalIRQ_GetSource(uint32_t core)
{
    if (core > 3U) return 0;
    return local_read(ARM_LOCAL_IRQ_SRC(core));
}

#endif /* PLATFORM_RPI4 */
