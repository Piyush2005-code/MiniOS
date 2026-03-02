/**
 * @file sched.c
 * @brief Cooperative Scheduler implementation for MiniOS
 *
 * Implements a round-robin cooperative scheduler. Tasks yield
 * explicitly via SCHED_Yield(). Context switching is performed
 * by context_switch.S which saves/restores callee-saved registers.
 *
 * Task stacks are allocated from the kernel heap via MEM_Alloc().
 * A trampoline function wraps the task entry so that when the
 * entry function returns, SCHED_Exit() is called automatically.
 *
 * @note Per SRS FR-011 (cooperative execution)
 */

#include "kernel/sched.h"
#include "hal/timer.h"
#include "hal/uart.h"
#include "kernel/mem.h"

/* ------------------------------------------------------------------ */
/*  Internal state                                                    */
/* ------------------------------------------------------------------ */

static Task s_tasks[SCHED_MAX_TASKS];
static int s_num_tasks = 0;
static int s_current = -1; /* Index of running task       */
static int s_alive_count = 0;
static uint64_t s_total_switches = 0;

/* Main context — the scheduler returns here when all tasks done */
static uint64_t *s_main_sp = NULL;

/* Forward declaration */
static void task_trampoline(void);

/* ------------------------------------------------------------------ */
/*  String helper (no libc)                                           */
/* ------------------------------------------------------------------ */

static void str_copy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i] != '\0') {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status SCHED_Init(void) {
  int i;
  for (i = 0; i < SCHED_MAX_TASKS; i++) {
    s_tasks[i].id = i;
    s_tasks[i].state = TASK_UNUSED;
    s_tasks[i].sp = NULL;
    s_tasks[i].stack_base = NULL;
    s_tasks[i].switches = 0;
    s_tasks[i].total_ticks = 0;
  }

  s_num_tasks = 0;
  s_current = -1;
  s_alive_count = 0;
  s_total_switches = 0;
  s_main_sp = NULL;

  HAL_UART_PutString("[SCHED] Scheduler initialized (max ");
  HAL_UART_PutDec(SCHED_MAX_TASKS);
  HAL_UART_PutString(" tasks)\n");

  return STATUS_OK;
}

int SCHED_CreateTask(const char *name, TaskFunc entry, void *arg,
                     size_t stack_size) {
  if (s_num_tasks >= SCHED_MAX_TASKS) {
    HAL_UART_PutString("[SCHED] ERROR: Maximum tasks reached\n");
    return -1;
  }

  if (entry == NULL) {
    HAL_UART_PutString("[SCHED] ERROR: NULL entry function\n");
    return -1;
  }

  /* Use default stack size if not specified */
  if (stack_size == 0) {
    stack_size = SCHED_DEFAULT_STACK;
  }

  int id = s_num_tasks;
  Task *t = &s_tasks[id];

  /* Allocate stack from kernel heap (16-byte aligned per AAPCS64) */
  t->stack_base = (uint8_t *)MEM_Alloc(stack_size, 16);
  if (t->stack_base == NULL) {
    HAL_UART_PutString("[SCHED] ERROR: Failed to allocate stack\n");
    return -1;
  }

  t->stack_size = stack_size;
  t->entry = entry;
  t->arg = arg;
  t->state = TASK_READY;
  t->switches = 0;
  t->total_ticks = 0;
  t->last_start_tick = 0;

  if (name != NULL) {
    str_copy(t->name, name, SCHED_TASK_NAME_LEN);
  } else {
    t->name[0] = 'T';
    t->name[1] = '0' + (char)(id % 10);
    t->name[2] = '\0';
  }

  /*
   * Set up the initial stack frame so that context_switch()
   * will "return" to task_trampoline.
   *
   * Stack grows downward. We set SP to the top of the stack
   * minus space for the saved context frame.
   *
   * Context frame layout (pushed by context_switch):
   *   SP + 0:   x19
   *   SP + 8:   x20
   *   SP + 16:  x21
   *   SP + 24:  x22
   *   SP + 32:  x23
   *   SP + 40:  x24
   *   SP + 48:  x25
   *   SP + 56:  x26
   *   SP + 64:  x27
   *   SP + 72:  x28
   *   SP + 80:  x29 (frame pointer)
   *   SP + 88:  x30 (link register / return address)
   */
  uint64_t *stack_top = (uint64_t *)(t->stack_base + stack_size);

  /* Reserve space for 12 registers (x19-x30) */
  stack_top -= 12;

  /* Zero-fill the saved context */
  int i;
  for (i = 0; i < 12; i++) {
    stack_top[i] = 0;
  }

  /* Set x30 (LR) to the trampoline so the task "starts" there */
  stack_top[11] = (uint64_t)(uintptr_t)task_trampoline;

  /* Save the task ID in x19 so the trampoline can find it */
  stack_top[0] = (uint64_t)id;

  t->sp = stack_top;

  s_num_tasks++;
  s_alive_count++;

  HAL_UART_PutString("[SCHED] Created task ");
  HAL_UART_PutDec(id);
  HAL_UART_PutString(": \"");
  HAL_UART_PutString(t->name);
  HAL_UART_PutString("\" (stack=");
  HAL_UART_PutDec(stack_size / 1024);
  HAL_UART_PutString("KB)\n");

  return id;
}

/**
 * Find the next ready task using round-robin.
 * Returns -1 if no ready task exists.
 */
static int find_next_ready(int start) {
  int i;
  for (i = 1; i <= s_num_tasks; i++) {
    int idx = (start + i) % s_num_tasks;
    if (s_tasks[idx].state == TASK_READY) {
      return idx;
    }
  }
  return -1;
}

void SCHED_Yield(void) {
  if (s_current < 0)
    return;

  Task *current = &s_tasks[s_current];

  /* Record runtime for the current task */
  uint64_t now = HAL_Timer_GetTicks();
  if (current->last_start_tick > 0) {
    current->total_ticks += (now - current->last_start_tick);
  }

  /* Mark current as READY (it wants to run again) */
  current->state = TASK_READY;

  /* Find next ready task */
  int next = find_next_ready(s_current);

  if (next < 0 || next == s_current) {
    /* No other task ready; keep running */
    current->state = TASK_RUNNING;
    return;
  }

  /* Switch to the next task */
  Task *next_task = &s_tasks[next];
  next_task->state = TASK_RUNNING;
  next_task->switches++;
  next_task->last_start_tick = HAL_Timer_GetTicks();

  int old_current = s_current;
  s_current = next;
  s_total_switches++;

  context_switch(&s_tasks[old_current].sp, next_task->sp);
}

void SCHED_Exit(void) {
  if (s_current < 0)
    return;

  Task *current = &s_tasks[s_current];

  /* Record final runtime */
  uint64_t now = HAL_Timer_GetTicks();
  if (current->last_start_tick > 0) {
    current->total_ticks += (now - current->last_start_tick);
  }

  current->state = TASK_FINISHED;
  s_alive_count--;

  HAL_UART_PutString("[SCHED] Task ");
  HAL_UART_PutDec(s_current);
  HAL_UART_PutString(" (\"");
  HAL_UART_PutString(current->name);
  HAL_UART_PutString("\") finished\n");

  if (s_alive_count <= 0) {
    /* All tasks done — switch back to main context */
    HAL_UART_PutString("[SCHED] All tasks completed\n");
    if (s_main_sp != NULL) {
      uint64_t *dummy_sp;
      context_switch((uint64_t **)&dummy_sp, s_main_sp);
    }
    /* Should not reach here */
    while (1) {
      __asm__ volatile("wfe");
    }
  }

  /* Find next ready task */
  int next = find_next_ready(s_current);
  if (next >= 0) {
    Task *next_task = &s_tasks[next];
    next_task->state = TASK_RUNNING;
    next_task->switches++;
    next_task->last_start_tick = HAL_Timer_GetTicks();

    int old_current = s_current;
    s_current = next;
    s_total_switches++;

    context_switch(&s_tasks[old_current].sp, next_task->sp);
  }

  /* No tasks left — return to main */
  if (s_main_sp != NULL) {
    uint64_t *dummy_sp;
    context_switch((uint64_t **)&dummy_sp, s_main_sp);
  }
  while (1) {
    __asm__ volatile("wfe");
  }
}

Status SCHED_Run(void) {
  if (s_num_tasks == 0) {
    HAL_UART_PutString("[SCHED] No tasks to run\n");
    return STATUS_OK;
  }

  HAL_UART_PutString("[SCHED] Starting scheduler with ");
  HAL_UART_PutDec(s_num_tasks);
  HAL_UART_PutString(" tasks\n");

  /* Find the first ready task */
  int first = find_next_ready(-1);
  if (first < 0) {
    HAL_UART_PutString("[SCHED] No ready tasks found\n");
    return STATUS_ERROR_EXECUTION_FAILED;
  }

  /* Set up the first task */
  Task *t = &s_tasks[first];
  t->state = TASK_RUNNING;
  t->switches++;
  t->last_start_tick = HAL_Timer_GetTicks();
  s_current = first;
  s_total_switches++;

  /*
   * Switch from main context to the first task.
   * When all tasks complete, SCHED_Exit() will switch
   * back to s_main_sp, resuming execution after this call.
   */
  context_switch(&s_main_sp, t->sp);

  /* Execution resumes here when all tasks are done */
  s_current = -1;
  return STATUS_OK;
}

int SCHED_GetCurrentTaskId(void) { return s_current; }

int SCHED_GetAliveCount(void) { return s_alive_count; }

/**
 * Task trampoline: this is the first code executed by a new task.
 * x19 contains the task ID (set up in SCHED_CreateTask).
 * We call the task's entry function, then SCHED_Exit().
 */
static void task_trampoline(void) {
  /*
   * After context_switch restores registers, x19 = task ID.
   * We read it via inline assembly.
   */
  uint64_t task_id;
  __asm__ volatile("mov %0, x19" : "=r"(task_id));

  if (task_id < (uint64_t)SCHED_MAX_TASKS) {
    Task *t = &s_tasks[task_id];
    if (t->entry != NULL) {
      t->entry(t->arg);
    }
  }

  /* Task function returned — exit cleanly */
  SCHED_Exit();

  /* Should never reach here */
  while (1) {
    __asm__ volatile("wfe");
  }
}

void SCHED_PrintStats(void) {
  int i;
  HAL_UART_PutString("[SCHED] ---- Scheduler Statistics ----\n");
  HAL_UART_PutString("[SCHED]   Total context switches: ");
  HAL_UART_PutDec(s_total_switches);
  HAL_UART_PutString("\n");

  for (i = 0; i < s_num_tasks; i++) {
    Task *t = &s_tasks[i];
    HAL_UART_PutString("[SCHED]   Task ");
    HAL_UART_PutDec(i);
    HAL_UART_PutString(" (\"");
    HAL_UART_PutString(t->name);
    HAL_UART_PutString("\"): switches=");
    HAL_UART_PutDec(t->switches);
    HAL_UART_PutString(", runtime=");
    HAL_UART_PutDec(HAL_Timer_TicksToUs(t->total_ticks));
    HAL_UART_PutString(" us, state=");
    switch (t->state) {
    case TASK_UNUSED:
      HAL_UART_PutString("UNUSED");
      break;
    case TASK_READY:
      HAL_UART_PutString("READY");
      break;
    case TASK_RUNNING:
      HAL_UART_PutString("RUNNING");
      break;
    case TASK_BLOCKED:
      HAL_UART_PutString("BLOCKED");
      break;
    case TASK_FINISHED:
      HAL_UART_PutString("FINISHED");
      break;
    }
    HAL_UART_PutString("\n");
  }
}
