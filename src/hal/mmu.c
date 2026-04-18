/**
 * @file mmu.c
 * @brief MMU and cache initialization for MiniOS
 *
 * Sets up identity-mapped page tables. Two platform configurations:
 *
 * QEMU virt (2 L1 entries, 2 GB):
 *   0x00000000–0x3FFFFFFF — Device memory (UART at 0x09000000, GIC etc.)
 *   0x40000000–0x7FFFFFFF — Normal cacheable RAM (kernel at 0x40000000)
 *
 * Raspberry Pi 4B (4 L1 entries, 4 GB):
 *   0x00000000–0x3FFFFFFF — Device/VC memory (VideoCore, lower MMIO)
 *   0x40000000–0x7FFFFFFF — Normal RAM (kernel at 0x80000)
 *   0x80000000–0xBFFFFFFF — Normal RAM (upper 1 GB window)
 *   0xC0000000–0xFFFFFFFF — Device memory (BCM2711 peripherals 0xFE000000+)
 *
 * Uses 4KB granule with Level 1 block descriptors (1 GB blocks)
 * for simplicity. IPS is set to 36-bit PA for Pi 4B.
 *
 * @note Per SRS FR-002, FR-003
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
     * Entry 0: 0x00000000 – 0x3FFFFFFF (1 GB)
     * Device-nGnRnE memory.
     * QEMU: covers UART (0x09000000) and GIC (0x08000000).
     * Pi 4B: covers VideoCore / VC4 memory and lower MMIO.
     */
    l1_page_table[0] = (0x00000000UL)
                      | PTE_TYPE_BLOCK
                      | PTE_ATTR_DEVICE
                      | PTE_AF
                      | PTE_SH_OUTER
                      | PTE_AP_RW_EL1
                      | PTE_UXN
                      | PTE_PXN;

    /*
     * Entry 1: 0x40000000 – 0x7FFFFFFF (1 GB)
     * Normal Write-Back cacheable RAM.
     * QEMU: kernel at 0x40000000.
     * Pi 4B: kernel at 0x80000 (within this range).
     */
    l1_page_table[1] = (0x40000000UL)
                      | PTE_TYPE_BLOCK
                      | PTE_ATTR_NORMAL
                      | PTE_AF
                      | PTE_SH_INNER
                      | PTE_AP_RW_EL1;

#ifdef PLATFORM_RPI4
    /*
     * Entry 2: 0x80000000 – 0xBFFFFFFF (1 GB)
     * Pi 4B: Normal Write-Back cacheable RAM (upper RAM window).
     * Required on Pi 4B models with > 1 GB RAM to access the
     * upper portion of LPDDR4 through the identity map.
     */
    l1_page_table[2] = (0x80000000UL)
                      | PTE_TYPE_BLOCK
                      | PTE_ATTR_NORMAL
                      | PTE_AF
                      | PTE_SH_INNER
                      | PTE_AP_RW_EL1;

    /*
     * Entry 3: 0xC0000000 – 0xFFFFFFFF (1 GB)
     * Pi 4B: Device-nGnRnE for BCM2711 peripheral window.
     * Key addresses in this range:
     *   0xFC000000 – 0xFEFFFFFF — BCM2711 peripherals
     *   0xFE000000              — Peripheral base
     *   0xFE201000              — UART0 (PL011)
     *   0xFF800000              — ARM Local Interrupt Controller
     *   0xFF841000              — GIC400 Distributor
     *   0xFF842000              — GIC400 CPU Interface
     */
    l1_page_table[3] = (0xC0000000UL)
                      | PTE_TYPE_BLOCK
                      | PTE_ATTR_DEVICE
                      | PTE_AF
                      | PTE_SH_OUTER
                      | PTE_AP_RW_EL1
                      | PTE_UXN
                      | PTE_PXN;
#endif /* PLATFORM_RPI4 */
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
     *
     * IPS (bits [34:32]):
     *   QEMU virt: IPS=000 (32-bit PA, 4 GB) — adequate for virt machine
     *   Pi 4B:     IPS=001 (36-bit PA, 64 GB) — covers 0x00000000–0xFFFFFFFF
     *              (our 4-entry L1 table maps the full 4 GB space)
     */
    uint64_t tcr = (32UL << 0)   /* T0SZ = 32 */
                 | (1UL  << 8)   /* IRGN0 = Write-Back */
                 | (1UL  << 10)  /* ORGN0 = Write-Back */
                 | (3UL  << 12)  /* SH0 = Inner Shareable */
                 | (0UL  << 14); /* TG0 = 4KB granule */

#ifdef PLATFORM_RPI4
    tcr |= (1UL << 32);          /* IPS = 001 = 36-bit PA (64 GB) */
#else
    tcr |= (0UL << 32);          /* IPS = 000 = 32-bit PA (4 GB) */
#endif

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
