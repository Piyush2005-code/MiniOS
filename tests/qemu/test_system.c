/**
 * @file test_system.c
 * @brief System tests: ST-BOOT, ST-INIT, ST-API, ST-BENCH, ST-MEM, ST-STAB
 *
 * Black-box system tests that observe behaviour through the public API
 * and UART output. No internal static variables are inspected (Rule 10).
 */

#include "types.h"
#include "status.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "hal/mmu.h"
#include "kernel/kmem.h"
#include "kernel/thread.h"

static int sys_pass = 0;
static int sys_fail = 0;

static void ta(const char *id, int cond)
{
    HAL_UART_PutString("[TEST] ");
    HAL_UART_PutString(id);
    HAL_UART_PutString(cond ? " PASS\n" : " FAIL\n");
    if (cond) sys_pass++; else sys_fail++;
}

/* ------------------------------------------------------------------ */
/*  ST-BOOT                                                            */
/* ------------------------------------------------------------------ */

/* ST-BOOT-001: Kernel boots from 0x40000000 and reaches test runner */
static void test_ST_BOOT_001(void)
{
    /* If we are here, the kernel booted and reached test_runner_main */
    ta("ST-BOOT-001", 1);
}

static void test_ST_BOOT_002(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    uint32_t level = (uint32_t)((el >> 2) & 0x3);
    ta("ST-BOOT-002", level == 1);
}

static void test_ST_BOOT_003(void)
{
    ta("ST-BOOT-003", bss_test_var == 0);
}

static void test_ST_BOOT_004(void)
{
    int result = deep_stack_func(4);
    ta("ST-BOOT-004", result >= 0);  /* must not fault */
}

static void test_ST_BOOT_005(void)
{
    HAL_UART_PutString("[ST-BOOT-005] UART operational\n");
    ta("ST-BOOT-005", 1);
}

static void test_ST_BOOT_006(void)
{
    extern void _vector_table(void);
    uint64_t vbar;
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
    ta("ST-BOOT-006", vbar == (uint64_t)(uintptr_t)&_vector_table);
}

static void test_ST_INIT_001(void)
{
    /* UART, MMU, Timer, Memory all initialised (we are here = all passed) */
    ta("ST-INIT-001", 1);
}

static void test_ST_INIT_002(void)
{
    /* Timer frequency within 50-100 MHz */
    uint64_t freq = HAL_Timer_GetFreq();
    ta("ST-INIT-002", freq >= 50000000ULL && freq <= 100000000ULL);
}

static void test_ST_INIT_003(void)
{
    /* Heap size reported matches linker symbols */
    extern uint8_t _heap_start[];
    extern uint8_t _heap_end[];
    size_t linker_size = (size_t)(_heap_end - _heap_start);
    kmem_stats_t st;
    KMEM_GetStats(&st);
    /* KMEM aligns start to page, so total <= linker range */
    ta("ST-INIT-003", st.heap_total > 0 && st.heap_total <= linker_size);
}

static void test_ST_INIT_004(void)
{
    /* Normal RAM read/write after MMU init */
    volatile uint32_t *ptr = (volatile uint32_t *)0x40110000UL;
    *ptr = 0x12345678UL;
    ta("ST-INIT-004", *ptr == 0x12345678UL);
}

static void test_ST_INIT_005(void)
{
    /* Timer still readable after MMU init (device memory mapped) */
    uint64_t t = HAL_Timer_GetTicks();
    ta("ST-INIT-005", t > 0);
}


void run_system_tests(int *pass, int *fail)
{
    /* ST-BOOT */
    test_ST_BOOT_001(); test_ST_BOOT_002(); test_ST_BOOT_003();
    test_ST_BOOT_004(); test_ST_BOOT_005(); test_ST_BOOT_006();
    /* ST-INIT */
    test_ST_INIT_001(); test_ST_INIT_002(); test_ST_INIT_003();
    test_ST_INIT_004(); test_ST_INIT_005();
    /* ST-API */
    test_ST_API_001(); test_ST_API_002(); test_ST_API_003();
    test_ST_API_004(); test_ST_API_005();
    /* ST-BENCH */
    test_ST_BENCH_001(); test_ST_BENCH_002(); test_ST_BENCH_003();
    test_ST_BENCH_004(); test_ST_BENCH_005(); test_ST_BENCH_006();
    test_ST_BENCH_007(); test_ST_BENCH_008(); test_ST_BENCH_009();
    test_ST_BENCH_010();
    /* ST-MEM-STRESS */
    test_ST_MEM_STRESS_001(); test_ST_MEM_STRESS_002(); test_ST_MEM_STRESS_003();
    /* ST-STAB */
    test_ST_STAB_001(); test_ST_STAB_002(); test_ST_STAB_003();

    *pass += sys_pass;
    *fail += sys_fail;
}
