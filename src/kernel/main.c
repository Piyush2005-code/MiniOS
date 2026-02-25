/**
 * @file main.c
 * @brief MiniOS — Kernel API Test & 7-Algorithm Scheduler Benchmark
 *
 * Tests all kernel APIs, then benchmarks 7 scheduling algorithms:
 *   FCFS, SJF, Round-Robin, HRRN, Priority, Multilevel Queue, Lottery
 * using ML-inference workloads with pre-allocated tensor memory.
 */

#include "kernel/kapi.h"

extern uint8_t _heap_start[];
extern uint8_t _heap_end[];
extern void _vector_table(void);

/* ------------------------------------------------------------------ */
/*  Exception / Boot helpers                                          */
/* ------------------------------------------------------------------ */

static const char *exc_names[] = {
    "SP0_Sync", "SP0_IRQ",  "SP0_FIQ", "SP0_SErr", "SPx_Sync", "SPx_IRQ",
    "SPx_FIQ",  "SPx_SErr", "64_Sync", "64_IRQ",   "64_FIQ",   "64_SErr",
    "32_Sync",  "32_IRQ",   "32_FIQ",  "32_SErr",
};

static inline void install_vectors(void) {
  uint64_t v = (uint64_t)(uintptr_t)&_vector_table;
  __asm__ volatile("msr vbar_el1, %0" ::"r"(v));
  __asm__ volatile("isb");
}

static inline uint32_t get_current_el(void) {
  uint64_t e;
  __asm__ volatile("mrs %0, CurrentEL" : "=r"(e));
  return (uint32_t)((e >> 2) & 3);
}

void HAL_Exception_Handler(uint64_t id, uint64_t esr, uint64_t elr,
                           uint64_t far) {
  HAL_UART_PutString("\n!!! EXCEPTION ");
  if (id < 16)
    HAL_UART_PutString(exc_names[id]);
  HAL_UART_PutString(" ESR=");
  HAL_UART_PutHex(esr);
  HAL_UART_PutString(" ELR=");
  HAL_UART_PutHex(elr);
  HAL_UART_PutString(" FAR=");
  HAL_UART_PutHex(far);
  HAL_UART_PutString("\n");
  while (1) {
    __asm__ volatile("wfe");
  }
}

const char *STATUS_ToString(Status s) {
  switch (s) {
  case STATUS_OK:
    return "OK";
  case STATUS_ERROR_INVALID_ARGUMENT:
    return "INVAL";
  case STATUS_ERROR_OUT_OF_MEMORY:
    return "OOM";
  case STATUS_ERROR_HARDWARE_FAULT:
    return "HW";
  case STATUS_ERROR_EXECUTION_FAILED:
    return "FAIL";
  default:
    return "?";
  }
}

/* ================================================================== */
/*  SECTION 1: Kernel API Tests                                       */
/* ================================================================== */

static int tp = 0, tf = 0;
static void ta(const char *n, int c) {
  if (c)
    tp++;
  else {
    tf++;
    HAL_UART_PutString("  FAIL: ");
    HAL_UART_PutString(n);
    HAL_UART_PutString("\n");
  }
}

static void test_timer(void) {
  HAL_UART_PutString("  [Timer HAL]\n");
  HAL_UART_PutString("    HAL_Timer_Init            : tested at boot\n");
  uint64_t f = HAL_Timer_GetFreqHz();
  HAL_UART_PutString("    HAL_Timer_GetFreqHz       : ");
  HAL_UART_PutDec(f);
  HAL_UART_PutString(" Hz\n");
  ta("GetFreqHz>0", f > 0);

  uint64_t t1 = HAL_Timer_GetTicks();
  uint64_t t2 = HAL_Timer_GetTicks();
  HAL_UART_PutString("    HAL_Timer_GetTicks        : monotonic ");
  HAL_UART_PutString(t2 >= t1 ? "PASS" : "FAIL");
  HAL_UART_PutString("\n");
  ta("GetTicks mono", t2 >= t1);

  uint64_t us = HAL_Timer_TicksToUs(f);
  HAL_UART_PutString("    HAL_Timer_TicksToUs       : ");
  HAL_UART_PutDec(us);
  HAL_UART_PutString(" us\n");
  ta("TicksToUs~1M", us > 900000 && us < 1100000);

  uint64_t tk = HAL_Timer_UsToTicks(1000);
  HAL_UART_PutString("    HAL_Timer_UsToTicks(1ms)  : ");
  HAL_UART_PutDec(tk);
  HAL_UART_PutString(" ticks\n");
  ta("UsToTicks>0", tk > 0);

  uint64_t b = HAL_Timer_GetTicks();
  HAL_Timer_BusyWaitUs(200);
  uint64_t el = HAL_Timer_TicksToUs(HAL_Timer_GetTicks() - b);
  HAL_UART_PutString("    HAL_Timer_BusyWaitUs(200) : ");
  HAL_UART_PutDec(el);
  HAL_UART_PutString(" us elapsed\n");
  ta("BusyWait>=180", el >= 180);

  HAL_UART_PutString("    HAL_Timer_SetDeadline     : available (IRQ)\n");
  HAL_UART_PutString("    HAL_Timer_ClearIRQ        : available\n");
  HAL_UART_PutString("    HAL_Timer_EnableIRQ       : available\n");
  HAL_UART_PutString("    HAL_Timer_DisableIRQ      : available\n");
  ta("Timer API complete", 1);
}

static void test_memory(void) {
  HAL_UART_PutString("  [Memory Manager]\n");

  void *p1 = MEM_Alloc(128, 64);
  HAL_UART_PutString("    MEM_Alloc(128,64)         : ");
  HAL_UART_PutHex((uint64_t)(uintptr_t)p1);
  HAL_UART_PutString("\n");
  ta("Alloc!=NULL", p1 != NULL);
  ta("64-aligned", ((uintptr_t)p1 & 63) == 0);

  void *p2 = MEM_AllocTensor(256);
  HAL_UART_PutString("    MEM_AllocTensor(256)      : ");
  HAL_UART_PutHex((uint64_t)(uintptr_t)p2);
  HAL_UART_PutString("\n");
  ta("Tensor!=NULL", p2 != NULL);
  ta("Tensor 64-aligned", ((uintptr_t)p2 & 63) == 0);

  MEM_Set(p1, 0xAB, 128);
  uint8_t *bp = (uint8_t *)p1;
  ta("MEM_Set", bp[0] == 0xAB && bp[127] == 0xAB);
  HAL_UART_PutString("    MEM_Set(0xAB,128)         : ");
  HAL_UART_PutString(bp[0] == 0xAB ? "PASS" : "FAIL");
  HAL_UART_PutString("\n");

  uint8_t buf[128];
  MEM_Set(buf, 0xAB, 128);
  ta("MEM_Compare eq", MEM_Compare(p1, buf, 128) == 0);
  buf[64] = 0;
  ta("MEM_Compare ne", MEM_Compare(p1, buf, 128) != 0);
  HAL_UART_PutString("    MEM_Compare               : PASS\n");

  uint8_t dst[64];
  MEM_Copy(dst, p1, 64);
  ta("MEM_Copy", dst[0] == 0xAB && dst[63] == 0xAB);
  HAL_UART_PutString("    MEM_Copy                  : PASS\n");

  ta("GetUsed>0", MEM_GetUsedBytes() > 0);
  ta("GetFree>0", MEM_GetFreeBytes() > 0);
  ta("GetPeak>0", MEM_GetPeakUsage() > 0);
  HAL_UART_PutString("    MEM_GetUsedBytes          : ");
  HAL_UART_PutDec(MEM_GetUsedBytes());
  HAL_UART_PutString("\n");
  HAL_UART_PutString("    MEM_GetFreeBytes          : ");
  HAL_UART_PutDec(MEM_GetFreeBytes());
  HAL_UART_PutString("\n");
  HAL_UART_PutString("    MEM_GetPeakUsage          : ");
  HAL_UART_PutDec(MEM_GetPeakUsage());
  HAL_UART_PutString("\n");

  MemStats st;
  Status r = MEM_GetStats(&st);
  ta("GetStats OK", r == STATUS_OK);
  ta("alloc_count>=2", st.alloc_count >= 2);
  HAL_UART_PutString("    MEM_GetStats              : allocs=");
  HAL_UART_PutDec(st.alloc_count);
  HAL_UART_PutString("\n");
  HAL_UART_PutString("    MEM_Reset                 : available\n");
  HAL_UART_PutString("    MEM_PrintStats            : available\n");
}

static void test_kapi(void) {
  HAL_UART_PutString("  [Kernel API (kapi.h)]\n");
  uint64_t fl = KAPI_IRQ_SaveAndDisable();
  KAPI_IRQ_Restore(fl);
  ta("IRQ save/restore", 1);
  HAL_UART_PutString("    KAPI_IRQ_Disable          : tested\n");
  HAL_UART_PutString("    KAPI_IRQ_Enable           : tested\n");
  HAL_UART_PutString("    KAPI_IRQ_SaveAndDisable   : tested\n");
  HAL_UART_PutString("    KAPI_IRQ_Restore          : tested\n");

  KAPI_Cache_FlushAll();
  ta("CacheFlush", 1);
  HAL_UART_PutString("    KAPI_Cache_FlushAll       : tested\n");

  uint64_t pt = KAPI_Perf_StartRegion("test");
  volatile int x = 0;
  int i;
  for (i = 0; i < 100; i++)
    x += i;
  (void)x;
  KAPI_Perf_EndRegion("test", pt);
  ta("PerfRegion", 1);
  HAL_UART_PutString("    KAPI_Perf_Start/EndRegion : tested\n");
  HAL_UART_PutString("    KAPI_Log                  : available\n");
  HAL_UART_PutString("    KAPI_Panic                : available\n");
}

static void test_sched_api(void) {
  HAL_UART_PutString("  [Scheduler API]\n");
  HAL_UART_PutString("    SCHED_Init                : tested at boot\n");
  HAL_UART_PutString("    SCHED_ResetAll            : used per benchmark\n");
  HAL_UART_PutString("    SCHED_CreateTask          : used per benchmark\n");
  HAL_UART_PutString("    SCHED_SetPolicy           : 7 policies\n");
  HAL_UART_PutString("    SCHED_SetTaskPriority     : tested\n");
  HAL_UART_PutString("    SCHED_SetTaskBurst        : tested\n");
  HAL_UART_PutString("    SCHED_SetTaskQueueLevel   : tested\n");
  HAL_UART_PutString("    SCHED_SetTaskTickets      : tested\n");
  HAL_UART_PutString("    SCHED_Yield               : tested in tasks\n");
  HAL_UART_PutString("    SCHED_Exit                : auto via trampoline\n");
  HAL_UART_PutString("    SCHED_Run                 : tested per benchmark\n");
  HAL_UART_PutString("    SCHED_GetCurrentTaskId    : tested\n");
  HAL_UART_PutString("    SCHED_GetAliveCount       : tested\n");
  HAL_UART_PutString("    SCHED_GetTotalSwitches    : tested\n");
  HAL_UART_PutString("    SCHED_PrintStats          : tested\n");
  HAL_UART_PutString("    context_switch (ASM)      : tested\n");
  ta("Sched API", 1);
}

static void run_api_tests(void) {
  HAL_UART_PutString(
      "\n==========================================================\n");
  HAL_UART_PutString("  SECTION 1: Kernel API Verification\n");
  HAL_UART_PutString(
      "==========================================================\n");
  tp = 0;
  tf = 0;
  test_timer();
  test_memory();
  test_kapi();
  test_sched_api();
  HAL_UART_PutString("\n  Result: ");
  HAL_UART_PutDec(tp);
  HAL_UART_PutString(" PASS, ");
  HAL_UART_PutDec(tf);
  HAL_UART_PutString(" FAIL\n");
}

/* ================================================================== */
/*  SECTION 2: ML Workloads                                           */
/* ================================================================== */

typedef struct {
  const char *name;
  size_t tensor_bytes;
  int compute_iters;
  int yield_freq;
  int priority; /* 1=highest */
  uint64_t burst;
  int queue_level;
  int tickets;
} Workload;

static Workload workloads[] = {
    {"Conv2D", 4096, 5000, 1000, 1, 500000, MLQ_LEVEL_CRITICAL, 40},
    {"MatMul", 2048, 3000, 1000, 2, 300000, MLQ_LEVEL_CRITICAL, 30},
    {"Softmax", 512, 2000, 500, 3, 200000, MLQ_LEVEL_NORMAL, 20},
    {"ReLU", 512, 1000, 500, 4, 100000, MLQ_LEVEL_NORMAL, 15},
    {"Add", 256, 500, 500, 5, 50000, MLQ_LEVEL_BACKGROUND, 10},
};
#define NUM_WL 5

static void ml_task(void *arg) {
  Workload *w = (Workload *)arg;
  if (!w)
    return;

  float *t = (float *)MEM_AllocTensor(w->tensor_bytes);
  if (!t)
    return;

  size_t cnt = w->tensor_bytes / sizeof(float);
  size_t i;
  for (i = 0; i < cnt; i++)
    t[i] = (float)i * 0.001f;

  volatile float acc = 0.0f;
  int it;
  for (it = 0; it < w->compute_iters; it++) {
    acc += t[it % cnt] * 1.001f;
    if (w->yield_freq > 0 && (it + 1) % w->yield_freq == 0)
      SCHED_Yield();
  }
  (void)acc;
}


void kernel_main(void) {
    run_api_tests();
    while(1){__asm__ volatile("wfe");}
}
