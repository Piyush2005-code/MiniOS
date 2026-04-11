/**
 * @file sched_esp.c
 * @brief Cooperative Task Scheduler — MiniOS-ESP8266
 *
 * -----------------------------------------------------------------------
 * WHY THIS EXISTS
 * -----------------------------------------------------------------------
 * The ARM64 MiniOS scheduler uses real context switching (cpu_context_switch
 * in context.S), a per-thread stack, and WFE/WFI for idle.  None of that
 * works on the ESP8266 because:
 *
 *   1. The NonOS SDK owns the "main loop".  user_init() must RETURN so the
 *      SDK can service the Wi-Fi state machine, TCP/IP timers, and espconn
 *      callbacks.  Blocking in a while(1) = instant WDT reset.
 *
 *   2. The hardware Watchdog (HW WDT) triggers after ~1.6 s of uninterrupted
 *      execution.  The Software WDT fires first at ~6 s.  Neither can be
 *      disabled in production firmware.
 *
 *   3. The Xtensa LX106 has no OS-grade context-switch support and no MMU.
 *
 * -----------------------------------------------------------------------
 * DESIGN — "Tick-Post-Run" cooperative model
 * -----------------------------------------------------------------------
 *
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │  os_timer (SCHED_TICK_MS = 20 ms, repeating)                   │
 *  │      └─► sched_tick_cb()                                        │
 *  │              └─► system_os_post(SCHED_TASK_PRIO,               │
 *  │                                 SCHED_SIG_TICK, 0)              │
 *  └─────────────────────────────────────────────────────────────────┘
 *            ▼  (SDK delivers to task when event loop is idle)
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │  system_os_task @ ESP_TASK_PRIO_2                               │
 *  │      └─► sched_task_runner()                                    │
 *  │              ├─ dequeue ONE callback from g_run_queue           │
 *  │              ├─ execute callback(arg)   [≤ 18 ms budget]        │
 *  │              └─ return  →  SDK resumes Wi-Fi stack              │
 *  └─────────────────────────────────────────────────────────────────┘
 *
 * Each tick at most ONE queued callback is dispatched.  If the queue has
 * multiple entries they drain across successive ticks (one per 20 ms).
 * Callbacks that want to re-run simply call SCHED_Submit(themselves, arg)
 * at the end of their body.
 *
 * -----------------------------------------------------------------------
 * TIMING BUDGET
 * -----------------------------------------------------------------------
 *   SCHED_TICK_MS = 20 ms
 *   Max callback wall time: ≤ 18 ms  (2 ms headroom for SDK queue overhead)
 *   Wi-Fi beacon interval:  typically 100 ms — we never miss one.
 *   HW WDT margin:          1600 ms  — 80× our tick period, fully safe.
 *
 * For ONNX inference (which can take 200 ms+ for a TinyMLP on LX106), the
 * caller must split work into operator-level slices and use the state machine
 * pattern demonstrated below.
 *
 * -----------------------------------------------------------------------
 * MEMORY FOOTPRINT
 * -----------------------------------------------------------------------
 *   g_run_queue[SCHED_MAX_TASKS] × 8 bytes = 64 bytes dRAM (default)
 *   g_tick_timer                           = 28 bytes dRAM (os_timer_t)
 *   g_msg_queue[SCHED_QUEUE_DEPTH]         = 32 bytes dRAM (os_event_t)
 *   Module state struct                    = 20 bytes dRAM
 *   TOTAL                                  ≈ 144 bytes dRAM
 */

#include "kernel/sched.h"
#include "hal/uart.h"
#include "types.h"

/* ESP8266 NonOS SDK headers */
#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"
#include "user_interface.h"

/* ------------------------------------------------------------------ */
/*  Private types                                                      */
/* ------------------------------------------------------------------ */

/** One slot in the circular ready queue. */
typedef struct {
    sched_task_fn_t fn;   /**< Callback to invoke, NULL = empty slot */
    void           *arg;  /**< Opaque argument forwarded to fn */
} sched_slot_t;

/* ------------------------------------------------------------------ */
/*  Module-private state                                               */
/* ------------------------------------------------------------------ */

/** Circular FIFO task queue (head = next to run, tail = next free). */
static sched_slot_t g_run_queue[SCHED_MAX_TASKS];
static volatile uint8_t g_q_head = 0;  /* index of next task to run   */
static volatile uint8_t g_q_tail = 0;  /* index of next free slot      */
static volatile uint8_t g_q_len  = 0;  /* current occupancy             */

/** os_timer that fires every SCHED_TICK_MS to drive the loop. */
static os_timer_t g_sched_tick_timer;

/**
 * SDK message queue backing store for system_os_task.
 * Must be statically allocated for the lifetime of the task.
 */
static os_event_t g_msg_queue[SCHED_QUEUE_DEPTH];

/** Statistics counters (never overflow in practice). */
static volatile uint32_t g_ticks         = 0;
static volatile uint32_t g_tasks_run     = 0;
static volatile uint32_t g_queue_full_err = 0;

/** Scheduler initialised flag. */
static bool g_sched_ready = false;

/* ------------------------------------------------------------------ */
/*  Priority level for the scheduler system_os_task                   */
/*                                                                     */
/*  ESP8266 NonOS SDK defines three user-reachable task priorities:    */
/*    ESP_TASK_PRIO_0  (lowest, 0)                                     */
/*    ESP_TASK_PRIO_1  (middle, 1)                                     */
/*    ESP_TASK_PRIO_2  (highest user, 2)                               */
/*                                                                     */
/*  We use PRIO_2 so the scheduler runs before background SDK work.   */
/* ------------------------------------------------------------------ */

#define SCHED_TASK_PRIO   ESP_TASK_PRIO_2

/* ------------------------------------------------------------------ */
/*  Internal: run-queue enqueue (called from timer cb — no nesting)   */
/* ------------------------------------------------------------------ */

/**
 * @brief Enqueue one task slot.
 * @return true if enqueued, false if queue full.
 *
 * Called from sched_tick_cb() which executes in SDK timer context
 * (not preempted by itself, but be careful about re-entrancy from
 * within a running callback calling SCHED_Submit).
 */
static IRAM_ATTR bool rq_enqueue(sched_task_fn_t fn, void *arg)
{
    if (g_q_len >= SCHED_MAX_TASKS) {
        g_queue_full_err++;
        return false;
    }

    g_run_queue[g_q_tail].fn  = fn;
    g_run_queue[g_q_tail].arg = arg;
    g_q_tail = (uint8_t)((g_q_tail + 1U) % SCHED_MAX_TASKS);
    g_q_len++;
    return true;
}

/**
 * @brief Dequeue the front task slot.
 * @param[out] out_fn  Callback pointer written on success.
 * @param[out] out_arg Argument pointer written on success.
 * @return true if a task was dequeued, false if queue empty.
 */
static IRAM_ATTR bool rq_dequeue(sched_task_fn_t *out_fn, void **out_arg)
{
    if (g_q_len == 0) {
        return false;
    }

    *out_fn  = g_run_queue[g_q_head].fn;
    *out_arg = g_run_queue[g_q_head].arg;
    g_run_queue[g_q_head].fn  = (sched_task_fn_t)0;
    g_run_queue[g_q_head].arg = (void *)0;
    g_q_head = (uint8_t)((g_q_head + 1U) % SCHED_MAX_TASKS);
    g_q_len--;
    return true;
}

/* ------------------------------------------------------------------ */
/*  SDK system_os_task runner                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Task function executed by the SDK event loop.
 *
 * This is the heart of the scheduler.  The SDK calls this function when
 * a message is pending in g_msg_queue.  We dequeue ONE slot from our
 * run-queue per invocation, execute it, then return.
 *
 * Returning control to the SDK gives the Wi-Fi stack time to process
 * beacons, ACKs, and retransmits before we're called again next tick.
 *
 * @param e  SDK event message (we only use e->sig to distinguish signals).
 */
static void ICACHE_FLASH_ATTR sched_task_runner(os_event_t *e)
{
    if (e->sig != SCHED_SIG_TICK && e->sig != SCHED_SIG_POST) {
        return; /* Unknown signal — ignore */
    }

    sched_task_fn_t fn  = (sched_task_fn_t)0;
    void           *arg = (void *)0;

    if (rq_dequeue(&fn, &arg)) {
        if (fn != (sched_task_fn_t)0) {
            fn(arg);           /* ← user callback, must return within 18 ms */
            g_tasks_run++;
        }
    }

    /*
     * If the queue is not empty after running one task, post another
     * signal immediately so the SDK delivers another sched_task_runner
     * invocation as soon as it can (without waiting for the next tick).
     * This drains burst submissions quickly without blocking Wi-Fi.
     */
    if (g_q_len > 0) {
        system_os_post(SCHED_TASK_PRIO, SCHED_SIG_POST, 0);
    }
}

/* ------------------------------------------------------------------ */
/*  Timer callback — fires every SCHED_TICK_MS                        */
/* ------------------------------------------------------------------ */

/**
 * @brief os_timer callback: advance tick counter and signal the task.
 *
 * Runs in the SDK "soft interrupt" / deferred-ISR context.
 * MUST be very short — just post a signal.  Never call user code here.
 *
 * The IRAM_ATTR placement ensures this runs from iRAM even when the
 * SPI Flash is being read (iCache miss scenario during heavy ONNX work).
 */
static void IRAM_ATTR sched_tick_cb(void *arg)
{
    (void)arg;
    g_ticks++;

    /*
     * system_os_post() is safe to call from timer cb context.
     * It drops the message silently if the SDK queue is full —
     * that is acceptable; the next tick will fire in 20 ms.
     */
    system_os_post(SCHED_TASK_PRIO, SCHED_SIG_TICK, 0);
}

/* ------------------------------------------------------------------ */
/*  Public API implementation                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the cooperative scheduler.
 *
 * Steps:
 *  1. Zero the run-queue.
 *  2. Register a system_os_task at SCHED_TASK_PRIO with our runner.
 *  3. Arm a repeating os_timer for SCHED_TICK_MS.
 */
Status ICACHE_FLASH_ATTR SCHED_Init(void)
{
    /* Zero the run-queue */
    uint8_t i;
    for (i = 0; i < SCHED_MAX_TASKS; i++) {
        g_run_queue[i].fn  = (sched_task_fn_t)0;
        g_run_queue[i].arg = (void *)0;
    }
    g_q_head         = 0;
    g_q_tail         = 0;
    g_q_len          = 0;
    g_ticks          = 0;
    g_tasks_run      = 0;
    g_queue_full_err = 0;

    /*
     * Register the scheduler's system_os_task.
     *
     * system_os_task(task_fn, priority, queue_buf, queue_len)
     *
     * Only ONE task per priority level is allowed.  Using PRIO_2 here
     * means the application must not register another PRIO_2 task.
     */
    system_os_task(sched_task_runner,
                   SCHED_TASK_PRIO,
                   g_msg_queue,
                   SCHED_QUEUE_DEPTH);

    /*
     * Arm the recurring tick timer.
     *
     * os_timer_arm(timer, ms, repeat)
     *   repeat = 1 → fires every SCHED_TICK_MS milliseconds.
     *
     * The timer drives the scheduler rhythm.  Each tick posts one
     * SCHED_SIG_TICK message to sched_task_runner.
     */
    os_timer_disarm(&g_sched_tick_timer);
    os_timer_setfn(&g_sched_tick_timer,
                   (os_timer_func_t)sched_tick_cb,
                   NULL);
    os_timer_arm(&g_sched_tick_timer, SCHED_TICK_MS, 1 /* repeat */);

    g_sched_ready = true;

    HAL_UART_PutString("[SCHED] Cooperative scheduler ready (");
    HAL_UART_PutDec(SCHED_TICK_MS);
    HAL_UART_PutString(" ms tick, max_tasks=");
    HAL_UART_PutDec(SCHED_MAX_TASKS);
    HAL_UART_PutString(")\n");

    return STATUS_OK;
}

/**
 * @brief Submit a non-blocking callback to the scheduler run-queue.
 */
Status ICACHE_FLASH_ATTR SCHED_Submit(sched_task_fn_t fn, void *arg)
{
    if (!g_sched_ready || fn == (sched_task_fn_t)0) {
        return STATUS_ERROR_NOT_INITIALIZED;
    }

    if (!rq_enqueue(fn, arg)) {
        /* Queue full — SCHED_GetStatus() exposes the counter */
        return STATUS_ERROR_POOL_EXHAUSTED;
    }

    /*
     * Post a signal immediately so the SDK dispatches the task
     * as soon as the current event-loop iteration completes,
     * rather than waiting until the next 20 ms tick.
     */
    system_os_post(SCHED_TASK_PRIO, SCHED_SIG_POST, 0);

    return STATUS_OK;
}

/**
 * @brief Feed the software watchdog.
 *
 * Must be called by any long computation that cannot be easily split
 * (e.g., a single large matrix multiply).  Keeps the WDT from firing.
 *
 * Internally maps to system_soft_wdt_feed().  Call at ≤ 500 ms intervals.
 */
void ICACHE_FLASH_ATTR SCHED_FeedWDT(void)
{
    system_soft_wdt_feed();
}

/**
 * @brief Return current scheduler statistics.
 */
void ICACHE_FLASH_ATTR SCHED_GetStatus(sched_status_t *out)
{
    if (!out) return;
    out->ticks          = g_ticks;
    out->tasks_run      = g_tasks_run;
    out->queue_full_err = g_queue_full_err;
    out->queue_depth    = g_q_len;
}

/**
 * @brief Return number of tasks currently queued.
 */
uint8_t ICACHE_FLASH_ATTR SCHED_GetQueueDepth(void)
{
    return g_q_len;
}

/*
 * -----------------------------------------------------------------------
 * USAGE NOTES FOR INTEGRATORS
 * -----------------------------------------------------------------------
 *
 * 1. SIMPLE PERIODIC TASK (e.g. health monitor every 5s):
 *
 *    static void health_cb(void *arg) {
 *        (void)arg;
 *        HAL_UART_PutString("[MON] heap=");
 *        HAL_UART_PutDec(system_get_free_heap_size());
 *        HAL_UART_PutString("\n");
 *        // Re-schedule after ~5000 ms = 250 × 20 ms ticks.
 *        // Use a counter stored in arg (cast to intptr_t) or a static.
 *        static uint16_t countdown = 250;
 *        if (--countdown == 0) {
 *            countdown = 250;
 *            SCHED_Submit(health_cb, NULL);
 *        }
 *    }
 *    // In user_init() after SCHED_Init():
 *    SCHED_Submit(health_cb, NULL);
 *
 * 2. LONG-RUNNING ONNX INFERENCE (state machine pattern):
 *
 *    typedef struct { uint16_t op_idx; } InferState;
 *    static InferState g_infer = {0};
 *
 *    static void infer_step_cb(void *arg) {
 *        InferState *s = (InferState *)arg;
 *        uint32_t t0 = HAL_Timer_GetTicks();
 *
 *        // Run operators until 18 ms budget exhausted or graph done
 *        while (s->op_idx < g_ctx.num_nodes) {
 *            ONNX_Runtime_RunSingleNode(&g_ctx, s->op_idx);
 *            s->op_idx++;
 *            SCHED_FeedWDT();  // feed WDT inside tight loop
 *            if (HAL_Timer_GetElapsedUs(t0) > 18000) break; // 18 ms
 *        }
 *
 *        if (s->op_idx < g_ctx.num_nodes) {
 *            SCHED_Submit(infer_step_cb, s);  // continue next tick
 *        } else {
 *            s->op_idx = 0;  // inference complete — reset for next run
 *        }
 *    }
 *
 *    // Fire off inference:
 *    g_infer.op_idx = 0;
 *    SCHED_Submit(infer_step_cb, &g_infer);
 *
 * 3. SHELL HOOKUP:
 *    The shell_esp.c already polls via its own os_timer every 20 ms.
 *    Because HAL_UART_HasChar() is non-blocking, the shell poll callback
 *    is safe to use independently of this scheduler.  Alternatively,
 *    submit the shell poll as a scheduler task:
 *
 *    extern void shell_poll_cb(void *);   // from shell_esp.c
 *    SCHED_Submit(shell_poll_cb, NULL);
 *
 *    and remove the separate os_timer in shell_esp.c to cut one timer.
 * -----------------------------------------------------------------------
 */
