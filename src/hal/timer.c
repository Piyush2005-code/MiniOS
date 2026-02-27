/**
 * @file timer.c
 * @brief ARM Generic Timer implementation
 */

#include "hal/timer.h"
#include "hal/uart.h"
#include "types.h"

/* ARM Generic Timer register accessors */
#define READ_CNTFRQ()    ({ uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0"   : "=r"(v)); v; })
#define READ_CNTPCT()    ({ uint64_t v; __asm__ volatile("mrs %0, cntpct_el0"   : "=r"(v)); v; })
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
     * Use uint64_t for the intermediate product so that
     * timer_freq (62 000 000) * interval_us (10 000) does not overflow.
     *
     *   62 000 000 * 10 000 = 620 000 000 000  (needs > 32 bits)
     *   620 000 000 000 / 1 000 000 = 620 000  (fits in 32 bits)
     */
    uint64_t tval = ((uint64_t)timer_freq * (uint64_t)interval_us) / 1000000u;
    WRITE_TVAL(tval);
    WRITE_CTL(1u);
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
    /* Multiply first to preserve precision before dividing */
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
    uint64_t tval = ((uint64_t)timer_freq * (uint64_t)interval_us) / 1000000u;
    WRITE_TVAL(tval);
}
