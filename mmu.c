// mmu.c
// Minimal MMU initialization for AArch64
// - Identity maps 64MB of RAM at the kernel load address (Normal Cacheable)
// - Maps the PL011 UART at 0x09000000 (Device-nGnRnE)
// - Uses MAIR index 0 for Device, index 1 for Normal

#include <stdint.h>

// Translation table entries (4KB granule, 3-level)
#define TT_TYPE_BLOCK     0x401ULL      // Block descriptor, bits[1:0]=01
#define TT_TYPE_TABLE     0x3ULL        // Table descriptor, bits[1:0]=11
#define TT_AF             (1 << 10)     // Access Flag
#define TT_NX             (0x3ULL << 53) // Execute Never
#define TT_ATTR_DEVICE    (0 << 2)      // AttrIdx = 0 (MAIR index 0)
#define TT_ATTR_NORMAL    (1 << 2)      // AttrIdx = 1 (MAIR index 1)

// MAIR: Memory Attribute Indirection Register
// Index 0: Device-nGnRnE (strongly ordered, non‑cacheable)
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

// Level 1 and Level 2 tables for kernel RAM
static uint64_t ttb_l1[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t ttb_l2[512] __attribute__((aligned(PAGE_SIZE)));
// Level 2 table for UART region (1GB block at L1 index 2)
static uint64_t ttb_l2_uart[512] __attribute__((aligned(PAGE_SIZE)));

void mmu_init(void) {
    // --- Get current execution address (physical, since MMU is off) ---
    uint64_t phys_base;
    __asm__ volatile ("adr %0, ." : "=r"(phys_base));
    uint64_t l1_index = (phys_base >> 30) & 0x1FF;   // 1GB index
    uint64_t l2_base = phys_base & ~((1ULL << 21) - 1); // 2MB aligned base

    // --- Setup L1 entry for kernel RAM: points to L2 table ---
    ttb_l1[l1_index] = ((uint64_t)ttb_l2) | TT_TYPE_TABLE;

    // --- Setup L2 entries for kernel RAM (2MB blocks, Normal Cacheable) ---
    for (int i = 0; i < 32; i++) { // Cover 64MB (32 * 2MB)
        uint64_t block_addr = l2_base + (i << 21);
        uint64_t attrs = TT_TYPE_BLOCK | TT_AF | TT_ATTR_NORMAL;
        if (i > 0) attrs |= TT_NX;               // Execute Never for data
        ttb_l2[i] = block_addr | attrs;
    }

    // --- Map UART (0x09000000) as Device memory ---
    uint64_t uart_addr = 0x09000000;
    uint64_t uart_l1_index = (uart_addr >> 30) & 0x1FF; // = 2 (0x080000000–0x0BFFFFFF)
    uint64_t uart_l2_index = (uart_addr >> 21) & 0x1FF; // = 72 (0x09000000 / 2MB)

    // Point L1 entry to the UART L2 table
    ttb_l1[uart_l1_index] = ((uint64_t)ttb_l2_uart) | TT_TYPE_TABLE;

    // L2 block descriptor for the 2MB region containing the UART
    uint64_t uart_block_base = uart_addr & ~((1ULL << 21) - 1);
    uint64_t uart_attrs = TT_TYPE_BLOCK | TT_AF | TT_ATTR_DEVICE | TT_NX;
    ttb_l2_uart[uart_l2_index] = uart_block_base | uart_attrs;

    // --- Set TTBR0_EL1 (translation table base) ---
    __asm__ volatile ("msr ttbr0_el1, %0" : : "r" ((uint64_t)ttb_l1));

    // --- Configure MAIR ---
    __asm__ volatile ("msr mair_el1, %0" : : "r" (MAIR));

    // --- Configure TCR ---
    uint64_t tcr_val = TCR;
    __asm__ volatile ("msr tcr_el1, %0" : : "r" (tcr_val));

    // --- Invalidate TLB ---
    __asm__ volatile ("tlbi vmalle1is; dsb sy; isb");

    // --- Enable MMU (SCTLR_EL1.M = 1, C=1, I=1) ---
    uint64_t sctlr;
    __asm__ volatile ("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1 << 0) | (1 << 2) | (1 << 12); // M, C, I
    sctlr &= ~(1 << 1);                       // Clear A (alignment check)
    __asm__ volatile ("msr sctlr_el1, %0" : : "r"(sctlr));
    __asm__ volatile ("isb");
}