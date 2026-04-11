/**
 * @file timer_esp.c
 * @brief Timer HAL Implementation — ESP8266 system_get_time() + os_timer
 *
 * Uses the ESP8266 NonOS SDK for:
 *   - HAL_Timer_GetTicks()  → system_get_time() (µs since boot, 32-bit)
 *   - HAL_Timer_DelayUs()   → busy-wait on system_get_time()
 *   - Periodic tick         → os_timer_t for SFU retransmit and shell poll
 *
 * All API names match the ARM64 timer.h for full source compatibility.
 */

#include "hal/timer.h"
#include "hal/uart.h"
#include "types.h"

/* ESP8266 NonOS SDK includes */
#include "osapi.h"
#include "os_type.h"
#include "ets_sys.h"
#include "user_interface.h"

/* ------------------------------------------------------------------ */
/*  Module State                                                      */
/* ------------------------------------------------------------------ */

static os_timer_t        g_tick_timer;
static timer_callback_t  g_callback     = (timer_callback_t)0;
static uint32_t          g_tick_period_ms = 10;  /* default 10ms */
static volatile uint32_t g_system_ticks  = 0;    /* ms-resolution tick count */
static bool              g_timer_active  = false;

/* ------------------------------------------------------------------ */
/*  Internal tick ISR                                                 */
/* ------------------------------------------------------------------ */

static void ICACHE_FLASH_ATTR timer_tick_cb(void *arg)
{
    (void)arg;
    g_system_ticks++;

    if (g_callback) {
        g_callback();
    }
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_Init                                                    */
/* ------------------------------------------------------------------ */

Status ICACHE_FLASH_ATTR HAL_Timer_Init(void)
{
    g_system_ticks   = 0;
    g_tick_period_ms = 10;
    g_callback       = (timer_callback_t)0;
    g_timer_active   = false;

    /* Disarm any existing timer */
    os_timer_disarm(&g_tick_timer);

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_GetTicks                                                */
/* ------------------------------------------------------------------ */

uint32_t HAL_Timer_GetTicks(void)
{
    /* system_get_time() → microseconds since boot */
    return (uint32_t)system_get_time();
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_GetFreq                                                 */
/* ------------------------------------------------------------------ */

uint32_t HAL_Timer_GetFreq(void)
{
    return 1000000UL;  /* system_get_time() has 1 MHz (1µs) resolution */
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_GetElapsedUs                                            */
/* ------------------------------------------------------------------ */

uint32_t HAL_Timer_GetElapsedUs(uint32_t start_ticks)
{
    uint32_t now = HAL_Timer_GetTicks();
    /* Handle 32-bit wrap-around (wraps after ~71 minutes) */
    return now - start_ticks;
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_DelayUs                                                 */
/* ------------------------------------------------------------------ */

void HAL_Timer_DelayUs(uint32_t us)
{
    /* os_delay_us is provided by the ESP8266 SDK for short delays */
    os_delay_us(us);
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_DelayMs                                                 */
/* ------------------------------------------------------------------ */

void HAL_Timer_DelayMs(uint32_t ms)
{
    /* Break into 10ms chunks to allow watchdog feeding */
    while (ms >= 10) {
        os_delay_us(10000);
        ms -= 10;
    }
    if (ms > 0) {
        os_delay_us(ms * 1000);
    }
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_SetInterval                                             */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR HAL_Timer_SetInterval(uint32_t us)
{
    g_tick_period_ms = us / 1000;
    if (g_tick_period_ms == 0) g_tick_period_ms = 1;
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_Enable                                                  */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR HAL_Timer_Enable(void)
{
    if (g_timer_active) {
        os_timer_disarm(&g_tick_timer);
    }

    os_timer_setfn(&g_tick_timer, (os_timer_func_t)timer_tick_cb, NULL);
    /* Repeating timer */
    os_timer_arm(&g_tick_timer, g_tick_period_ms, 1);
    g_timer_active = true;
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_Disable                                                 */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR HAL_Timer_Disable(void)
{
    os_timer_disarm(&g_tick_timer);
    g_timer_active = false;
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_SetCallback                                             */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR HAL_Timer_SetCallback(timer_callback_t cb)
{
    g_callback = cb;
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_GetSystemTicks                                          */
/* ------------------------------------------------------------------ */

uint32_t HAL_Timer_GetSystemTicks(void)
{
    return g_system_ticks;
}

/* ------------------------------------------------------------------ */
/*  HAL_Timer_GetTickPeriodMs                                         */
/* ------------------------------------------------------------------ */

uint32_t HAL_Timer_GetTickPeriodMs(void)
{
    return g_tick_period_ms;
}
