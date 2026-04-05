/**
 * @file thread.c
 * @brief Threading and Scheduler implementation for MiniOS
 *
 * Cooperative multithreading with priority-based scheduling.
 *
 * Scheduling algorithm:
 *   - 4 priority levels, each with a FIFO ready queue
 *   - Scheduler always picks the highest-priority ready thread
 *   - Threads yield voluntarily (cooperative)
 *   - Timer ticks wake sleeping threads but don't force switches
 *
 * Thread lifecycle: INVALID → READY → RUNNING ⇄ READY/SLEEPING → TERMINATED
 *
 * The idle thread (ID 0) is created from the boot context in SCHED_Init()
 * and runs at THREAD_PRIORITY_IDLE. All other threads are created via
 * THREAD_Create() with user-specified priorities and stack sizes.
 *
 * @note Per SRS: Cooperative execution model
 */

#include "kernel/thread.h"
#include "kernel/kmem.h"
#include "hal/timer.h"
#include "hal/arch.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  External assembly routines (context.S)                            */
/* ------------------------------------------------------------------ */
extern void _thread_entry_trampoline(void);

/* ------------------------------------------------------------------ */
/*  Thread table and scheduler state                                  */
/* ------------------------------------------------------------------ */

/** Static thread table — all TCBs are pre-allocated */
static thread_t threads[THREAD_MAX_COUNT];

/** Current number of created threads */
static uint32_t thread_count = 0;

/** Pointer to the currently running thread */
static thread_t *current_thread = NULL;

/** Per-priority ready queues (head and tail for O(1) enqueue) */
static thread_t *ready_head[THREAD_PRIORITY_COUNT];
static thread_t *ready_tail[THREAD_PRIORITY_COUNT];

/** Scheduler active flag */
static bool sched_running = false;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Copy a string with length limit
 */
static void copy_name(char *dst, const char *src, size_t max)
{
    size_t i;
    for (i = 0; i < max - 1 && src != NULL && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/**
 * @brief Enqueue a thread at the tail of its priority queue
 */
static void enqueue_ready(thread_t *t)
{
    uint8_t p = t->priority;
    t->next = NULL;

    if (ready_tail[p] != NULL) {
        ready_tail[p]->next = t;
    } else {
        ready_head[p] = t;
    }
    ready_tail[p] = t;
}

/**
 * @brief Dequeue the highest-priority ready thread
 * @return Thread pointer, or NULL if all queues are empty
 */
static thread_t *dequeue_ready(void)
{
    for (int p = 0; p < THREAD_PRIORITY_COUNT; p++) {
        if (ready_head[p] != NULL) {
            thread_t *t = ready_head[p];
            ready_head[p] = t->next;
            if (ready_head[p] == NULL) {
                ready_tail[p] = NULL;
            }
            t->next = NULL;
            return t;
        }
    }
    return NULL;
}

/**
 * @brief Core scheduling logic — switch from current to next thread
 *
 * Called with interrupts disabled. Selects the highest-priority
 * ready thread and performs a context switch if different from current.
 */
static void schedule(void)
{
    thread_t *next = dequeue_ready();

    if (next == NULL) {
        /* No other thread ready — continue with current */
        if (current_thread->state != THREAD_STATE_RUNNING) {
            /* Current thread is sleeping/blocked, but nothing else to run.
             * This shouldn't happen (idle thread should always be ready).
             * Fallback: force idle thread. */
            current_thread = &threads[0];
            current_thread->state = THREAD_STATE_RUNNING;
        }
        return;
    }

    if (next == current_thread) {
        /* Same thread, just continue */
        current_thread->state = THREAD_STATE_RUNNING;
        return;
    }

    /* Perform context switch */
    thread_t *old = current_thread;
    current_thread = next;
    next->state = THREAD_STATE_RUNNING;

    cpu_context_switch(&old->context, &next->context);

    /* When we return here, we've been switched back to 'old' */
}

/* ------------------------------------------------------------------ */
/*  Thread API                                                        */
/* ------------------------------------------------------------------ */

Status THREAD_Create(thread_t **out, const char *name,
                     thread_func_t func, void *arg,
                     uint8_t priority, size_t stack_size)
{
    if (func == NULL) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    if (thread_count >= THREAD_MAX_COUNT) {
        return STATUS_ERROR_THREAD_LIMIT;
    }

    if (priority >= THREAD_PRIORITY_COUNT) {
        priority = THREAD_PRIORITY_NORMAL;
    }

    if (stack_size == 0) {
        stack_size = THREAD_DEFAULT_STACK;
    }

    /* Allocate stack from heap (16-byte aligned for ARM64 ABI) */
    uint8_t *stack = (uint8_t *)KMEM_Alloc(stack_size, 16);
    if (stack == NULL) {
        return STATUS_ERROR_OUT_OF_MEMORY;
    }

    /* Get next free TCB slot */
    uint64_t flags = arch_irq_save();

    thread_t *t = &threads[thread_count];
    uint32_t id = thread_count;
    thread_count++;

    /* Initialize TCB */
    t->id         = id;
    t->state      = THREAD_STATE_READY;
    t->priority   = priority;
    t->stack_base = stack;
    t->stack_size = stack_size;
    t->wake_tick  = 0;
    t->total_ticks = 0;
    t->next       = NULL;

    copy_name(t->name, name, THREAD_NAME_MAX);

    /*
     * Set up initial context for first context switch.
     *
     * When cpu_context_switch restores this context and executes 'ret',
     * it jumps to LR = _thread_entry_trampoline.
     *
     * The trampoline reads:
     *   x19 = function pointer → calls it
     *   x20 = argument → passes as x0
     *
     * Then calls THREAD_Exit() if the function returns.
     */
    uint8_t *stack_top = stack + stack_size;
    stack_top = (uint8_t *)((uintptr_t)stack_top & ~0xFUL); /* 16-byte align */

    /* Zero the context */
    for (int i = 0; i < (int)sizeof(cpu_context_t) / 8; i++) {
        ((uint64_t *)&t->context)[i] = 0;
    }

    t->context.x19 = (uint64_t)(uintptr_t)func;
    t->context.x20 = (uint64_t)(uintptr_t)arg;
    t->context.lr  = (uint64_t)(uintptr_t)&_thread_entry_trampoline;
    t->context.sp  = (uint64_t)(uintptr_t)stack_top;
    t->context.fp  = 0;  /* End of call chain */

    /* Add to ready queue */
    enqueue_ready(t);

    arch_irq_restore(flags);

    if (out != NULL) {
        *out = t;
    }

    HAL_UART_PutString("[THRD] Created '");
    HAL_UART_PutString(t->name);
    HAL_UART_PutString("' (id=");
    HAL_UART_PutDec(t->id);
    HAL_UART_PutString(", prio=");
    HAL_UART_PutDec(t->priority);
    HAL_UART_PutString(", stack=");
    HAL_UART_PutDec(stack_size);
    HAL_UART_PutString(")\n");

    return STATUS_OK;
}

void THREAD_Yield(void)
{
    uint64_t flags = arch_irq_save();

    thread_t *old = current_thread;

    /* Only re-enqueue if still running (not sleeping/blocked/terminated) */
    if (old->state == THREAD_STATE_RUNNING) {
        old->state = THREAD_STATE_READY;
        enqueue_ready(old);
    }

    schedule();

    arch_irq_restore(flags);
}

void THREAD_Exit(void)
{
    uint64_t flags = arch_irq_save();

    HAL_UART_PutString("[THRD] Thread '");
    HAL_UART_PutString(current_thread->name);
    HAL_UART_PutString("' exiting\n");

    current_thread->state = THREAD_STATE_TERMINATED;

    /* Don't enqueue — thread is dead */
    schedule();

    /* If schedule returns (shouldn't for terminated thread),
     * restore IRQs and halt */
    arch_irq_restore(flags);

    while (1) {
        arch_wfe();
    }
}

void THREAD_Sleep(uint64_t ms)
{
    if (ms == 0) {
        THREAD_Yield();
        return;
    }

    uint64_t flags = arch_irq_save();

    /* Compute wake tick */
    uint32_t tick_ms = HAL_Timer_GetTickPeriodMs();
    if (tick_ms == 0) tick_ms = 10; /* fallback */

    uint64_t ticks = (ms + tick_ms - 1) / tick_ms; /* Round up */
    current_thread->wake_tick = HAL_Timer_GetSystemTicks() + ticks;
    current_thread->state = THREAD_STATE_SLEEPING;

    /* Don't enqueue — sleeping threads are tracked by timer tick */
    schedule();

    arch_irq_restore(flags);
}

thread_t *THREAD_GetCurrent(void)
{
    return current_thread;
}

uint32_t THREAD_GetID(void)
{
    if (current_thread == NULL) return 0;
    return current_thread->id;
}

/* ------------------------------------------------------------------ */
/*  Scheduler API                                                     */
/* ------------------------------------------------------------------ */

Status SCHED_Init(void)
{
    /* Initialize ready queues */
    for (int p = 0; p < THREAD_PRIORITY_COUNT; p++) {
        ready_head[p] = NULL;
        ready_tail[p] = NULL;
    }

    /*
     * Create idle thread (ID 0) from current boot context.
     *
     * The boot code's stack and registers become the idle thread.
     * We don't need to set up context — it will be saved on
     * the first context switch (when idle yields).
     */
    thread_t *idle = &threads[0];
    idle->id         = 0;
    idle->state      = THREAD_STATE_RUNNING;
    idle->priority   = THREAD_PRIORITY_IDLE;
    idle->stack_base = NULL;  /* Uses boot stack */
    idle->stack_size = 0;
    idle->wake_tick  = 0;
    idle->total_ticks = 0;
    idle->next       = NULL;

    copy_name(idle->name, "idle", THREAD_NAME_MAX);

    current_thread = idle;
    thread_count = 1;
    sched_running = false;

    HAL_UART_PutString("[SCHED] Scheduler initialized\n");
    return STATUS_OK;
}

void SCHED_Start(void)
{
    HAL_UART_PutString("[SCHED] Starting scheduler\n");
    sched_running = true;

    /*
     * The calling context (kernel_main) becomes the idle thread.
     * Enter the idle loop: yield to let real threads run,
     * then WFE when nothing is available.
     */
    while (1) {
        THREAD_Yield();
        arch_wfe();
    }
}

void SCHED_TimerTick(void)
{
    /*
     * Called from timer ISR context.
     *
     * Check all sleeping threads: if their wake tick has arrived,
     * move them to READY state and enqueue them.
     *
     * This does NOT perform context switches — the woken thread
     * will be scheduled on the next voluntary yield.
     */
    uint64_t now = HAL_Timer_GetSystemTicks();

    for (uint32_t i = 0; i < thread_count; i++) {
        if (threads[i].state == THREAD_STATE_SLEEPING) {
            if (now >= threads[i].wake_tick) {
                threads[i].state = THREAD_STATE_READY;
                enqueue_ready(&threads[i]);
            }
        }
    }
}

uint32_t SCHED_GetThreadCount(void)
{
    return thread_count;
}

uint64_t SCHED_GetUptime(void)
{
    return HAL_Timer_GetSystemTicks() * HAL_Timer_GetTickPeriodMs();
}
