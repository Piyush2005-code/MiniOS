/**
 * @file sched.c
 * @brief 7-Policy Cooperative Scheduler for MiniOS
 *
 * Implements: FCFS, SJF, Round-Robin, HRRN, Priority,
 *             Multilevel Queue, Lottery.
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
static int s_current = -1;
static int s_alive_count = 0;
static uint64_t s_total_switches = 0;
static uint64_t s_arrival_counter = 0;
static SchedPolicy s_policy = SCHED_POLICY_RR;
static uint64_t *s_main_sp = NULL;

/* Simple PRNG state for Lottery scheduling */
static uint32_t s_rng_state = 12345;

static void task_trampoline(void);

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static void str_copy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i] != '\0') {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

/** Simple xorshift32 PRNG */
static uint32_t prng_next(void) {
  s_rng_state ^= s_rng_state << 13;
  s_rng_state ^= s_rng_state >> 17;
  s_rng_state ^= s_rng_state << 5;
  return s_rng_state;
}

/* ------------------------------------------------------------------ */
/*  Policy: FCFS — lowest arrival_order                               */
/* ------------------------------------------------------------------ */

static int find_next_fcfs(void) {
  int best = -1;
  uint64_t best_arrival = (uint64_t)-1;
  int i;
  for (i = 0; i < s_num_tasks; i++) {
    if (s_tasks[i].state == TASK_READY &&
        s_tasks[i].arrival_order < best_arrival) {
      best_arrival = s_tasks[i].arrival_order;
      best = i;
    }
  }
  return best;
}

/* ------------------------------------------------------------------ */
/*  Policy: SJF — lowest burst_estimate                               */
/* ------------------------------------------------------------------ */

static int find_next_sjf(void) {
  int best = -1;
  uint64_t best_burst = (uint64_t)-1;
  int i;
  for (i = 0; i < s_num_tasks; i++) {
    if (s_tasks[i].state == TASK_READY &&
        s_tasks[i].burst_estimate < best_burst) {
      best_burst = s_tasks[i].burst_estimate;
      best = i;
    }
  }
  return best;
}

/* ------------------------------------------------------------------ */
/*  Policy: Round-Robin — cyclic                                      */
/* ------------------------------------------------------------------ */

static int find_next_rr(int start) {
  int i;
  for (i = 1; i <= s_num_tasks; i++) {
    int idx = (start + i) % s_num_tasks;
    if (s_tasks[idx].state == TASK_READY) {
      return idx;
    }
  }
  return -1;
}

/* ------------------------------------------------------------------ */
/*  Policy: HRRN — Highest Response Ratio Next                        */
/*  RR = (wait_time + burst_estimate) / burst_estimate                */
/* ------------------------------------------------------------------ */

static int find_next_hrrn(void) {
  int best = -1;
  uint64_t best_ratio_num = 0; /* numerator × denom comparison */
  uint64_t best_ratio_den = 1;
  uint64_t now = HAL_Timer_GetTicks();
  int i;

  for (i = 0; i < s_num_tasks; i++) {
    if (s_tasks[i].state != TASK_READY)
      continue;

    uint64_t wait = 0;
    if (now > s_tasks[i].creation_tick) {
      wait = now - s_tasks[i].creation_tick;
    }
    /* Subtract runtime already consumed from wait */
    if (wait > s_tasks[i].total_ticks) {
      wait -= s_tasks[i].total_ticks;
    } else {
      wait = 0;
    }

    uint64_t burst = s_tasks[i].burst_estimate;
    if (burst == 0)
      burst = 1;

    /* Response ratio = (wait + burst) / burst
     * Compare: (wait_i + burst_i) * burst_best > (wait_best + burst_best) *
     * burst_i Using cross-multiplication to avoid division */
    uint64_t num = wait + burst;
    uint64_t den = burst;

    if (best < 0 || num * best_ratio_den > best_ratio_num * den) {
      best = i;
      best_ratio_num = num;
      best_ratio_den = den;
    }
  }
  return best;
}

/* ------------------------------------------------------------------ */
/*  Policy: Priority — lowest priority value wins                     */
/* ------------------------------------------------------------------ */

static int find_next_priority(void) {
  int best = -1;
  int best_prio = 0x7FFFFFFF;
  int i;
  for (i = 0; i < s_num_tasks; i++) {
    if (s_tasks[i].state == TASK_READY && s_tasks[i].priority < best_prio) {
      best_prio = s_tasks[i].priority;
      best = i;
    }
  }
  return best;
}

/* ------------------------------------------------------------------ */

static int find_next_ready(int current_idx) {
    if (s_policy == SCHED_POLICY_FCFS) return find_next_fcfs();
    if (s_policy == SCHED_POLICY_SJF) return find_next_sjf();
    if (s_policy == SCHED_POLICY_HRRN) return find_next_hrrn();
    if (s_policy == SCHED_POLICY_PRIORITY) return find_next_priority();
    return find_next_rr(current_idx);
}
void SCHED_SetPolicy(SchedPolicy policy) { s_policy = policy; }
const char* SCHED_PolicyName(SchedPolicy policy) { return "Policy"; }
 {
  int i;
  for (i = 0; i < SCHED_MAX_TASKS; i++) {
    s_tasks[i].id = i;
    s_tasks[i].state = TASK_UNUSED;
    s_tasks[i].sp = NULL;
    s_tasks[i].stack_base = NULL;
    s_tasks[i].priority = 10;
    s_tasks[i].burst_estimate = 100000;
    s_tasks[i].arrival_order = 0;
    s_tasks[i].queue_level = MLQ_LEVEL_NORMAL;
    s_tasks[i].tickets = 10;
    s_tasks[i].switches = 0;
    s_tasks[i].total_ticks = 0;
    s_tasks[i].wait_ticks = 0;
    s_tasks[i].first_run_tick = 0;
    s_tasks[i].finish_tick = 0;
    s_tasks[i].creation_tick = 0;
  }
  s_num_tasks = 0;
  s_current = -1;
  s_alive_count = 0;
  s_total_switches = 0;
  s_arrival_counter = 0;
  s_main_sp = NULL;
  return STATUS_OK;
}

void SCHED_ResetAll(void) {
  SCHED_Init();
  /* Reset PRNG to consistent seed for reproducibility */
  s_rng_state = 12345;
}

int SCHED_CreateTask(const char *name, TaskFunc entry, void *arg,
                     size_t stack_size) {
  if (s_num_tasks >= SCHED_MAX_TASKS || entry == NULL)
    return -1;
  if (stack_size == 0)
    stack_size = SCHED_DEFAULT_STACK;

  int id = s_num_tasks;
  Task *t = &s_tasks[id];

  t->stack_base = (uint8_t *)MEM_Alloc(stack_size, 16);
  if (t->stack_base == NULL)
    return -1;

  t->stack_size = stack_size;
  t->entry = entry;
  t->arg = arg;
  t->state = TASK_READY;
  t->priority = 10;
  t->burst_estimate = 100000;
  t->arrival_order = s_arrival_counter++;
  t->queue_level = MLQ_LEVEL_NORMAL;
  t->tickets = 10;
  t->switches = 0;
  t->total_ticks = 0;
  t->wait_ticks = 0;
  t->first_run_tick = 0;
  t->finish_tick = 0;
  t->creation_tick = HAL_Timer_GetTicks();

  if (name != NULL) {
    str_copy(t->name, name, SCHED_TASK_NAME_LEN);
  } else {
    t->name[0] = 'T';
    t->name[1] = '0' + (char)(id % 10);
    t->name[2] = '\0';
  }

  uint64_t *stack_top = (uint64_t *)(t->stack_base + stack_size);
  stack_top -= 12;
  int i;
  for (i = 0; i < 12; i++)
    stack_top[i] = 0;
  stack_top[11] = (uint64_t)(uintptr_t)task_trampoline;
  stack_top[0] = (uint64_t)id;
  t->sp = stack_top;

  s_num_tasks++;
  s_alive_count++;
  return id;
}

void SCHED_SetTaskPriority(int id, int prio) {
  if (id >= 0 && id < s_num_tasks)
    s_tasks[id].priority = prio;
}

void SCHED_SetTaskBurst(int id, uint64_t ticks) {
  if (id >= 0 && id < s_num_tasks)
    s_tasks[id].burst_estimate = ticks;
}

void SCHED_SetTaskQueueLevel(int id, int level) {
  if (id >= 0 && id < s_num_tasks)
    s_tasks[id].queue_level = level;
}

void SCHED_SetTaskTickets(int id, int tickets) {
  if (id >= 0 && id < s_num_tasks)
    s_tasks[id].tickets = tickets;
}

void SCHED_Yield(void) {
  if (s_current < 0)
    return;
  Task *cur = &s_tasks[s_current];

  uint64_t now = HAL_Timer_GetTicks();
  if (cur->last_start_tick > 0)
    cur->total_ticks += (now - cur->last_start_tick);

  cur->state = TASK_READY;

  int next = find_next_ready(s_current);
  if (next < 0 || next == s_current) {
    cur->state = TASK_RUNNING;
    return;
  }

  Task *nt = &s_tasks[next];
  nt->state = TASK_RUNNING;
  nt->switches++;
  nt->last_start_tick = HAL_Timer_GetTicks();
  if (nt->first_run_tick == 0)
    nt->first_run_tick = nt->last_start_tick;

  int old = s_current;
  s_current = next;
  s_total_switches++;
  context_switch(&s_tasks[old].sp, nt->sp);
}

void SCHED_Exit(void) {
  if (s_current < 0)
    return;
  Task *cur = &s_tasks[s_current];

  uint64_t now = HAL_Timer_GetTicks();
  if (cur->last_start_tick > 0)
    cur->total_ticks += (now - cur->last_start_tick);

  cur->state = TASK_FINISHED;
  cur->finish_tick = now;
  s_alive_count--;

  if (s_alive_count <= 0) {
    if (s_main_sp != NULL) {
      uint64_t *dummy;
      context_switch((uint64_t **)&dummy, s_main_sp);
    }
    while (1) {
      __asm__ volatile("wfe");
    }
  }

  int next = find_next_ready(s_current);
  if (next >= 0) {
    Task *nt = &s_tasks[next];
    nt->state = TASK_RUNNING;
    nt->switches++;
    nt->last_start_tick = HAL_Timer_GetTicks();
    if (nt->first_run_tick == 0)
      nt->first_run_tick = nt->last_start_tick;

    int old = s_current;
    s_current = next;
    s_total_switches++;
    context_switch(&s_tasks[old].sp, nt->sp);
  }

  if (s_main_sp != NULL) {
    uint64_t *dummy;
    context_switch((uint64_t **)&dummy, s_main_sp);
  }
  while (1) {
    __asm__ volatile("wfe");
  }
}

Status SCHED_Run(void) {
  if (s_num_tasks == 0)
    return STATUS_OK;

  int first = find_next_ready(-1);
  if (first < 0)
    return STATUS_ERROR_EXECUTION_FAILED;

  Task *t = &s_tasks[first];
  t->state = TASK_RUNNING;
  t->switches++;
  t->last_start_tick = HAL_Timer_GetTicks();
  t->first_run_tick = t->last_start_tick;
  s_current = first;
  s_total_switches++;

  context_switch(&s_main_sp, t->sp);
  s_current = -1;
  return STATUS_OK;
}

int SCHED_GetCurrentTaskId(void) { return s_current; }
int SCHED_GetAliveCount(void) { return s_alive_count; }
uint64_t SCHED_GetTotalSwitches(void) { return s_total_switches; }
const Task *SCHED_GetTasks(void) { return s_tasks; }
int SCHED_GetTaskCount(void) { return s_num_tasks; }

static void task_trampoline(void) {
  uint64_t task_id;
  __asm__ volatile("mov %0, x19" : "=r"(task_id));
  if (task_id < (uint64_t)SCHED_MAX_TASKS) {
    Task *t = &s_tasks[task_id];
    if (t->entry != NULL)
      t->entry(t->arg);
  }
  SCHED_Exit();
  while (1) {
    __asm__ volatile("wfe");
  }
}

void SCHED_PrintStats(void) {
  int i;
  HAL_UART_PutString("  Pol=");
  HAL_UART_PutString(SCHED_PolicyName(s_policy));
  HAL_UART_PutString(" CSw=");
  HAL_UART_PutDec(s_total_switches);
  HAL_UART_PutString("\n");

  for (i = 0; i < s_num_tasks; i++) {
    Task *t = &s_tasks[i];
    uint64_t run_us = HAL_Timer_TicksToUs(t->total_ticks);
    uint64_t turn_us = 0, resp_us = 0;

    if (t->finish_tick > t->creation_tick)
      turn_us = HAL_Timer_TicksToUs(t->finish_tick - t->creation_tick);
    if (t->first_run_tick > t->creation_tick)
      resp_us = HAL_Timer_TicksToUs(t->first_run_tick - t->creation_tick);

    HAL_UART_PutString("  T");
    HAL_UART_PutDec(i);
    HAL_UART_PutString(" ");
    HAL_UART_PutString(t->name);
    HAL_UART_PutString(" p=");
    HAL_UART_PutDec(t->priority);
    HAL_UART_PutString(" sw=");
    HAL_UART_PutDec(t->switches);
    HAL_UART_PutString(" run=");
    HAL_UART_PutDec(run_us);
    HAL_UART_PutString("us turn=");
    HAL_UART_PutDec(turn_us);
    HAL_UART_PutString("us resp=");
    HAL_UART_PutDec(resp_us);
    HAL_UART_PutString("us\n");
  }
}
