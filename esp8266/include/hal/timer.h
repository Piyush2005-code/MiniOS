/**
 * @file timer.h
 * @brief Timer HAL API for MiniOS-ESP8266
 *
 * API-compatible with the original ARM64 timer.h, implemented via
 * system_get_time() (µs resolution) and os_timer for periodic callbacks.
 */

#ifndef MINIOS_ESP8266_HAL_TIMER_H
#define MINIOS_ESP8266_HAL_TIMER_H

#include "types.h"
#include "status.h"

typedef void (*timer_callback_t)(void);

Status   HAL_Timer_Init(void);
uint32_t HAL_Timer_GetTicks(void);
uint32_t HAL_Timer_GetFreq(void);
uint32_t HAL_Timer_GetElapsedUs(uint32_t start_ticks);
void     HAL_Timer_DelayUs(uint32_t us);
void     HAL_Timer_DelayMs(uint32_t ms);
void     HAL_Timer_SetInterval(uint32_t us);
void     HAL_Timer_Enable(void);
void     HAL_Timer_Disable(void);
void     HAL_Timer_SetCallback(timer_callback_t cb);
uint32_t HAL_Timer_GetSystemTicks(void);
uint32_t HAL_Timer_GetTickPeriodMs(void);

#endif /* MINIOS_ESP8266_HAL_TIMER_H */
