/**
 * @file timer.h
 * @brief ARM Generic Timer interface
 *
 * Uses the ARM Generic Timer physical counter (CNTPCT_EL0) and
 * physical timer (CNTP_TVAL_EL0 / CNTP_CTL_EL0) accessible from EL1.
 *
 * Timer frequency is read at init from CNTFRQ_EL0.
 * On QEMU virt (cortex-a53) this is typically 62.5 MHz.
 */

#ifndef MINIOS_TIMER_H
#define MINIOS_TIMER_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise the ARM Generic Timer.
 *
 * Reads CNTFRQ_EL0 to determine the counter frequency.
 * Does NOT enable the timer — call HAL_Timer_Enable() separately.
 *
 * @return STATUS_OK on success.
 */
Status HAL_Timer_Init(void);

/**
 * @brief Enable the physical timer and program it to fire in
 *        `interval_us` microseconds.
 *
 * @param interval_us Desired tick interval in microseconds.
 */
void HAL_Timer_Enable(uint32_t interval_us);

/** @brief Disable the physical timer (CNTP_CTL_EL0.ENABLE = 0). */
void HAL_Timer_Disable(void);

/**
 * @brief Read the raw 64-bit counter value (CNTPCT_EL0).
 * @return Counter ticks since reset.
 */
uint64_t HAL_Timer_GetTicks(void);

/**
 * @brief Compute elapsed microseconds since a reference tick value.
 * @param start_ticks Value previously returned by HAL_Timer_GetTicks().
 * @return Elapsed time in microseconds.
 */
uint64_t HAL_Timer_GetElapsedUs(uint64_t start_ticks);

/**
 * @brief Busy-wait for at least `us` microseconds.
 * @param us Delay in microseconds.
 */
void HAL_Timer_DelayUs(uint32_t us);

/**
 * @brief Reload the timer with `interval_us` microseconds from now.
 *
 * Call this inside the IRQ handler to make the timer periodic.
 *
 * @param interval_us Next interval in microseconds.
 */
void HAL_Timer_Reload(uint32_t interval_us);

#endif /* MINIOS_TIMER_H */
