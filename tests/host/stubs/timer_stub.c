/**
 * @file timer_stub.c
 * @brief Stub HAL Timer functions for host-side unit tests.
 * GetTicks returns an incrementing counter (+1000 each call).
 */

#include <stdint.h>
#include <stddef.h>
#include "status.h"

static uint64_t fake_ticks = 0;
static uint64_t fake_sys_ticks = 0;
static const uint64_t fake_freq = 62500000ULL;

Status   HAL_Timer_Init(void)               { return STATUS_OK; }
uint64_t HAL_Timer_GetTicks(void)           { fake_ticks += 1000; return fake_ticks; }
uint64_t HAL_Timer_GetFreq(void)            { return fake_freq; }
uint64_t HAL_Timer_GetSystemTicks(void)     { return fake_sys_ticks++; }
uint32_t HAL_Timer_GetTickPeriodMs(void)    { return 10; }
void     HAL_Timer_SetInterval(uint64_t us) { (void)us; }
void     HAL_Timer_Enable(void)             {}
void     HAL_Timer_Disable(void)            {}
void     HAL_Timer_AckInterrupt(void)       {}
void     HAL_Timer_HandleIRQ(void)          {}
void     HAL_Timer_DelayUs(uint64_t us)     { (void)us; }
uint64_t HAL_Timer_GetElapsedUs(uint64_t start) { (void)start; return 1000; }
void     HAL_Timer_SetCallback(void (*cb)(void)) { (void)cb; }

void timer_stub_reset(void)
{
    fake_ticks    = 0;
    fake_sys_ticks = 0;
}
