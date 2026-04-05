/**
 * @file mmu.c
 * @brief MMU and cache initialization for MiniOS
 *
 * Sets up identity-mapped page tables for the QEMU virt machine:
 *   - 0x00000000–0x3FFFFFFF : Device memory (UART, GIC, etc.)
 *   - 0x40000000–0x7FFFFFFF : Normal cacheable RAM (code, data, stack)
 *
 * Uses 4KB granule with Level 1 block descriptors (1GB blocks)
 * for simplicity. This provides 2 entries covering 2GB which is
 * sufficient for QEMU virt with 512MB–1GB RAM.
 *
 * @note Per SRS FR-002, FR-003
 *
 * @complexity Time: O(1), Space: O(4KB page table)
 */

#include "hal/mmu.h"
#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Page table storage — must be 4KB aligned                          */
/* ------------------------------------------------------------------ */

/*
 * Level 1 page table: 512 entries × 8 bytes = 4KB
 * Each L1 entry with 4KB granule can point to a L2 table or
 * be a 1GB block descriptor.
 *
 * We use 1GB block descriptors for simplicity:
 *   Entry 0: 0x00000000–0x3FFFFFFF → Device
 *   Entry 1: 0x40000000–0x7FFFFFFF → Normal RAM
 */
static uint64_t l1_page_table[512] __attribute__((aligned(4096)));

/* ------------------------------------------------------------------ */
/*  Inline assembly helpers for system registers                      */
/* ------------------------------------------------------------------ */

static inline void write_mair_el1(uint64_t val)
{
    __asm__ volatile("msr mair_el1, %0" :: "r"(val));
}

static inline void write_tcr_el1(uint64_t val)
{
    __asm__ volatile("msr tcr_el1, %0" :: "r"(val));
}

static inline void write_ttbr0_el1(uint64_t val)
{
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(val));
}

static inline uint64_t read_sctlr_el1(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(val));
    return val;
}

static inline void write_sctlr_el1(uint64_t val)
{
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(val));
}

/* ------------------------------------------------------------------ */
/*  Internal: build the page tables                                   */
/* ------------------------------------------------------------------ */

static void mmu_build_page_tables(void)
{
    uint64_t i;

    /* Zero entire L1 table */
    for (i = 0; i < 512; i++) {
        l1_page_table[i] = 0;
    }

    /*
     * Entry 0: 0x00000000 – 0x3FFFFFFF (1GB)
     * Device-nGnRnE memory for MMIO
     * - Block descriptor (bit 0 = 1, bit 1 = 0)
     * - MAIR index 0 (Device-nGnRnE)
     * - Access flag set
     * - Inner shareable
     * - EL1 read/write
     * - Execute Never (PXN + UXN)
     */
    l1_page_table[0] = (0x00000000UL)           /* Physical base address */
                      | PTE_TYPE_BLOCK           /* Block descriptor */
                      | PTE_ATTR_DEVICE          /* MAIR index 0 */
                      | PTE_AF                   /* Access flag */
                      | PTE_SH_OUTER             /* Outer shareable for device */
                      | PTE_AP_RW_EL1            /* EL1 read/write */
                      | PTE_UXN                  /* No execute (unprivileged) */
                      | PTE_PXN;                 /* No execute (privileged) */

    /*
     * Entry 1: 0x40000000 – 0x7FFFFFFF (1GB)
     * Normal Write-Back cacheable memory for RAM
     * - Block descriptor
     * - MAIR index 1 (Normal WB)
     * - Access flag set
     * - Inner shareable
     * - EL1 read/write
     */
    l1_page_table[1] = (0x40000000UL)           /* Physical base address */
                      | PTE_TYPE_BLOCK           /* Block descriptor */
                      | PTE_ATTR_NORMAL          /* MAIR index 1 */
                      | PTE_AF                   /* Access flag */
                      | PTE_SH_INNER             /* Inner shareable for RAM */
                      | PTE_AP_RW_EL1;           /* EL1 read/write */
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status HAL_MMU_Init(void)
{
    uint64_t sctlr;

    HAL_UART_PutString("[MMU ] Building page tables...\n");

    /* Step 1: Build the page tables in memory */
    mmu_build_page_tables();

    /*
     * Step 2: Configure MAIR_EL1
     * Attr0 = 0x00 (Device-nGnRnE)
     * Attr1 = 0xFF (Normal, Write-Back, Read/Write Allocate)
     */
    write_mair_el1((MAIR_NORMAL_WB << 8) | MAIR_DEVICE_nGnRnE);

    /*
     * Step 3: Configure TCR_EL1
     *
     * T0SZ  = 32  → 2^(64-32) = 4GB address space (bits [5:0])
     * IRGN0 = 01  → Inner Write-Back, Write-Allocate (bits [9:8])
     * ORGN0 = 01  → Outer Write-Back, Write-Allocate (bits [11:10])
     * SH0   = 11  → Inner Shareable (bits [13:12])
     * TG0   = 00  → 4KB granule (bits [15:14])
     * IPS   = 000 → 32-bit PA (4GB) (bits [34:32])
     */
    uint64_t tcr = (32UL << 0)      /* T0SZ = 32 */
                 | (1UL  << 8)      /* IRGN0 = Write-Back */
                 | (1UL  << 10)     /* ORGN0 = Write-Back */
                 | (3UL  << 12)     /* SH0 = Inner Shareable */
                 | (0UL  << 14);    /* TG0 = 4KB granule */

    write_tcr_el1(tcr);

    /*
     * Step 4: Set TTBR0_EL1 to point to our L1 page table
     */
    write_ttbr0_el1((uint64_t)(uintptr_t)l1_page_table);

    /* Ensure all table writes are visible before enabling MMU */
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    HAL_UART_PutString("[MMU ] Page table at: ");
    HAL_UART_PutHex((uint64_t)(uintptr_t)l1_page_table);
    HAL_UART_PutString("\n");

    /*
     * Step 5: Invalidate all TLB entries before enabling
     */
    HAL_MMU_InvalidateTLB();

    /*
     * Step 6: Enable MMU and caches via SCTLR_EL1
     *
     * Bit 0  (M)   = 1 → MMU enable
     * Bit 2  (C)   = 1 → Data cache enable
     * Bit 12 (I)   = 1 → Instruction cache enable
     * Bit 26 (UCI)  = 1 → EL0 cache instructions enabled
     */
    sctlr = read_sctlr_el1();
    sctlr |= (1UL << 0);   /* M   — MMU enable */
    sctlr |= (1UL << 2);   /* C   — Data cache enable */
    sctlr |= (1UL << 12);  /* I   — Instruction cache enable */
    write_sctlr_el1(sctlr);

    /* Synchronize */
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    HAL_UART_PutString("[MMU ] MMU and caches enabled\n");

    return STATUS_OK;
}

void HAL_MMU_InvalidateTLB(void)
{
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

void HAL_MMU_CleanInvalidateDCache(void)
{
    /*
     * Simple approach: use data barrier instructions.
     * A full set-way clean+invalidate can be added later
     * for real hardware; QEMU doesn't strictly need it.
     */
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}
