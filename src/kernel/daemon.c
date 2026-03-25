/**
 * @file daemon.c
 * @brief MiniOS built-in background daemons
 *
 * Implements three cooperative, low-priority daemon threads:
 *
 *   1. clock_daemon    — second-resolution wall clock
 *   2. memwatch_daemon — heap usage watchdog with high-watermark warning
 *   3. runtime_daemon  — periodic uptime + scheduler stats reporter
 *
 * All daemons follow the same pattern:
 *   while(1) { do_work(); THREAD_Sleep(period_ms); }
 *
 * This ensures inference threads (THREAD_PRIORITY_NORMAL) are never
 * starved; daemons run only when no higher-priority work is pending.
 *
 * @note Per SRS FR-020, FR-023, FR-027, FR-030: performance monitoring
 *       and system health reporting.
 */

#include "kernel/daemon.h"
#include "kernel/thread.h"
#include "kernel/kmem.h"
#include "kernel/cmd.h"
#include "kernel/shell.h"
#include "hal/uart.h"
#include "hal/timer.h"

/* ------------------------------------------------------------------ */
/*  Shared state (written by clock_daemon, read by others)           */
/* ------------------------------------------------------------------ */

/**
 * Wall-clock seconds since boot.
 * volatile because it is written in one thread and read in others.
 * Under cooperative scheduling a full-word write is atomic on ARM64,
 * but volatile prevents compiler caching.
 */
static volatile uint64_t g_wall_seconds = 0;

uint64_t DAEMON_GetWallSeconds(void)
{
    return g_wall_seconds;
}

/* ------------------------------------------------------------------ */
/*  Internal helper: print time as "HH:MM:SS"                        */
/* ------------------------------------------------------------------ */
static void uart_print_time(uint64_t total_sec)
{
    uint32_t h  = (uint32_t)(total_sec / 3600U);
    uint32_t m  = (uint32_t)((total_sec % 3600U) / 60U);
    uint32_t s  = (uint32_t)(total_sec % 60U);

    if (h < 10) HAL_UART_PutString("0");
    HAL_UART_PutDec(h);
    HAL_UART_PutString(":");
    if (m < 10) HAL_UART_PutString("0");
    HAL_UART_PutDec(m);
    HAL_UART_PutString(":");
    if (s < 10) HAL_UART_PutString("0");
    HAL_UART_PutDec(s);
}

/* ------------------------------------------------------------------ */
/*  Daemon 1: Wall-Clock                                              */
/* ------------------------------------------------------------------ */
/**
 * @brief clock_daemon — maintains g_wall_seconds, 1 Hz.
 *
 * Sleeps for DAEMON_CLOCK_PERIOD_MS (1 000 ms) between ticks.
 * Resolution is ±1 scheduler tick (10 ms) relative to real wall time.
 *
 * UML Activity: "idle → sleep 1 s → increment clock → print → repeat"
 *
 * Example UART output:
 *   [CLOCK] 00:00:01
 *   [CLOCK] 00:00:02
 */
static void clock_daemon(void *arg)
{
    (void)arg;
    HAL_UART_PutString("[CLOCK] Wall-clock daemon started (1 s period)\n");

    while (1) {
        /* Sleep first so first tick prints at t=1s, not t=0 */
        THREAD_Sleep(DAEMON_CLOCK_PERIOD_MS);

        g_wall_seconds++;

        HAL_UART_PutString("[CLOCK] ");
        uart_print_time(g_wall_seconds);
        HAL_UART_PutString("\n");
    }
    /* Never reached — cooperative thread runs forever */
}

/* ------------------------------------------------------------------ */
/*  Daemon 2: Memory Watchdog                                         */
/* ------------------------------------------------------------------ */
/**
 * @brief memwatch_daemon — monitors kernel heap, warns on pressure.
 *
 * Wakes every DAEMON_MEMWATCH_PERIOD_MS (500 ms).
 * If heap usage exceeds DAEMON_MEM_WARN_PERCENT (80%), a WARNING
 * is emitted. In a production system this could signal ML threads
 * to free intermediate tensors before the next inference.
 *
 * Example UART output:
 *   [MEMW ] heap 48KB/256KB (18%)
 *   [MEMW ] *** WARN: heap 212KB/256KB (82%) -- above 80% watermark ***
 */
static void memwatch_daemon(void *arg)
{
    (void)arg;
    HAL_UART_PutString("[MEMW ] Memory watchdog started (500 ms period)\n");

    while (1) {
        THREAD_Sleep(DAEMON_MEMWATCH_PERIOD_MS);

        kmem_stats_t st;
        KMEM_GetStats(&st);

        /* Safe percentage calculation */
        uint32_t pct = 0;
        if (st.heap_total > 0) {
            pct = (uint32_t)((st.heap_used * 100ULL) / st.heap_total);
        }

        HAL_UART_PutString("[MEMW ] heap ");
        HAL_UART_PutDec((uint32_t)(st.heap_used / 1024U));
        HAL_UART_PutString("KB/");
        HAL_UART_PutDec((uint32_t)(st.heap_total / 1024U));
        HAL_UART_PutString("KB (");
        HAL_UART_PutDec(pct);
        HAL_UART_PutString("%)\n");

        if (pct >= DAEMON_MEM_WARN_PERCENT) {
            HAL_UART_PutString("[MEMW ] *** WARN: heap ");
            HAL_UART_PutDec(pct);
            HAL_UART_PutString("% -- above ");
            HAL_UART_PutDec(DAEMON_MEM_WARN_PERCENT);
            HAL_UART_PutString("% watermark ***\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Daemon 3: Runtime Monitor                                         */
/* ------------------------------------------------------------------ */
/**
 * @brief runtime_daemon — prints uptime + thread stats every 2 s.
 *
 * Provides the data that future "uptime", "ps", and "memstat" shell
 * commands will query. Currently outputs directly to UART.
 *
 * Columns:
 *   uptime  — ms since SCHED_Start()
 *   threads — total thread count (including idle + daemons)
 *   wall    — HH:MM:SS from clock_daemon
 *
 * Example UART output:
 *   [RTMON] uptime=2000ms  threads=6  wall=00:00:02
 *   [RTMON] uptime=4000ms  threads=6  wall=00:00:04
 */
static void runtime_daemon(void *arg)
{
    (void)arg;
    HAL_UART_PutString("[RTMON] Runtime monitor started (2 s period)\n");

    while (1) {
        THREAD_Sleep(DAEMON_RUNTIME_PERIOD_MS);

        uint64_t uptime_ms   = SCHED_GetUptime();
        uint32_t num_threads = SCHED_GetThreadCount();

        HAL_UART_PutString("[RTMON] uptime=");
        HAL_UART_PutDec((uint32_t)uptime_ms);
        HAL_UART_PutString("ms  threads=");
        HAL_UART_PutDec(num_threads);
        HAL_UART_PutString("  wall=");
        uart_print_time(g_wall_seconds);
        HAL_UART_PutString("\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Registry                                                          */
/* ------------------------------------------------------------------ */
Status DAEMON_RegisterAll(void)
{
    Status   s;
    thread_t *t = NULL;     /* TCB pointer (not used after creation) */

    HAL_UART_PutString("[DAEM ] Registering background daemons...\n");

    /* --- 1. Wall clock --- */
    s = THREAD_Create(&t, "clock", clock_daemon, NULL,
                      THREAD_PRIORITY_LOW, 0 /* default 8 KB stack */);
    if (s != STATUS_OK) {
        HAL_UART_PutString("[DAEM ] ERROR: clock daemon: ");
        HAL_UART_PutDec((uint32_t)s);
        HAL_UART_PutString("\n");
        return s;
    }

    /* --- 2. Memory watchdog --- */
    s = THREAD_Create(&t, "memwatch", memwatch_daemon, NULL,
                      THREAD_PRIORITY_LOW, 0);
    if (s != STATUS_OK) {
        HAL_UART_PutString("[DAEM ] ERROR: memwatch daemon: ");
        HAL_UART_PutDec((uint32_t)s);
        HAL_UART_PutString("\n");
        return s;
    }

    /* --- 3. Runtime monitor --- */
    s = THREAD_Create(&t, "rtmon", runtime_daemon, NULL,
                      THREAD_PRIORITY_LOW, 0);
    if (s != STATUS_OK) {
        HAL_UART_PutString("[DAEM ] ERROR: rtmon daemon: ");
        HAL_UART_PutDec((uint32_t)s);
        HAL_UART_PutString("\n");
        return s;
    }

    HAL_UART_PutString("[DAEM ] All daemons registered (clock, memwatch, rtmon)\n");

    /* --- 4. Register built-in commands --- */
    CMD_RegisterBuiltins();
    HAL_UART_PutString("[DAEM ] Built-in commands registered\n");

    /* --- 5. Interactive shell daemon --- */
    s = SHELL_RegisterDaemon();
    if (s != STATUS_OK) {
        HAL_UART_PutString("[DAEM ] ERROR: shell daemon: ");
        HAL_UART_PutDec((uint32_t)s);
        HAL_UART_PutString("\n");
        return s;
    }

    HAL_UART_PutString("[DAEM ] All subsystems ready\n");
    return STATUS_OK;
}
