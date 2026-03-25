/**
 * @file timer.c
 * @brief ARM Generic Timer implementation for MiniOS
 *
 * Uses the ARM Generic Timer accessible at EL1 via:
 *   CNTPCT_EL0  — Physical counter (read-only)
 *   CNTFRQ_EL0  — Counter frequency (set by firmware)
 *   CNTP_TVAL_EL0 — Timer value (countdown)
 *   CNTP_CTL_EL0  — Timer control (enable/mask/status)
 *
 * The physical timer interrupt is PPI 30, routed through the
 * GIC to generate periodic scheduler ticks.
 *
 * @note Per SRS FR-005: Microsecond-resolution timer services
 *
 * @complexity All operations: Time O(1), Space O(1)
 */

#include "hal/timer.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  System register accessors                                         */
/* ------------------------------------------------------------------ */

static inline uint64_t read_cntpct(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static inline uint64_t read_cntfrq(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline void write_cntp_tval(uint64_t val)
{
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(val));
}

static inline void write_cntp_ctl(uint64_t val)
{
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(val));
}

static inline uint64_t read_cntp_ctl(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(val));
    return val;
}

/* ------------------------------------------------------------------ */
/*  Timer state                                                       */
/* ------------------------------------------------------------------ */

static uint64_t timer_freq;             /* Counter frequency (Hz) */
static uint64_t tick_interval_counts;   /* Ticks per timer period */
static uint32_t tick_period_us;         /* Timer period in microseconds */
static volatile uint64_t system_ticks;  /* Tick counter (from ISR) */
static timer_callback_t timer_callback; /* User callback */

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status HAL_Timer_Init(void)
{
    /* Read hardware counter frequency */
    timer_freq = read_cntfrq();
    if (timer_freq == 0) {
        HAL_UART_PutString("[TMR ] ERROR: Counter frequency is 0\n");
        return STATUS_ERROR_HARDWARE_FAULT;
    }

    /* Default tick interval: 10ms (100 Hz) */
    tick_period_us = TIMER_DEFAULT_TICK_US;
    tick_interval_counts = (timer_freq * tick_period_us) / 1000000;

    system_ticks = 0;
    timer_callback = NULL;

    /* Timer starts disabled */
    write_cntp_ctl(0);

    HAL_UART_PutString("[TMR ] Timer initialized: freq=");
    HAL_UART_PutDec(timer_freq / 1000000);
    HAL_UART_PutString(" MHz, tick=");
    HAL_UART_PutDec(tick_period_us / 1000);
    HAL_UART_PutString(" ms\n");

    return STATUS_OK;
}

uint64_t HAL_Timer_GetTicks(void)
{
    return read_cntpct();
}

uint64_t HAL_Timer_GetFreq(void)
{
    return timer_freq;
}

uint64_t HAL_Timer_GetElapsedUs(uint64_t start_ticks)
{
    uint64_t now = read_cntpct();
    uint64_t elapsed = now - start_ticks;
    return (elapsed * 1000000) / timer_freq;
}

void HAL_Timer_DelayUs(uint64_t us)
{
    uint64_t start = read_cntpct();
    uint64_t target = (us * timer_freq) / 1000000;

    while ((read_cntpct() - start) < target) {
        /* spin */
    }
}

void HAL_Timer_SetInterval(uint64_t us)
{
    tick_period_us = (uint32_t)us;
    tick_interval_counts = (timer_freq * us) / 1000000;
}

void HAL_Timer_Enable(void)
{
    /* Load countdown value */
    write_cntp_tval(tick_interval_counts);

    /*
     * CNTP_CTL_EL0:
     *   bit 0 (ENABLE)  = 1 → Timer enabled
     *   bit 1 (IMASK)   = 0 → Interrupt not masked
     *   bit 2 (ISTATUS) = read-only
     */
    write_cntp_ctl(1);
}

void HAL_Timer_Disable(void)
{
    write_cntp_ctl(0);
}

void HAL_Timer_AckInterrupt(void)
{
    /* Reload the countdown for next tick */
    write_cntp_tval(tick_interval_counts);
}

void HAL_Timer_HandleIRQ(void)
{
    /* Increment system tick counter */
    system_ticks++;

    /* Acknowledge and reload */
    HAL_Timer_AckInterrupt();

    /* Call user callback if registered */
    if (timer_callback != NULL) {
        timer_callback();
    }
}

void HAL_Timer_SetCallback(timer_callback_t cb)
{
    timer_callback = cb;
}

uint64_t HAL_Timer_GetSystemTicks(void)
{
    return system_ticks;
}

uint32_t HAL_Timer_GetTickPeriodMs(void)
{
    return tick_period_us / 1000;
}
