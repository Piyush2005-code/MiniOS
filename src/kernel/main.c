/**
 * @file main.c
 * @brief MiniOS kernel entry point
 *
 * Initializes all hardware subsystems (UART, MMU, Timer),
 * then initializes kernel services (Memory Manager, Scheduler),
 * creates demo tasks to demonstrate cooperative multithreading,
 * and runs the scheduler.
 *
 * After all tasks complete, prints statistics and enters idle.
 */

#include "kernel/kapi.h"

/* ------------------------------------------------------------------ */
/*  External symbols from linker script                               */
/* ------------------------------------------------------------------ */
extern uint8_t _heap_start[];
extern uint8_t _heap_end[];

/* ------------------------------------------------------------------ */
/*  External symbols from vectors.S                                   */
/* ------------------------------------------------------------------ */
extern void _vector_table(void);

/* ------------------------------------------------------------------ */
/*  Exception handler names for pretty-printing                       */
/* ------------------------------------------------------------------ */
static const char *exception_names[] = {
    "EL1 SP0 Synchronous",     /*  0 */
    "EL1 SP0 IRQ",             /*  1 */
    "EL1 SP0 FIQ",             /*  2 */
    "EL1 SP0 SError",          /*  3 */
    "EL1 SPx Synchronous",     /*  4 */
    "EL1 SPx IRQ",             /*  5 */
    "EL1 SPx FIQ",             /*  6 */
    "EL1 SPx SError",          /*  7 */
    "EL0 AArch64 Synchronous", /*  8 */
    "EL0 AArch64 IRQ",         /*  9 */
    "EL0 AArch64 FIQ",         /* 10 */
    "EL0 AArch64 SError",      /* 11 */
    "EL0 AArch32 Synchronous", /* 12 */
    "EL0 AArch32 IRQ",         /* 13 */
    "EL0 AArch32 FIQ",         /* 14 */
    "EL0 AArch32 SError",      /* 15 */
};

/* ------------------------------------------------------------------ */
/*  Install exception vector table                                    */
/* ------------------------------------------------------------------ */
static inline void install_vectors(void) {
  uint64_t vbar = (uint64_t)(uintptr_t)&_vector_table;
  __asm__ volatile("msr vbar_el1, %0" ::"r"(vbar));
  __asm__ volatile("isb");
}

/* ------------------------------------------------------------------ */
/*  Read Current Exception Level                                      */
/* ------------------------------------------------------------------ */
static inline uint32_t get_current_el(void) {
  uint64_t el;
  __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
  return (uint32_t)((el >> 2) & 0x3);
}

/* ------------------------------------------------------------------ */
/*  Exception handler (called from vectors.S)                         */
/* ------------------------------------------------------------------ */
void HAL_Exception_Handler(uint64_t id, uint64_t esr, uint64_t elr,
                           uint64_t far) {
  HAL_UART_PutString("\n!!! EXCEPTION: ");
  if (id < 16) {
    HAL_UART_PutString(exception_names[id]);
  } else {
    HAL_UART_PutString("Unknown (");
    HAL_UART_PutDec(id);
    HAL_UART_PutString(")");
  }
  HAL_UART_PutString("\n");

  HAL_UART_PutString("  ESR_EL1 : ");
  HAL_UART_PutHex(esr);
  HAL_UART_PutString("\n");

  HAL_UART_PutString("  ELR_EL1 : ");
  HAL_UART_PutHex(elr);
  HAL_UART_PutString("\n");

  HAL_UART_PutString("  FAR_EL1 : ");
  HAL_UART_PutHex(far);
  HAL_UART_PutString("\n");

  HAL_UART_PutString("  System halted.\n");

  /* Halt forever */
  while (1) {
    __asm__ volatile("wfe");
  }
}

/* ------------------------------------------------------------------ */
/*  Status code to string conversion                                 */
/* ------------------------------------------------------------------ */
const char *STATUS_ToString(Status status) {
  switch (status) {
  case STATUS_OK:
    return "OK";
  case STATUS_ERROR_INVALID_ARGUMENT:
    return "INVALID_ARGUMENT";
  case STATUS_ERROR_NOT_SUPPORTED:
    return "NOT_SUPPORTED";
  case STATUS_ERROR_NOT_INITIALIZED:
    return "NOT_INITIALIZED";
  case STATUS_ERROR_OUT_OF_MEMORY:
    return "OUT_OF_MEMORY";
  case STATUS_ERROR_MEMORY_ALIGNMENT:
    return "MEMORY_ALIGNMENT";
  case STATUS_ERROR_MEMORY_PROTECTION:
    return "MEMORY_PROTECTION";
  case STATUS_ERROR_HARDWARE_FAULT:
    return "HARDWARE_FAULT";
  case STATUS_ERROR_TIMEOUT:
    return "TIMEOUT";
  case STATUS_ERROR_EXECUTION_FAILED:
    return "EXECUTION_FAILED";
  case STATUS_ERROR_EXECUTION_TIMEOUT:
    return "EXECUTION_TIMEOUT";
  case STATUS_ERROR_INVALID_GRAPH:
    return "INVALID_GRAPH";
  case STATUS_ERROR_UNSUPPORTED_OPERATOR:
    return "UNSUPPORTED_OPERATOR";
  case STATUS_ERROR_SHAPE_MISMATCH:
    return "SHAPE_MISMATCH";
  case STATUS_ERROR_COMM_FAILURE:
    return "COMM_FAILURE";
  case STATUS_ERROR_CRC_MISMATCH:
    return "CRC_MISMATCH";
  default:
    return "UNKNOWN";
  }
}

/* ================================================================== */
/*  Demo Tasks — Demonstrate cooperative multithreading               */
/* ================================================================== */

/**
 * Demo Task 1: Simulates ML preprocessing
 * Allocates a tensor, fills it, yields multiple times
 */
static void task_ml_preprocess(void *arg) {
  int task_id = SCHED_GetCurrentTaskId();
  (void)arg;

  HAL_UART_PutString("[TASK-");
  HAL_UART_PutDec(task_id);
  HAL_UART_PutString("] ML Preprocessing started\n");

  /* Allocate a small "tensor" */
  uint64_t t0 = KAPI_Perf_StartRegion("tensor_alloc");
  float *tensor = (float *)MEM_AllocTensor(256 * sizeof(float));
  KAPI_Perf_EndRegion("tensor_alloc", t0);

  if (tensor == NULL) {
    HAL_UART_PutString("[TASK-");
    HAL_UART_PutDec(task_id);
    HAL_UART_PutString("] ERROR: Failed to allocate tensor!\n");
    return;
  }

  /* Fill tensor (simulating preprocessing) */
  HAL_UART_PutString("[TASK-");
  HAL_UART_PutDec(task_id);
  HAL_UART_PutString("] Filling 256-element tensor...\n");

  int i;
  for (i = 0; i < 256; i++) {
    tensor[i] = (float)i * 0.01f;
  }

  /* Yield to let other tasks run */
  SCHED_Yield();

  HAL_UART_PutString("[TASK-");
  HAL_UART_PutDec(task_id);
  HAL_UART_PutString("] Preprocessing complete. tensor[0]=");
  /* Print integer part of tensor[255] as a quick check */
  HAL_UART_PutDec((uint64_t)(tensor[255] * 100));
  HAL_UART_PutString("/100\n");
}

/**
 * Demo Task 2: Simulates ML inference computation
 * Performs a simple dot-product style computation, yields periodically
 */
static void task_ml_inference(void *arg) {
  int task_id = SCHED_GetCurrentTaskId();
  int iterations = (arg != NULL) ? *((int *)arg) : 3;

  HAL_UART_PutString("[TASK-");
  HAL_UART_PutDec(task_id);
  HAL_UART_PutString("] ML Inference started (");
  HAL_UART_PutDec(iterations);
  HAL_UART_PutString(" iterations)\n");

  int iter;
  for (iter = 0; iter < iterations; iter++) {
    /* Simulate computation */
    volatile uint64_t sum = 0;
    int j;
    for (j = 0; j < 1000; j++) {
      sum += j;
    }

    HAL_UART_PutString("[TASK-");
    HAL_UART_PutDec(task_id);
    HAL_UART_PutString("] Inference iteration ");
    HAL_UART_PutDec(iter + 1);
    HAL_UART_PutString("/");
    HAL_UART_PutDec(iterations);
    HAL_UART_PutString(" done (sum=");
    HAL_UART_PutDec(sum);
    HAL_UART_PutString(")\n");

    /* Cooperative yield between iterations */
    SCHED_Yield();
  }

  HAL_UART_PutString("[TASK-");
  HAL_UART_PutDec(task_id);
  HAL_UART_PutString("] Inference complete\n");
}

/**
 * Demo Task 3: Simulates result post-processing
 */
static void task_postprocess(void *arg) {
  int task_id = SCHED_GetCurrentTaskId();
  (void)arg;

  HAL_UART_PutString("[TASK-");
  HAL_UART_PutDec(task_id);
  HAL_UART_PutString("] Post-processing started\n");

  /* Yield once to let inference finish first */
  SCHED_Yield();

  /* Allocate result buffer */
  uint8_t *result = (uint8_t *)MEM_Alloc(128, 16);
  if (result != NULL) {
    MEM_Set(result, 0x42, 128);
    HAL_UART_PutString("[TASK-");
    HAL_UART_PutDec(task_id);
    HAL_UART_PutString("] Result buffer filled: ");
    HAL_UART_PutHex(result[0]);
    HAL_UART_PutString("\n");
  }

  SCHED_Yield();

  HAL_UART_PutString("[TASK-");
  HAL_UART_PutDec(task_id);
  HAL_UART_PutString("] Post-processing complete\n");
}

/* ================================================================== */
/*  Kernel entry point                                                */
/* ================================================================== */

void kernel_main(void) {
  Status status;

  /* ---- Step 1: Initialize UART for serial output ---- */
  status = HAL_UART_Init();

  /* Print boot banner */
  HAL_UART_PutString("\n");
  HAL_UART_PutString("======================================\n");
  HAL_UART_PutString("  MiniOS v0.2 - ARM64 Unikernel\n");
  HAL_UART_PutString("  ML Inference for ARM64 Devices\n");
  HAL_UART_PutString("  With Kernel API + Multithreading\n");
  HAL_UART_PutString("======================================\n");
  HAL_UART_PutString("\n");

  /* Report UART status */
  HAL_UART_PutString("[BOOT] UART initialized: ");
  HAL_UART_PutString(STATUS_ToString(status));
  HAL_UART_PutString("\n");

  /* Report current exception level */
  uint32_t el = get_current_el();
  HAL_UART_PutString("[BOOT] Running at EL");
  HAL_UART_PutDec(el);
  HAL_UART_PutString("\n");

  /* ---- Step 2: Install exception vector table ---- */
  HAL_UART_PutString("[BOOT] Installing exception vectors...\n");
  install_vectors();
  HAL_UART_PutString("[BOOT] Exception vectors installed\n");

  /* ---- Step 3: Initialize MMU and caches ---- */
  HAL_UART_PutString("[BOOT] Initializing MMU...\n");
  status = HAL_MMU_Init();
  HAL_UART_PutString("[BOOT] MMU status: ");
  HAL_UART_PutString(STATUS_ToString(status));
  HAL_UART_PutString("\n");

  /* ---- Step 4: Initialize ARM Generic Timer ---- */
  HAL_UART_PutString("[BOOT] Initializing timer...\n");
  status = HAL_Timer_Init();
  HAL_UART_PutString("[BOOT] Timer status: ");
  HAL_UART_PutString(STATUS_ToString(status));
  HAL_UART_PutString("\n");

  /* ---- Step 5: Initialize Memory Manager ---- */
  HAL_UART_PutString("[BOOT] Initializing memory manager...\n");
  size_t heap_size = (size_t)(_heap_end - _heap_start);
  status = MEM_Init((void *)_heap_start, heap_size);
  HAL_UART_PutString("[BOOT] Memory status: ");
  HAL_UART_PutString(STATUS_ToString(status));
  HAL_UART_PutString("\n");

  /* ---- Step 6: Initialize Scheduler ---- */
  HAL_UART_PutString("[BOOT] Initializing scheduler...\n");
  status = SCHED_Init();
  HAL_UART_PutString("[BOOT] Scheduler status: ");
  HAL_UART_PutString(STATUS_ToString(status));
  HAL_UART_PutString("\n");

  /* ---- Boot complete ---- */
  HAL_UART_PutString("\n");
  HAL_UART_PutString("[BOOT] =============================\n");
  HAL_UART_PutString("[BOOT]  All subsystems initialized\n");
  HAL_UART_PutString("[BOOT] =============================\n");
  HAL_UART_PutString("\n");

  /* ---- Step 7: Create demo tasks ---- */
  HAL_UART_PutString("[BOOT] Creating demo tasks...\n");

  static int inference_iters = 3; /* static so pointer remains valid */

  SCHED_CreateTask("ML-Preprocess", task_ml_preprocess, NULL, 0);
  SCHED_CreateTask("ML-Inference", task_ml_inference, &inference_iters, 0);
  SCHED_CreateTask("Postprocess", task_postprocess, NULL, 0);

  /* ---- Step 8: Run the scheduler ---- */
  HAL_UART_PutString("\n");

  uint64_t t_start = HAL_Timer_GetTicks();

  status = SCHED_Run();

  uint64_t t_end = HAL_Timer_GetTicks();
  uint64_t elapsed_us = HAL_Timer_TicksToUs(t_end - t_start);

  HAL_UART_PutString("\n[BOOT] Scheduler returned: ");
  HAL_UART_PutString(STATUS_ToString(status));
  HAL_UART_PutString("\n");

  /* ---- Step 9: Print statistics ---- */
  HAL_UART_PutString("\n");
  HAL_UART_PutString("[BOOT] Total scheduler runtime: ");
  HAL_UART_PutDec(elapsed_us);
  HAL_UART_PutString(" us\n\n");

  SCHED_PrintStats();
  HAL_UART_PutString("\n");
  MEM_PrintStats();

  /* ---- Idle ---- */
  HAL_UART_PutString("\n");
  HAL_UART_PutString("[BOOT] All work complete. Entering idle...\n");
  HAL_UART_PutString("       (Ctrl+A then X to exit QEMU)\n");

  while (1) {
    __asm__ volatile("wfe");
  }
}
