/**
 * @file timer.c
 * @brief ARM Generic Timer implementation
 *
 * NOTE: HAL_Timer_Enable() has an overflow bug in the TVAL calculation.
 *       See comment inside the function.
 */

#include "hal/timer.h"
#include "hal/uart.h"
#include "types.h"

/* ARM Generic Timer register accessors */
#define READ_CNTFRQ()    ({ uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0"  : "=r"(v)); v; })
#define READ_CNTPCT()    ({ uint64_t v; __asm__ volatile("mrs %0, cntpct_el0"  : "=r"(v)); v; })
#define WRITE_TVAL(v)    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"((uint64_t)(v)))
#define READ_CTL()       ({ uint64_t v; __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(v)); v; })
#define WRITE_CTL(v)     __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)(v)))

static uint64_t timer_freq = 0;

Status HAL_Timer_Init(void)
{
    timer_freq = READ_CNTFRQ();

    if (timer_freq == 0) {
        return STATUS_ERROR_HARDWARE_FAULT;
    }

    /* Disable timer initially */
    WRITE_CTL(0u);
    WRITE_TVAL(0u);

    HAL_UART_PutString("[TMR ] Timer initialised: freq=");
    HAL_UART_PutDec((uint32_t)(timer_freq / 1000000u));
    HAL_UART_PutString(" MHz\n");

    return STATUS_OK;
}

void HAL_Timer_Enable(uint32_t interval_us)
{
    /*
     * BUG: overflow in TVAL calculation.
     *
     * timer_freq is ~62 000 000 (62 MHz).
     * Multiplying by interval_us (e.g. 10 000 for 10 ms) before dividing:
     *   62000000 * 10000 = 620 000 000 000 > UINT32_MAX (4 294 967 295)
     * The cast to uint32_t silently truncates, giving a wildly wrong interval.
     *
     * Fix: use uint64_t arithmetic throughout (see next commit).
     */
    uint32_t tval = (uint32_t)(timer_freq * interval_us) / 1000000u;  /* OVERFLOW */
    WRITE_TVAL((uint64_t)tval);
    WRITE_CTL(1u);  /* ENABLE=1, IMASK=0, ISTATUS: RO */
}

void HAL_Timer_Disable(void)
{
    WRITE_CTL(0u);
}

uint64_t HAL_Timer_GetTicks(void)
{
    return READ_CNTPCT();
}

uint64_t HAL_Timer_GetElapsedUs(uint64_t start_ticks)
{
    uint64_t now  = READ_CNTPCT();
    uint64_t diff = now - start_ticks;
    return (diff * 1000000u) / timer_freq;
}

void HAL_Timer_DelayUs(uint32_t us)
{
    uint64_t start  = READ_CNTPCT();
    uint64_t counts = ((uint64_t)us * timer_freq) / 1000000u;
    while ((READ_CNTPCT() - start) < counts) {
        /* busy wait */
    }
}

void HAL_Timer_Reload(uint32_t interval_us)
{
    /* Same overflow bug as HAL_Timer_Enable — will be fixed together */
    uint32_t tval = (uint32_t)(timer_freq * interval_us) / 1000000u;
    WRITE_TVAL((uint64_t)tval);
}
