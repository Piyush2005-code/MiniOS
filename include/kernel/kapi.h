/**
 * @file kapi.h
 * @brief MiniOS Kernel API — Unified system interface
 *
 * This is the single header that ML runtime code includes to
 * access all kernel services. In a unikernel, there are no
 * system calls — all services are direct C function calls
 * with zero overhead.
 *
 * Usage:
 *   #include "kernel/kapi.h"
 *
 * This gives access to:
 *   - Timer:    HAL_Timer_*
 *   - Memory:   MEM_*
 *   - Scheduler: SCHED_*
 *   - UART I/O: HAL_UART_*
 *   - MMU:      HAL_MMU_*
 *   - IRQ control: KAPI_IRQ_*
 *   - Diagnostics: KAPI_Panic(), KAPI_Log()
 *   - Performance:  KAPI_Perf_*
 */

#ifndef MINIOS_KERNEL_KAPI_H
#define MINIOS_KERNEL_KAPI_H

/* ------------------------------------------------------------------ */
/*  Re-export all subsystem headers                                   */
/* ------------------------------------------------------------------ */

#include "hal/mmu.h"
#include "hal/timer.h"
#include "hal/uart.h"
#include "kernel/mem.h"
#include "kernel/sched.h"
#include "status.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  IRQ Control (inline — zero overhead)                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Disable all IRQs (mask DAIF.I)
 */
static inline void KAPI_IRQ_Disable(void) {
  __asm__ volatile("msr daifset, #2" ::: "memory");
}

/**
 * @brief Enable IRQs (unmask DAIF.I)
 */
static inline void KAPI_IRQ_Enable(void) {
  __asm__ volatile("msr daifclr, #2" ::: "memory");
}

/**
 * @brief Save current IRQ state and disable IRQs
 *
 * Use with KAPI_IRQ_Restore() for safe critical sections.
 *
 * @return Saved DAIF register value
 */
static inline uint64_t KAPI_IRQ_SaveAndDisable(void) {
  uint64_t flags;
  __asm__ volatile("mrs %0, daif" : "=r"(flags));
  __asm__ volatile("msr daifset, #2" ::: "memory");
  return flags;
}

/**
 * @brief Restore previously saved IRQ state
 *
 * @param[in] flags  Value returned by KAPI_IRQ_SaveAndDisable()
 */
static inline void KAPI_IRQ_Restore(uint64_t flags) {
  __asm__ volatile("msr daif, %0" ::"r"(flags) : "memory");
}

/* ------------------------------------------------------------------ */
/*  Cache Control                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Flush all caches (data barrier + instruction barrier)
 */
static inline void KAPI_Cache_FlushAll(void) {
  HAL_MMU_CleanInvalidateDCache();
}

/* ------------------------------------------------------------------ */
/*  Diagnostics                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Kernel panic — print reason and halt forever
 *
 * @param[in] reason  Human-readable panic message
 */
static inline void KAPI_Panic(const char *reason) {
  HAL_UART_PutString("\n\n!!! KERNEL PANIC !!!\n");
  HAL_UART_PutString("Reason: ");
  HAL_UART_PutString(reason);
  HAL_UART_PutString("\n\nSystem halted.\n");

  /* Disable interrupts and halt */
  __asm__ volatile("msr daifset, #15");
  while (1) {
    __asm__ volatile("wfe");
  }
}

/**
 * @brief Print a log message with module prefix
 *
 * @param[in] module  Module name (e.g., "ONNX", "NET")
 * @param[in] msg     Log message
 */
static inline void KAPI_Log(const char *module, const char *msg) {
  HAL_UART_PutString("[");
  HAL_UART_PutString(module);
  HAL_UART_PutString("] ");
  HAL_UART_PutString(msg);
  HAL_UART_PutString("\n");
}

/* ------------------------------------------------------------------ */
/*  Performance Regions                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Record the start time of a named performance region
 *
 * This is a lightweight wrapper that prints timing info.
 * For production, replace with ring-buffer telemetry.
 *
 * @param[in] name  Region name (e.g., "MatMul", "Conv2D")
 * @return Start tick value (pass to KAPI_Perf_EndRegion)
 */
static inline uint64_t KAPI_Perf_StartRegion(const char *name) {
  (void)name; /* Name only used in end region */
  return HAL_Timer_GetTicks();
}

/**
 * @brief Record the end time and print elapsed microseconds
 *
 * @param[in] name        Region name
 * @param[in] start_tick  Value returned by KAPI_Perf_StartRegion
 */
static inline void KAPI_Perf_EndRegion(const char *name, uint64_t start_tick) {
  uint64_t elapsed = HAL_Timer_GetTicks() - start_tick;
  uint64_t us = HAL_Timer_TicksToUs(elapsed);

  HAL_UART_PutString("[PERF] ");
  HAL_UART_PutString(name);
  HAL_UART_PutString(": ");
  HAL_UART_PutDec(us);
  HAL_UART_PutString(" us\n");
}

#endif /* MINIOS_KERNEL_KAPI_H */
