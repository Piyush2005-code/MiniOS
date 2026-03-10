/**
 * @file timer.h
 * @brief ARM Generic Timer interface for MiniOS
 *
 * Provides microsecond-resolution timing using the ARM Generic Timer
 * (CNTPCT_EL0 / CNTP_*_EL0 registers). Used for:
 *   - Execution timing measurement (per SRS FR-005, FR-012)
 *   - Scheduler tick generation (periodic interrupt)
 *   - Delay functions for hardware initialization
 *
 * The physical timer interrupt (PPI 30) is routed through the GIC
 * to drive the scheduler's periodic tick.
 *
 * @note Per SRS FR-005: Timer services with microsecond resolution
 */

#ifndef MINIOS_HAL_TIMER_H
#define MINIOS_HAL_TIMER_H

#include "types.h"
#include "status.h"

/* Default tick period: 10ms (100 Hz) */
#define TIMER_DEFAULT_TICK_US   10000

/* ------------------------------------------------------------------ */
/*  Timer callback type                                               */
/* ------------------------------------------------------------------ */
typedef void (*timer_callback_t)(void);

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the ARM Generic Timer
 *
 * Reads counter frequency, configures default tick interval.
 * Timer starts disabled — call HAL_Timer_Enable() to start ticks.
 *
 * @return STATUS_OK on success
 */
Status HAL_Timer_Init(void);

/**
 * @brief Read the current counter value
 * @return Raw counter ticks since boot
 */
uint64_t HAL_Timer_GetTicks(void);

/**
 * @brief Get the timer counter frequency
 * @return Frequency in Hz (ticks per second)
 */
uint64_t HAL_Timer_GetFreq(void);

/**
 * @brief Compute elapsed microseconds since a start tick
 * @param[in] start_ticks  Value from HAL_Timer_GetTicks() at start
 * @return Elapsed time in microseconds
 */
uint64_t HAL_Timer_GetElapsedUs(uint64_t start_ticks);

/**
 * @brief Busy-wait delay for a specified number of microseconds
 * @param[in] us  Delay duration in microseconds
 */
void HAL_Timer_DelayUs(uint64_t us);

/**
 * @brief Set the periodic tick interval
 * @param[in] us  Tick interval in microseconds
 */
void HAL_Timer_SetInterval(uint64_t us);

/**
 * @brief Enable the timer and start generating interrupts
 */
void HAL_Timer_Enable(void);

/**
 * @brief Disable the timer (stop interrupts)
 */
void HAL_Timer_Disable(void);

/**
 * @brief Acknowledge a timer interrupt and reload the countdown
 *
 * Must be called from the IRQ handler when a timer interrupt fires.
 */
void HAL_Timer_AckInterrupt(void);

/**
 * @brief Handle timer IRQ (called from IRQ dispatcher)
 *
 * Acknowledges the interrupt, invokes the registered callback,
 * and reloads the timer for the next tick.
 */
void HAL_Timer_HandleIRQ(void);

/**
 * @brief Register a callback for periodic timer ticks
 * @param[in] cb  Function to call on each tick (NULL to clear)
 */
void HAL_Timer_SetCallback(timer_callback_t cb);

/**
 * @brief Get the number of timer ticks elapsed since timer init
 * @return System tick count (incremented each timer interrupt)
 */
uint64_t HAL_Timer_GetSystemTicks(void);

/**
 * @brief Get tick interval in milliseconds
 * @return Tick period in ms
 */
uint32_t HAL_Timer_GetTickPeriodMs(void);

#endif /* MINIOS_HAL_TIMER_H */
