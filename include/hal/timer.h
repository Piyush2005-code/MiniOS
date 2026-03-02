/**
 * @file timer.h
 * @brief ARM Generic Timer interface for MiniOS
 *
 * Provides access to the ARM64 Generic Timer (CNTPCT_EL0)
 * for execution timing, busy-wait delays, and scheduler
 * tick generation.
 *
 * @note Per SRS FR-005 (microsecond resolution timer)
 */

#ifndef MINIOS_HAL_TIMER_H
#define MINIOS_HAL_TIMER_H

#include "status.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the ARM Generic Timer
 *
 * Reads the timer frequency, enables the physical timer,
 * and clears any pending interrupts.
 *
 * @return STATUS_OK on success
 */
Status HAL_Timer_Init(void);

/**
 * @brief Read the current timer tick count
 *
 * Reads CNTPCT_EL0 — a monotonically increasing 64-bit counter.
 *
 * @return Current tick count
 */
uint64_t HAL_Timer_GetTicks(void);

/**
 * @brief Get the timer frequency in Hz
 *
 * Reads CNTFRQ_EL0 — typically 62.5 MHz on QEMU virt.
 *
 * @return Timer frequency in Hz
 */
uint64_t HAL_Timer_GetFreqHz(void);

/**
 * @brief Convert tick count to microseconds
 *
 * @param[in] ticks  Number of timer ticks
 * @return Equivalent time in microseconds
 */
uint64_t HAL_Timer_TicksToUs(uint64_t ticks);

/**
 * @brief Convert microseconds to tick count
 *
 * @param[in] us  Time in microseconds
 * @return Equivalent tick count
 */
uint64_t HAL_Timer_UsToTicks(uint64_t us);

/**
 * @brief Busy-wait for a specified number of microseconds
 *
 * Spins reading the timer counter. Suitable for short delays.
 *
 * @param[in] us  Microseconds to wait
 */
void HAL_Timer_BusyWaitUs(uint64_t us);

/**
 * @brief Set a timer deadline (for scheduler tick)
 *
 * Configures CNTP_TVAL_EL0 so that a timer IRQ fires
 * after the specified number of ticks.
 *
 * @param[in] ticks  Ticks until IRQ fires
 */
void HAL_Timer_SetDeadline(uint64_t ticks);

/**
 * @brief Clear the timer IRQ and re-arm
 *
 * Acknowledges the timer interrupt. Must be called from
 * the IRQ handler before returning.
 */
void HAL_Timer_ClearIRQ(void);

/**
 * @brief Disable the physical timer IRQ
 */
void HAL_Timer_DisableIRQ(void);

/**
 * @brief Enable the physical timer IRQ
 */
void HAL_Timer_EnableIRQ(void);

#endif /* MINIOS_HAL_TIMER_H */
