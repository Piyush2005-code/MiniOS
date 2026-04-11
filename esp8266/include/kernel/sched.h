/**
 * @file sched.h
 * @brief Cooperative Task Scheduler API for MiniOS-ESP8266
 *
 * Implements MiniOS scheduling on the ESP8266 NonOS SDK using the
 * system_os_post / system_os_task mechanism. Because the ESP8266 Wi-Fi
 * stack is cooperative and NOT preemptive, any code that holds the CPU
 * for > ~500 µs risks:
 *   - Beacon misses (Wi-Fi disconnection)
 *   - WDT resets (~26 ms hardware, ~6 s software watchdog)
 *
 * Design:
 *   - A single recurring os_timer fires every SCHED_TICK_MS (20 ms).
 *   - On each tick, the timer callback posts a SCHED_SIG_TICK signal to
 *     a dedicated system_os_task (priority ESP_TASK_PRIO_2).
 *   - The task dequeues the front task callback from a circular ready
 *     queue, runs it for ONE SLOT, then returns, yielding back to the
 *     SDK event loop.
 *   - Long-running work (ONNX inference) must be expressed as a state
 *     machine split across multiple SCHED_Submit() calls.
 *
 * Integration contract:
 *   SCHED_Init()   — call once from user_init(), before Wi-Fi starts
 *   SCHED_Submit() — submit a non-blocking callback (≤ 18 ms budget)
 *   SCHED_Tick()   — only call from the timer ISR cb (handled internally)
 *
 * Maximum tasks in the run queue: SCHED_MAX_TASKS (configurable, default 8)
 *
 * @note No ARM64 context-switch machinery (context.S / cpu_context_switch)
 *       is used here. This file is ESP8266-only.
 */

#ifndef MINIOS_ESP8266_KERNEL_SCHED_H
#define MINIOS_ESP8266_KERNEL_SCHED_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

/** Scheduler tick period in milliseconds.
 *  Must be < 500 ms (ESP8266 WDT limit) and > 10 ms (SDK minimum). */
#define SCHED_TICK_MS           20

/** Maximum number of concurrently enqueued task slots. */
#define SCHED_MAX_TASKS         8

/** ESP8266 system_os_task queue depth (SDK max is 32). */
#define SCHED_QUEUE_DEPTH       8

/** Signal value sent by the timer to the scheduler task. */
#define SCHED_SIG_TICK          0x01U

/** Signal value used for direct posting (immediate run). */
#define SCHED_SIG_POST          0x02U

/* ------------------------------------------------------------------ */
/*  Task callback type                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief User-supplied cooperative task function.
 * @param arg   Opaque argument provided at SCHED_Submit() time.
 *
 * MUST complete in ≤ SCHED_TICK_MS - 2 ms (i.e. ≤ 18 ms for 20 ms tick).
 * Long work must be split into a state machine across multiple invocations.
 */
typedef void (*sched_task_fn_t)(void *arg);

/* ------------------------------------------------------------------ */
/*  Status flags returned by SCHED_GetStatus()                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t ticks;          /**< Total scheduler ticks since SCHED_Init */
    uint32_t tasks_run;      /**< Total task executions */
    uint32_t queue_full_err; /**< Number of SCHED_Submit failures (queue full) */
    uint8_t  queue_depth;    /**< Current number of pending tasks */
} sched_status_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the cooperative scheduler.
 *
 * Must be called from user_init() after HAL_UART_Init() and KMEM_Init()
 * but before any SCHED_Submit() call.
 *
 * Sets up:
 *   - The system_os_task (ESP_TASK_PRIO_2) that runs queued callbacks
 *   - The os_timer that fires every SCHED_TICK_MS and posts SCHED_SIG_TICK
 *
 * @return STATUS_OK on success.
 */
Status SCHED_Init(void);

/**
 * @brief Submit a callback to be run on the next scheduler tick.
 *
 * Thread-safe (can be called from timer callbacks).
 * If the queue is full, the submission is silently dropped and
 * SCHED_GetStatus().queue_full_err is incremented.
 *
 * @param fn   Callback to invoke (must not block / busy-wait).
 * @param arg  Opaque argument forwarded to fn.
 * @return STATUS_OK if submitted, STATUS_ERROR_POOL_EXHAUSTED if queue full.
 */
Status SCHED_Submit(sched_task_fn_t fn, void *arg);

/**
 * @brief Feed the software watchdog.
 *
 * Call from inside a long computation that is safely interruptible.
 * Wraps system_soft_wdt_feed().
 */
void SCHED_FeedWDT(void);

/**
 * @brief Retrieve scheduler statistics.
 * @param out Pointer to caller-allocated sched_status_t.
 */
void SCHED_GetStatus(sched_status_t *out);

/**
 * @brief Return number of tasks currently enqueued.
 */
uint8_t SCHED_GetQueueDepth(void);

#endif /* MINIOS_ESP8266_KERNEL_SCHED_H */
