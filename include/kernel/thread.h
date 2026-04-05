/**
 * @file thread.h
 * @brief Threading and Scheduler API for MiniOS
 *
 * Provides cooperative multithreading with priority-based scheduling
 * for ML inference workloads. Designed for single-core ARM64 with
 * timer-assisted thread management (sleep/wake).
 *
 * Execution Model:
 *   - Cooperative: threads yield explicitly at operator boundaries
 *   - Priority-based: 4 levels (HIGH, NORMAL, LOW, IDLE)
 *   - Timer ticks wake sleeping threads; no forced preemption
 *   - Single address space: all threads share the same memory map
 *
 * Typical ML inference threading pattern:
 *   Thread 1 (NORMAL): Inference execution, yields between operators
 *   Thread 2 (LOW):    Performance monitoring, yields after collection
 *   Thread 3 (LOW):    Communication handler, yields when idle
 *   Idle thread:        WFE loop when no work is pending
 *
 * @note Per SRS: Cooperative execution model with static scheduling
 */

#ifndef MINIOS_KERNEL_THREAD_H
#define MINIOS_KERNEL_THREAD_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

/** Maximum number of threads (including idle) */
#define THREAD_MAX_COUNT        16

/** Default stack size per thread (8KB) */
#define THREAD_DEFAULT_STACK    8192

/** Maximum thread name length (including null terminator) */
#define THREAD_NAME_MAX         16

/** Thread priority levels (lower number = higher priority) */
#define THREAD_PRIORITY_HIGH    0
#define THREAD_PRIORITY_NORMAL  1
#define THREAD_PRIORITY_LOW     2
#define THREAD_PRIORITY_IDLE    3
#define THREAD_PRIORITY_COUNT   4

/* ------------------------------------------------------------------ */
/*  Types                                                             */
/* ------------------------------------------------------------------ */

/** Thread entry function signature */
typedef void (*thread_func_t)(void *arg);

/** Thread states */
typedef enum {
    THREAD_STATE_INVALID    = 0,    /**< Unused TCB slot */
    THREAD_STATE_READY,             /**< Runnable, in ready queue */
    THREAD_STATE_RUNNING,           /**< Currently executing on CPU */
    THREAD_STATE_SLEEPING,          /**< Sleeping, waiting for tick */
    THREAD_STATE_BLOCKED,           /**< Blocked on resource (future) */
    THREAD_STATE_TERMINATED         /**< Finished execution */
} thread_state_t;

/**
 * @brief CPU context for cooperative context switch
 *
 * Stores only callee-saved registers (x19-x30) and SP.
 * Caller-saved registers are preserved by the C calling convention
 * when threads yield voluntarily.
 *
 * Layout must match context.S offsets exactly:
 *   x19 at offset  0, x20 at  8, x21 at 16, x22 at 24,
 *   x23 at 32, x24 at 40, x25 at 48, x26 at 56,
 *   x27 at 64, x28 at 72, fp(x29) at 80, lr(x30) at 88,
 *   sp at 96. Total: 104 bytes.
 */
typedef struct {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp;    /* x29 — frame pointer */
    uint64_t lr;    /* x30 — link register (return address) */
    uint64_t sp;    /* stack pointer */
} cpu_context_t;

/**
 * @brief Thread Control Block (TCB)
 *
 * Contains all per-thread state: saved CPU context, scheduling
 * metadata, and stack information.
 */
typedef struct thread {
    cpu_context_t   context;        /**< Saved CPU context (must be first!) */
    uint32_t        id;             /**< Unique thread ID */
    char            name[THREAD_NAME_MAX]; /**< Human-readable name */
    thread_state_t  state;          /**< Current thread state */
    uint8_t         priority;       /**< Scheduling priority */
    uint8_t        *stack_base;     /**< Base of allocated stack */
    size_t          stack_size;     /**< Stack size in bytes */
    uint64_t        wake_tick;      /**< System tick to wake from sleep */
    uint64_t        total_ticks;    /**< Total ticks this thread has run */
    struct thread  *next;           /**< Next pointer for queue linkage */
} thread_t;

/* ------------------------------------------------------------------ */
/*  Thread Management API                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a new thread
 *
 * Allocates a stack from the kernel heap and initializes the thread
 * context so that it begins execution at the given function when
 * first scheduled.
 *
 * @param[out] out        Receives pointer to the new thread TCB
 * @param[in]  name       Human-readable name (max 15 chars)
 * @param[in]  func       Thread entry function
 * @param[in]  arg        Argument passed to entry function
 * @param[in]  priority   Scheduling priority (THREAD_PRIORITY_*)
 * @param[in]  stack_size Stack size in bytes (0 = default 8KB)
 *
 * @return STATUS_OK on success
 *         STATUS_ERROR_OUT_OF_MEMORY if stack allocation fails
 *         STATUS_ERROR_THREAD_LIMIT if max threads reached
 *         STATUS_ERROR_INVALID_ARGUMENT if func is NULL
 */
Status THREAD_Create(thread_t **out, const char *name,
                     thread_func_t func, void *arg,
                     uint8_t priority, size_t stack_size);

/**
 * @brief Voluntarily yield the CPU to the next ready thread
 *
 * The current thread is placed at the end of its priority queue
 * and the highest-priority ready thread is selected. If no other
 * thread is ready, the current thread continues immediately.
 *
 * This is the primary scheduling mechanism for cooperative
 * multithreading. ML inference threads should yield between
 * operator executions.
 */
void THREAD_Yield(void);

/**
 * @brief Terminate the current thread
 *
 * Marks the thread as TERMINATED and switches to the next ready
 * thread. Does not return.
 *
 * @warning The thread's stack memory is not reclaimed. For long-
 *          running systems, consider thread reuse patterns.
 */
void THREAD_Exit(void);

/**
 * @brief Sleep the current thread for a specified duration
 *
 * The thread is moved to SLEEPING state and will be woken by the
 * scheduler timer tick when the duration expires. Resolution is
 * limited to the timer tick period (default 10ms).
 *
 * @param[in] ms  Sleep duration in milliseconds (minimum: 1 tick period)
 */
void THREAD_Sleep(uint64_t ms);

/**
 * @brief Get a pointer to the currently running thread
 * @return Pointer to current thread's TCB
 */
thread_t *THREAD_GetCurrent(void);

/**
 * @brief Get the current thread's ID
 * @return Thread ID
 */
uint32_t THREAD_GetID(void);

/* ------------------------------------------------------------------ */
/*  Scheduler API                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the scheduler
 *
 * Creates the idle thread from the current boot context.
 * Must be called after KMEM_Init().
 *
 * @return STATUS_OK on success
 */
Status SCHED_Init(void);

/**
 * @brief Start the scheduler's idle loop
 *
 * Converts the calling context into the idle thread and begins
 * cooperative scheduling. This function does NOT return.
 *
 * The idle thread runs at THREAD_PRIORITY_IDLE and enters
 * low-power WFE state when no work is available.
 */
void SCHED_Start(void);

/**
 * @brief Timer tick handler — called from timer interrupt
 *
 * Increments the system tick counter and wakes any threads
 * whose sleep duration has elapsed. Called from interrupt
 * context. Does NOT perform context switches.
 */
void SCHED_TimerTick(void);

/**
 * @brief Get the total number of created threads
 * @return Thread count (including idle and terminated)
 */
uint32_t SCHED_GetThreadCount(void);

/**
 * @brief Get the system uptime in timer ticks
 * @return Number of timer ticks since scheduler start
 */
uint64_t SCHED_GetUptime(void);

/* ------------------------------------------------------------------ */
/*  Assembly routines (defined in context.S)                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Perform a cooperative context switch between two threads
 *
 * Saves callee-saved registers (x19-x30, SP) to old_ctx,
 * then restores them from new_ctx and returns to the new
 * thread's saved LR.
 *
 * @param[out] old_ctx  Context structure to save current state into
 * @param[in]  new_ctx  Context structure to restore state from
 */
extern void cpu_context_switch(cpu_context_t *old_ctx,
                               cpu_context_t *new_ctx);

#endif /* MINIOS_KERNEL_THREAD_H */
