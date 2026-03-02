/**
 * @file sched.h
 * @brief Cooperative Scheduler interface for MiniOS
 *
 * Provides lightweight cooperative multithreading for the
 * unikernel. Tasks run until they explicitly yield or exit.
 * The scheduler uses round-robin selection among ready tasks.
 *
 * Each task gets its own stack allocated from the kernel heap.
 * Context switching saves/restores callee-saved registers
 * (x19–x30, SP) via pure assembly.
 *
 * @note Per SRS FR-011 (cooperative execution),
 *       BR-005 (cooperative scheduler)
 */

#ifndef MINIOS_KERNEL_SCHED_H
#define MINIOS_KERNEL_SCHED_H

#include "status.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Configuration                                                     */
/* ------------------------------------------------------------------ */

/** Maximum number of concurrent tasks */
#define SCHED_MAX_TASKS 16

/** Default stack size per task (64 KB) */
#define SCHED_DEFAULT_STACK (64 * 1024)

/** Task name maximum length */
#define SCHED_TASK_NAME_LEN 32

/* ------------------------------------------------------------------ */
/*  Task states                                                       */
/* ------------------------------------------------------------------ */

typedef enum {
  TASK_UNUSED = 0,   /**< Slot is available                       */
  TASK_READY = 1,    /**< Task is runnable                        */
  TASK_RUNNING = 2,  /**< Task is currently executing             */
  TASK_BLOCKED = 3,  /**< Task is waiting (future: I/O, timer)    */
  TASK_FINISHED = 4, /**< Task entry function has returned        */
} TaskState;

/* ------------------------------------------------------------------ */
/*  Task entry function signature                                     */
/* ------------------------------------------------------------------ */

/** Task function prototype: void my_task(void* arg) */
typedef void (*TaskFunc)(void *arg);

/* ------------------------------------------------------------------ */
/*  Task Control Block                                                */
/* ------------------------------------------------------------------ */

typedef struct {
  int id;                         /**< Task ID (0-based)    */
  TaskState state;                /**< Current state        */
  char name[SCHED_TASK_NAME_LEN]; /**< Human-readable name  */

  /* Context */
  uint64_t *sp;        /**< Saved stack pointer   */
  uint8_t *stack_base; /**< Stack allocation base */
  size_t stack_size;   /**< Stack size in bytes   */

  /* Entry */
  TaskFunc entry; /**< Task function         */
  void *arg;      /**< Argument to entry     */

  /* Statistics */
  uint64_t switches;        /**< Times scheduled       */
  uint64_t total_ticks;     /**< Total runtime (ticks) */
  uint64_t last_start_tick; /**< Tick when last started*/
} Task;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the scheduler
 *
 * Clears all task slots and prepares the scheduler for use.
 * Must be called before any other SCHED_* functions.
 *
 * @return STATUS_OK on success
 */
Status SCHED_Init(void);

/**
 * @brief Create a new task
 *
 * Allocates a stack from the kernel heap, sets up the initial
 * context, and marks the task as TASK_READY.
 *
 * @param[in] name        Human-readable task name
 * @param[in] entry       Task entry function
 * @param[in] arg         Opaque argument passed to entry
 * @param[in] stack_size  Stack size (0 = default 64KB)
 * @return Task ID (>= 0) on success, -1 on failure
 */
int SCHED_CreateTask(const char *name, TaskFunc entry, void *arg,
                     size_t stack_size);

/**
 * @brief Yield the current task to the scheduler
 *
 * Saves the current task's context and switches to the
 * next ready task. This is the primary cooperative yield point.
 */
void SCHED_Yield(void);

/**
 * @brief Mark the current task as finished and yield
 *
 * Called when a task's entry function returns (via trampoline)
 * or explicitly by the task. The task is not scheduled again.
 */
void SCHED_Exit(void);

/**
 * @brief Run the scheduler main loop
 *
 * Dispatches tasks in round-robin order until all tasks are
 * TASK_FINISHED. Returns control to the caller.
 *
 * @return STATUS_OK when all tasks complete
 */
Status SCHED_Run(void);

/**
 * @brief Get the current running task ID
 * @return Task ID, or -1 if no task is running
 */
int SCHED_GetCurrentTaskId(void);

/**
 * @brief Get the number of alive (non-finished) tasks
 * @return Count of tasks not in FINISHED or UNUSED state
 */
int SCHED_GetAliveCount(void);

/**
 * @brief Print scheduler statistics via UART
 */
void SCHED_PrintStats(void);

/* ------------------------------------------------------------------ */
/*  Context switch (implemented in context_switch.S)                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Low-level context switch
 *
 * Saves callee-saved registers (x19-x30, SP) to *old_sp,
 * then restores from new_sp.
 *
 * @param[out] old_sp  Pointer to save current SP into
 * @param[in]  new_sp  SP value to restore
 */
extern void context_switch(uint64_t **old_sp, uint64_t *new_sp);

#endif /* MINIOS_KERNEL_SCHED_H */
