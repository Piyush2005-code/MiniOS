/**
 * @file timer.h
 * @brief Timer HAL API for MiniOS-ESP8266
 *
 * API-compatible with the original ARM64 timer.h, implemented via the
 * ESP8266 system tick counter (system_get_time() returns microseconds
 * since boot) and os_timer for periodic callbacks.
 */

#ifndef MINIOS_ESP8266_HAL_TIMER_H
#define MINIOS_ESP8266_HAL_TIMER_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Callback type                                                     */
/* ------------------------------------------------------------------ */

typedef void (*timer_callback_t)(void);

/* ------------------------------------------------------------------ */
/*  Public API (mirrors ARM64 original)                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the timer subsystem.
 *        Reads system clock frequency (80 MHz), zeros tick counter.
 */
Status HAL_Timer_Init(void);

/**
 * @brief Get raw hardware tick count (microsecond resolution).
 *        Uses system_get_time() — wraps every ~71 minutes.
 * @return Microseconds since boot (32-bit on ESP8266).
 */
uint32_t HAL_Timer_GetTicks(void);

/**
 * @brief Get timer frequency in Hz.
 * @return 1,000,000 (1 MHz — system_get_time resolution).
 */
uint32_t HAL_Timer_GetFreq(void);

/**
 * @brief Compute elapsed microseconds since a start tick.
 * @param start_ticks  Value returned by a prior HAL_Timer_GetTicks().
 * @return Elapsed microseconds.
 */
uint32_t HAL_Timer_GetElapsedUs(uint32_t start_ticks);

/**
 * @brief Busy-wait for the specified number of microseconds.
 *        Uses a spin loop on system_get_time(). Max ~71 minutes.
 * @param us Delay in microseconds.
 */
void HAL_Timer_DelayUs(uint32_t us);

/**
 * @brief Convenience millisecond delay.
 * @param ms Delay in milliseconds.
 */
void HAL_Timer_DelayMs(uint32_t ms);

/**
 * @brief Set the periodic tick interval (used for SFU retransmit timer).
 * @param us Interval in microseconds.
 */
void HAL_Timer_SetInterval(uint32_t us);

/**
 * @brief Enable the periodic os_timer.
 */
void HAL_Timer_Enable(void);

/**
 * @brief Disable the periodic os_timer.
 */
void HAL_Timer_Disable(void);

/**
 * @brief Register a function to be called on each timer tick.
 * @param cb Callback function pointer.
 */
void HAL_Timer_SetCallback(timer_callback_t cb);

/**
 * @brief Get the monotonic system tick count (millisecond resolution).
 * @return Milliseconds since boot.
 */
uint32_t HAL_Timer_GetSystemTicks(void);

/**
 * @brief Get the configured tick period in milliseconds.
 * @return Tick period in ms.
 */
uint32_t HAL_Timer_GetTickPeriodMs(void);

#endif /* MINIOS_ESP8266_HAL_TIMER_H */
