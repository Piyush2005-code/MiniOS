/**
 * @file test_mmu.c
 * @brief QEMU tests for UT-MMU-001 through UT-MMU-017
 *
 * Tests verify MMU configuration via:
 *   1. System register reads (TTBR0_EL1, MAIR_EL1, SCTLR_EL1)
 *   2. Direct page-table-entry inspection through the pointer
 *      stored in TTBR0_EL1 (identity-mapped, so VA == PA)
 *   3. Behavioural read/write to normal RAM
 *
 * Page table layout (from mmu.c — identity mapped 1 GB blocks):
 *   Entry 0: PA 0x00000000 — Device-nGnRnE (MMIO)
 *   Entry 1: PA 0x40000000 — Normal WB (RAM, QEMU virt load address)
 *   Entries 2..511: 0 (unused)
 */

#include "types.h"
#include "status.h"
#include "hal/mmu.h"
#include "hal/uart.h"

static int mmu_pass = 0;
static int mmu_fail = 0;

static void ta(const char *id, int cond)
{
    HAL_UART_PutString("[TEST] ");
    HAL_UART_PutString(id);
    HAL_UART_PutString(cond ? " PASS\n" : " FAIL\n");
    if (cond) mmu_pass++; else mmu_fail++;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Read the page-table base address from TTBR0_EL1 (bits [47:1]) */
static uint64_t read_ttbr0(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v));
    return v & ~0xFFFULL;  /* strip lower 12 bits (ASID / reserved) */
}

/** Read one L1 page table entry by index (0..511) */
static uint64_t pte(uint32_t idx)
{
    uint64_t base = read_ttbr0();
    volatile uint64_t *table = (volatile uint64_t *)(uintptr_t)base;
    return table[idx];
}

/* Expected full entry values from mmu.c */
#define ENTRY0_EXPECTED  ((uint64_t)(0x00000000UL \
    | PTE_TYPE_BLOCK | PTE_ATTR_DEVICE \
    | PTE_AF | PTE_SH_OUTER | PTE_AP_RW_EL1 \
    | PTE_UXN | PTE_PXN))

#define ENTRY1_EXPECTED  ((uint64_t)(0x40000000UL \
    | PTE_TYPE_BLOCK | PTE_ATTR_NORMAL \
    | PTE_AF | PTE_SH_INNER | PTE_AP_RW_EL1))

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_UT_MMU_001(void)
{
    /* MMU enabled: SCTLR_EL1 bit 0 (M) must be 1 */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    ta("UT-MMU-001", (sctlr & 1ULL) == 1ULL);
}

static void test_UT_MMU_002(void)
{
    /* Page table base is 4096-byte aligned (TTBR0_EL1 lower 12 bits == 0) */
    uint64_t v;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v));
    ta("UT-MMU-002", (v & 0xFFFULL) == 0ULL);
}

static void test_UT_MMU_003(void)
{
    /* L1 entries 2..7 are zero (spot-check first unused entries) */
    int ok = 1;
    for (uint32_t i = 2; i <= 7; i++) {
        if (pte(i) != 0ULL) { ok = 0; break; }
    }
    ta("UT-MMU-003", ok);
}

static void test_UT_MMU_004(void)
{
    /* Entry 0 physical base address is 0x00000000 (bits [47:30] == 0) */
    uint64_t e = pte(0);
    uint64_t pa = e & 0x0000FFFFC0000000ULL;  /* 1 GB block PA mask */
    ta("UT-MMU-004", pa == 0x00000000ULL);
}

static void test_UT_MMU_005(void)
{
    /* Entry 0 has PTE_TYPE_BLOCK set (bits [1:0] == 0b01) */
    ta("UT-MMU-005", (pte(0) & 0x3ULL) == (uint64_t)PTE_TYPE_BLOCK);
}

static void test_UT_MMU_006(void)
{
    /* Entry 0 has PTE_ATTR_DEVICE (MAIR index 0, bits [4:2] == 0b000) */
    ta("UT-MMU-006", (pte(0) & (0x7ULL << 2)) == (uint64_t)PTE_ATTR_DEVICE);
}

static void test_UT_MMU_007(void)
{
    /* Entry 0 has PTE_AF (access flag, bit 10) set */
    ta("UT-MMU-007", (pte(0) & PTE_AF) == PTE_AF);
}

static void test_UT_MMU_008(void)
{
    /* Entry 0 has both PTE_UXN (bit 54) and PTE_PXN (bit 53) set */
    ta("UT-MMU-008",
       (pte(0) & PTE_UXN) == PTE_UXN &&
       (pte(0) & PTE_PXN) == PTE_PXN);
}

static void test_UT_MMU_009(void)
{
    /* Entry 1 physical base address is 0x40000000 (bits [47:30]) */
    uint64_t e = pte(1);
    uint64_t pa = e & 0x0000FFFFC0000000ULL;
    ta("UT-MMU-009", pa == 0x40000000ULL);
}


void run_mmu_tests(int *pass, int *fail)
{
    test_UT_MMU_001(); test_UT_MMU_002(); test_UT_MMU_003();
    test_UT_MMU_004(); test_UT_MMU_005(); test_UT_MMU_006();
    test_UT_MMU_007(); test_UT_MMU_008(); test_UT_MMU_009();
    test_UT_MMU_010(); test_UT_MMU_011(); test_UT_MMU_012();
    test_UT_MMU_013(); test_UT_MMU_014(); test_UT_MMU_015();
    test_UT_MMU_016(); test_UT_MMU_017();
    *pass += mmu_pass;
    *fail += mmu_fail;
}
