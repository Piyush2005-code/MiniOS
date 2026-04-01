/**
 * @file daemon.h
 * @brief Background daemon subsystem for MiniOS
 *
 * Daemons are long-lived, low-priority cooperative threads that run
 * periodic housekeeping tasks. They are registered before SCHED_Start()
 * and sleep between activations using THREAD_Sleep().
 *
 * Built-in daemons:
 *   - clock_daemon    : wall-clock second counter (1 000 ms period)
 *   - memwatch_daemon : heap usage monitor / high-watermark warning (500 ms)
 *   - runtime_daemon  : uptime + thread-count reporter (2 000 ms)
 *
 * @note Per SRS: Cooperative execution model; daemons run at LOW priority
 *       so they never preempt inference workloads.
 */

#ifndef MINIOS_KERNEL_DAEMON_H
#define MINIOS_KERNEL_DAEMON_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Daemon wake periods (milliseconds)                                */
/* ------------------------------------------------------------------ */

/** Wall-clock daemon period: tick every 1 second */
#define DAEMON_CLOCK_PERIOD_MS      1000U

/** Memory watchdog period: sample heap every 500 ms */
#define DAEMON_MEMWATCH_PERIOD_MS    500U

/** Runtime monitor period: print stats every 2 seconds */
#define DAEMON_RUNTIME_PERIOD_MS    2000U

/** Heap usage threshold that triggers a UART warning (0-100 %) */
#define DAEMON_MEM_WARN_PERCENT      80U

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Create and register all built-in background daemons.
 *
 * Must be called AFTER SCHED_Init() and BEFORE SCHED_Start().
 * Creates three daemon threads at THREAD_PRIORITY_LOW.
 *
 * @return STATUS_OK on success
 *         STATUS_ERROR_OUT_OF_MEMORY if heap is too small for stacks
 *         STATUS_ERROR_THREAD_LIMIT  if thread table is full
 */
Status DAEMON_RegisterAll(void);

/**
 * @brief Get the wall-clock seconds elapsed since boot.
 *
 * Updated by clock_daemon every DAEMON_CLOCK_PERIOD_MS.
 * Resolution is 1 second; use SCHED_GetUptime() for milliseconds.
 *
 * @return Seconds since boot
 */
uint64_t DAEMON_GetWallSeconds(void);

#endif /* MINIOS_KERNEL_DAEMON_H */
