// mmu.c
// Minimal MMU initialization for AArch64 – fixed MSR operand width warning.

#include <stdint.h>

// Translation table entries (4KB granule, 3-level)
#define TT_TYPE_BLOCK     0x401ULL
#define TT_TYPE_TABLE     0x3ULL
#define TT_AF             (1 << 10)
#define TT_NX             (0x3ULL << 53)  // Execute Never for data

// MAIR: Memory Attribute Indirection Register
// Index 0: Device-nGnRnE (strongly ordered)
// Index 1: Normal, Outer/Inner Write-Back, Non-Transient, Read/Write-Alloc
#define MAIR_ATTR0        0x00ULL
#define MAIR_ATTR1        0xEEULL
#define MAIR              ((MAIR_ATTR1 << 8) | MAIR_ATTR0)

// TCR: Translation Control Register
// T0SZ=25 (48-bit VA), TG0=4KB, IRGN0=Write-Back, ORGN0=Write-Back, SH0=Inner Shareable
#define TCR_T0SZ          (64 - 48)
#define TCR_IRGN0_WB      (1 << 8)
#define TCR_ORGN0_WB      (1 << 10)
#define TCR_SH0_IS        (3 << 12)
#define TCR_TG0_4K        (0 << 14)
#define TCR              (TCR_T0SZ | TCR_IRGN0_WB | TCR_ORGN0_WB | TCR_SH0_IS | TCR_TG0_4K)

// Page tables (allocated in .bss, 4KB aligned)
#define PAGE_SIZE         4096
#define NUM_PTE           (PAGE_SIZE / sizeof(uint64_t))

// Level 1 and Level 2 tables
static uint64_t ttb_l1[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t ttb_l2[512] __attribute__((aligned(PAGE_SIZE)));

void mmu_init(void) {
    // Get current execution address (physical, since MMU is off)
    uint64_t phys_base;
    __asm__ volatile ("adr %0, ." : "=r"(phys_base));
    // Align to 1GB boundary for L1 index
    uint64_t l1_index = (phys_base >> 30) & 0x1FF;

    // Setup L1 entry: Points to L2 table
    ttb_l1[l1_index] = ((uint64_t)ttb_l2) | TT_TYPE_TABLE;

    // Setup L2 entries for 2MB blocks
    uint64_t l2_base = phys_base & ~((1ULL << 21) - 1);
    for (int i = 0; i < 32; i++) { // Cover 64MB (32 * 2MB)
        uint64_t block_addr = l2_base + (i << 21);
        uint64_t attrs = TT_TYPE_BLOCK | TT_AF;
        // Mark data section as NX (Execute Never) - safety
        // For simplicity, we mark blocks above 64MB as NX.
        // A real implementation would parse the ELF sections.
        if (i > 0) attrs |= TT_NX;
        ttb_l2[i] = block_addr | attrs;
    }

    // Set TTBR0_EL1 (translation table base)
    __asm__ volatile ("msr ttbr0_el1, %0" : : "r" ((uint64_t)ttb_l1));

    // Configure MAIR
    __asm__ volatile ("msr mair_el1, %0" : : "r" (MAIR));

    // Configure TCR – use a temporary 64-bit variable to avoid warning
    uint64_t tcr_val = TCR;
    __asm__ volatile ("msr tcr_el1, %0" : : "r" (tcr_val));

    // Invalidate TLB
    __asm__ volatile ("tlbi vmalle1is; dsb sy; isb");

    // Enable MMU (SCTLR_EL1.M = 1, C=1, I=1)
    uint64_t sctlr;
    __asm__ volatile ("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1 << 0) | (1 << 2) | (1 << 12); // M, C, I
    sctlr &= ~(1 << 1); // Clear A (alignment check)
    __asm__ volatile ("msr sctlr_el1, %0" : : "r"(sctlr));
    __asm__ volatile ("isb");
}