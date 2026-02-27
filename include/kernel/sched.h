/**
 * @file sched.h
 * @brief Multi-policy Cooperative Scheduler for MiniOS
 *
 * Supports 7 scheduling algorithms for benchmarking:
 *   - FCFS (First Come First Served)
 *   - SJF (Shortest Job First)
 *   - Round-Robin
 *   - HRRN (Highest Response Ratio Next)
 *   - Priority-Based
 *   - Multilevel Queue
 *   - Lottery
 */

#ifndef MINIOS_KERNEL_SCHED_H
#define MINIOS_KERNEL_SCHED_H

#include "status.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/*  Configuration                                                     */
/* ------------------------------------------------------------------ */

#define SCHED_MAX_TASKS 16
#define SCHED_DEFAULT_STACK (64 * 1024)
#define SCHED_TASK_NAME_LEN 32

/* Multilevel Queue: 3 levels */
#define MLQ_LEVEL_CRITICAL 0   /**< Conv2D, MatMul — compute-heavy */
#define MLQ_LEVEL_NORMAL 1     /**< Softmax, ReLU              */
#define MLQ_LEVEL_BACKGROUND 2 /**< Add, housekeeping          */
#define MLQ_NUM_LEVELS 3

/* ------------------------------------------------------------------ */
/*  Scheduling policies                                               */
/* ------------------------------------------------------------------ */

typedef enum {
  SCHED_POLICY_FCFS = 0,
  SCHED_POLICY_SJF = 1,
  SCHED_POLICY_RR = 2,
  SCHED_POLICY_HRRN = 3,
  SCHED_POLICY_PRIORITY = 4,
  SCHED_POLICY_MLQ = 5,
  SCHED_POLICY_LOTTERY = 6,
} SchedPolicy;

#define SCHED_NUM_POLICIES 7

/* ------------------------------------------------------------------ */
/*  Task states                                                       */
/* ------------------------------------------------------------------ */

typedef enum {
  TASK_UNUSED = 0,
  TASK_READY = 1,
  TASK_RUNNING = 2,
  TASK_BLOCKED = 3,
  TASK_FINISHED = 4,
} TaskState;

typedef void (*TaskFunc)(void *arg);

/* ------------------------------------------------------------------ */
/*  Task Control Block                                                */
/* ------------------------------------------------------------------ */

typedef struct {
  int id;
  TaskState state;
  char name[SCHED_TASK_NAME_LEN];

  /* Context */
  uint64_t *sp;
  uint8_t *stack_base;
  size_t stack_size;

  /* Entry */
  TaskFunc entry;
  void *arg;

  /* Scheduling parameters */
  int priority;            /**< Priority (lower=higher)      */
  uint64_t burst_estimate; /**< Estimated burst (ticks)      */
  uint64_t arrival_order;  /**< FCFS ordering                */
  int queue_level;         /**< MLQ queue level (0=highest)  */
  int tickets;             /**< Lottery tickets              */

  /* Statistics */
  uint64_t switches;
  uint64_t total_ticks;
  uint64_t last_start_tick;
  uint64_t wait_ticks;
  uint64_t first_run_tick;
  uint64_t finish_tick;
  uint64_t creation_tick;
} Task;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void SCHED_SetPolicy(SchedPolicy policy);
const char *SCHED_PolicyName(SchedPolicy policy);
Status SCHED_Init(void);
void SCHED_ResetAll(void);

int SCHED_CreateTask(const char *name, TaskFunc entry, void *arg,
                     size_t stack_size);

void SCHED_SetTaskPriority(int task_id, int priority);
void SCHED_SetTaskBurst(int task_id, uint64_t burst_ticks);
void SCHED_SetTaskQueueLevel(int task_id, int level);
void SCHED_SetTaskTickets(int task_id, int tickets);

void SCHED_Yield(void);
void SCHED_Exit(void);
Status SCHED_Run(void);

int SCHED_GetCurrentTaskId(void);
int SCHED_GetAliveCount(void);
uint64_t SCHED_GetTotalSwitches(void);
const Task *SCHED_GetTasks(void);
int SCHED_GetTaskCount(void);
void SCHED_PrintStats(void);

/* Context switch (assembly) */
extern void context_switch(uint64_t **old_sp, uint64_t *new_sp);

#endif /* MINIOS_KERNEL_SCHED_H */
