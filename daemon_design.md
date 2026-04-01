# MiniOS — Background Daemon Implementation Guide

## 1. How Daemons Map to the Existing Kernel

Your kernel already has everything needed:

| Primitive | What it gives you |
|---|---|
| [THREAD_Create()](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/thread.c#147-239) | Spawn a perpetual background function |
| [THREAD_Sleep(ms)](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/thread.c#279-301) | Yield CPU for N ms — the daemon "period" |
| [THREAD_Yield()](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/thread.c#240-256) | Cooperative yield without sleeping |
| [SCHED_TimerTick()](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/thread.c#368-390) | 10 ms timer ISR wakes sleepers automatically |
| `KMEM_GetStats()` | Memory instrumentation data |
| `HAL_Timer_GetSystemTicks()` | Raw monotonic tick counter |
| [SCHED_GetUptime()](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/thread.c#396-400) | Milliseconds since boot |

A **daemon** in MiniOS is simply a `THREAD_PRIORITY_LOW` thread whose entry function runs an infinite `while(1)` loop that:
1. Does its periodic work.
2. Calls [THREAD_Sleep(period_ms)](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/thread.c#279-301) to yield until next activation.

```
         ┌─────────────────────────────────────────────────────┐
         │              kernel_main() boot sequence            │
         │  KERNEL_Init → create inference/comm threads        │
         │  DAEMON_RegisterAll()  ← NEW                        │
         │  KERNEL_Start()                                      │
         └─────────────────────────────────────────────────────┘
                          │   scheduler runs
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
   inference_thread  clock_daemon   memwatch_daemon
   (NORMAL)          (LOW, 1 000ms) (LOW, 500ms)
                                    runtime_daemon
                                    (LOW, 2 000ms)
```

---

## 2. File Layout (new files in bold)

```
src/
  kernel/
    main.c          ← add DAEMON_RegisterAll() call
    thread.c        (unchanged)
    kmem.c          (unchanged)
    **daemon.c**    ← all daemon implementations
include/
  kernel/
    thread.h        (unchanged)
    kmem.h          (unchanged)
    **daemon.h**    ← daemon registry API + constants
```

---

## 3. `include/kernel/daemon.h`

```c
/**
 * @file daemon.h
 * @brief Background daemon subsystem for MiniOS
 *
 * Daemons are long-lived, low-priority cooperative threads that run
 * periodic housekeeping tasks. They are registered before SCHED_Start()
 * and sleep between activations.
 */

#ifndef MINIOS_KERNEL_DAEMON_H
#define MINIOS_KERNEL_DAEMON_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Daemon periods                                                     */
/* ------------------------------------------------------------------ */
#define DAEMON_CLOCK_PERIOD_MS      1000   /**< Wall-clock tick: 1 s  */
#define DAEMON_MEMWATCH_PERIOD_MS    500   /**< Memory check: 500 ms  */
#define DAEMON_RUNTIME_PERIOD_MS    2000   /**< Runtime report: 2 s   */

/* Memory watermark: warn if heap used > this fraction (80 %) */
#define DAEMON_MEM_WARN_PERCENT     80

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Register and create all built-in daemon threads.
 *
 * Must be called AFTER SCHED_Init() and BEFORE SCHED_Start().
 *
 * Creates:
 *   - clock_daemon   : increments a second counter, prints wall time
 *   - memwatch_daemon: checks heap usage, warns on high watermark
 *   - runtime_daemon : prints system uptime + per-thread stats
 *
 * @return STATUS_OK if all daemons created successfully.
 */
Status DAEMON_RegisterAll(void);

/**
 * @brief Get wall-clock seconds elapsed since boot.
 * (Updated by clock_daemon every DAEMON_CLOCK_PERIOD_MS)
 */
uint64_t DAEMON_GetWallSeconds(void);

#endif /* MINIOS_KERNEL_DAEMON_H */
```

---

## 4. `src/kernel/daemon.c`

```c
/**
 * @file daemon.c
 * @brief MiniOS built-in background daemons
 *
 * Three cooperative daemons registered at boot:
 *
 *  1. clock_daemon   — maintains a second-resolution wall clock
 *  2. memwatch_daemon— monitors heap usage and warns on exhaustion
 *  3. runtime_daemon — prints uptime and scheduler statistics
 *
 * All daemons run at THREAD_PRIORITY_LOW and cooperate via
 * THREAD_Sleep() so they never starve inference workloads.
 */

#include "kernel/daemon.h"
#include "kernel/thread.h"
#include "kernel/kmem.h"
#include "hal/uart.h"
#include "hal/timer.h"

/* ------------------------------------------------------------------ */
/*  Shared daemon state                                               */
/* ------------------------------------------------------------------ */

/** Wall-clock seconds since boot (updated by clock_daemon) */
static volatile uint64_t wall_seconds = 0;

uint64_t DAEMON_GetWallSeconds(void) { return wall_seconds; }

/* ------------------------------------------------------------------ */
/*  Helper: print "HH:MM:SS"                                          */
/* ------------------------------------------------------------------ */
static void print_time(uint64_t s)
{
    uint64_t h  = s / 3600;
    uint64_t m  = (s % 3600) / 60;
    uint64_t sc = s % 60;

    /* Hours */
    if (h < 10) HAL_UART_PutString("0");
    HAL_UART_PutDec((uint32_t)h);
    HAL_UART_PutString(":");
    /* Minutes */
    if (m < 10) HAL_UART_PutString("0");
    HAL_UART_PutDec((uint32_t)m);
    HAL_UART_PutString(":");
    /* Seconds */
    if (sc < 10) HAL_UART_PutString("0");
    HAL_UART_PutDec((uint32_t)sc);
}

/* ------------------------------------------------------------------ */
/*  Daemon 1 — Wall Clock                                             */
/* ------------------------------------------------------------------ */
/**
 * clock_daemon
 *
 * Wakes every DAEMON_CLOCK_PERIOD_MS (1 000 ms) and increments the
 * global second counter. Acts as the system wall clock.
 *
 * Output example:
 *   [CLOCK] 00:00:01
 *   [CLOCK] 00:00:02
 */
static void clock_daemon(void *arg)
{
    (void)arg;
    HAL_UART_PutString("[CLOCK] Wall-clock daemon started\n");

    while (1) {
        THREAD_Sleep(DAEMON_CLOCK_PERIOD_MS);

        wall_seconds++;

        HAL_UART_PutString("[CLOCK] ");
        print_time(wall_seconds);
        HAL_UART_PutString("\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Daemon 2 — Memory Watchdog                                        */
/* ------------------------------------------------------------------ */
/**
 * memwatch_daemon
 *
 * Wakes every DAEMON_MEMWATCH_PERIOD_MS (500 ms) and checks the
 * kernel heap usage. If usage exceeds DAEMON_MEM_WARN_PERCENT (80%),
 * emits a warning over UART.
 *
 * This is the equivalent of a memory-pressure daemon. In a more
 * advanced system it would trigger garbage collection or signal
 * workload threads to free tensors.
 *
 * Output example:
 *   [MEMW ] heap 120KB/256KB (46%)  pools 3/8
 *   [MEMW ] *** WARN: heap usage 85% — above watermark ***
 */
static void memwatch_daemon(void *arg)
{
    (void)arg;
    HAL_UART_PutString("[MEMW ] Memory watchdog daemon started\n");

    while (1) {
        THREAD_Sleep(DAEMON_MEMWATCH_PERIOD_MS);

        kmem_stats_t st;
        KMEM_GetStats(&st);

        /* Compute usage percentage (avoid /0) */
        uint32_t pct = (st.heap_total > 0)
                     ? (uint32_t)((st.heap_used * 100) / st.heap_total)
                     : 0;

        HAL_UART_PutString("[MEMW ] heap ");
        HAL_UART_PutDec((uint32_t)(st.heap_used / 1024));
        HAL_UART_PutString("KB/");
        HAL_UART_PutDec((uint32_t)(st.heap_total / 1024));
        HAL_UART_PutString("KB (");
        HAL_UART_PutDec(pct);
        HAL_UART_PutString("%)");

        /* Pool stats if your kmem_stats_t tracks them */
        HAL_UART_PutString("\n");

        /* High-watermark warning */
        if (pct >= DAEMON_MEM_WARN_PERCENT) {
            HAL_UART_PutString("[MEMW ] *** WARN: heap usage ");
            HAL_UART_PutDec(pct);
            HAL_UART_PutString("% -- above watermark ***\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Daemon 3 — Runtime Monitor                                        */
/* ------------------------------------------------------------------ */
/**
 * runtime_daemon
 *
 * Wakes every DAEMON_RUNTIME_PERIOD_MS (2 000 ms) and prints:
 *   - System uptime (ms)
 *   - Total thread count
 *   - Wall-clock time
 *
 * This is what drives a future "uptime" or "ps" shell command —
 * the daemon maintains the live data; the shell just reads it.
 *
 * Output example:
 *   [RTMON] uptime=4000ms  threads=4  wall=00:00:04
 */
static void runtime_daemon(void *arg)
{
    (void)arg;
    HAL_UART_PutString("[RTMON] Runtime monitor daemon started\n");

    while (1) {
        THREAD_Sleep(DAEMON_RUNTIME_PERIOD_MS);

        uint64_t uptime_ms  = SCHED_GetUptime();
        uint32_t num_threads = SCHED_GetThreadCount();

        HAL_UART_PutString("[RTMON] uptime=");
        HAL_UART_PutDec((uint32_t)uptime_ms);
        HAL_UART_PutString("ms  threads=");
        HAL_UART_PutDec(num_threads);
        HAL_UART_PutString("  wall=");
        print_time(wall_seconds);
        HAL_UART_PutString("\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Registry                                                          */
/* ------------------------------------------------------------------ */
Status DAEMON_RegisterAll(void)
{
    Status s;
    thread_t *t;

    HAL_UART_PutString("[DAEM ] Registering daemons...\n");

    s = THREAD_Create(&t, "clock",   clock_daemon,   NULL,
                      THREAD_PRIORITY_LOW, 0);
    if (s != STATUS_OK) return s;

    s = THREAD_Create(&t, "memwatch", memwatch_daemon, NULL,
                      THREAD_PRIORITY_LOW, 0);
    if (s != STATUS_OK) return s;

    s = THREAD_Create(&t, "rtmon",   runtime_daemon, NULL,
                      THREAD_PRIORITY_LOW, 0);
    if (s != STATUS_OK) return s;

    HAL_UART_PutString("[DAEM ] All daemons registered\n");
    return STATUS_OK;
}
```

---

## 5. Changes to [main.c](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/main.c)

Add the include and one call in [kernel_main()](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/main.c#231-340):

```c
// ---- at the top of main.c ----
#include "kernel/daemon.h"

// ---- inside kernel_main(), after SCHED_Init() and thread creation ----

    /* ---- Step 8b: Register background daemons ---- */
    HAL_UART_PutString("[BOOT] Registering daemons...\n");
    status = DAEMON_RegisterAll();
    HAL_UART_PutString("[BOOT] Daemon status: ");
    HAL_UART_PutString(STATUS_ToString(status));
    HAL_UART_PutString("\n");
```

---

## 6. Adding `daemon.c` to the Makefile

```makefile
# In your existing Makefile, find the SRC or OBJS list and add:
KERNEL_OBJS += $(BUILD)/kernel/daemon.o
```

---

## 7. Future: "uptime" / "memstat" Shell Commands

When you wire a UART command loop (the future `comm` daemon), these shell commands are trivial:

```c
/* In your UART command parser */
if (strcmp(cmd, "uptime") == 0) {
    HAL_UART_PutString("Uptime: ");
    HAL_UART_PutDec((uint32_t)(SCHED_GetUptime() / 1000));
    HAL_UART_PutString("s  wall=");
    print_time(DAEMON_GetWallSeconds());
    HAL_UART_PutString("\n");

} else if (strcmp(cmd, "memstat") == 0) {
    kmem_stats_t st;
    KMEM_GetStats(&st);
    HAL_UART_PutString("Heap: ");
    HAL_UART_PutDec((uint32_t)(st.heap_used / 1024));
    HAL_UART_PutString("KB used / ");
    HAL_UART_PutDec((uint32_t)(st.heap_total / 1024));
    HAL_UART_PutString("KB total\n");

} else if (strcmp(cmd, "ps") == 0) {
    HAL_UART_PutString("Threads: ");
    HAL_UART_PutDec(SCHED_GetThreadCount());
    HAL_UART_PutString("\n");
}
```

---

## 8. Thread Budget

With daemons added, your thread table looks like:

| ID | Name       | Priority | Period  | Purpose |
|----|------------|----------|---------|---------|
| 0  | idle       | IDLE     | —       | WFE spin (boot context) |
| 1  | inference  | NORMAL   | yield   | ML graph execution |
| 2  | monitor    | LOW      | 100 ms  | (existing demo) |
| 3  | clock      | LOW      | 1 000 ms | Wall clock |
| 4  | memwatch   | LOW      | 500 ms  | Heap watchdog |
| 5  | rtmon      | LOW      | 2 000 ms | Runtime stats |

6 threads of 16 max — plenty of headroom for a `comm` daemon and future ONNX workers.

---

## 9. Key Design Rules for any New Daemon

1. **Never busy-wait** — always end the work body with [THREAD_Sleep(period_ms)](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/thread.c#279-301).
2. **Lowest possible priority** — use `THREAD_PRIORITY_LOW` so inference always wins.
3. **No dynamic allocation inside the loop** — allocate anything you need before the `while(1)`.
4. **Keep UART output short** — UART is blocking; long prints stall other threads.
5. **Shared state via `volatile`** — any variable written by one thread and read by another must be `volatile`. For larger structs, bracket writes with `arch_irq_save()` / `arch_irq_restore()`.
