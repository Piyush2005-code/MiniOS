/*
 * mmu.c - MMU Initialization for ARM64
 * 
 * Sets up a 3-level page table with:
 * - Identity mapping for kernel RAM (0x40000000 - 64MB) as Normal Cacheable
 * - Device mapping for PL011 UART (0x09000000) as Device-nGnRnE
 * 
 * Translation scheme:
 * - 4KB granule
 * - 48-bit virtual address space
 * - TTBR0_EL1 for user/kernel space
 */

#include <stdint.h>

/* Page table entry bits */
#define PTE_VALID       (1UL << 0)
#define PTE_TABLE       (1UL << 1)
#define PTE_PAGE        (1UL << 1)
#define PTE_BLOCK       (0UL << 1)
#define PTE_AF          (1UL << 10)     /* Access Flag */
#define PTE_SH_INNER    (3UL << 8)      /* Inner Shareable */
#define PTE_NS          (1UL << 5)      /* Non-secure */

/* Memory attributes indices (match MAIR_EL1 configuration) */
#define MAIR_DEVICE_nGnRnE  0x00        /* Device, no Gathering, no Reordering, no Early Write Ack */
#define MAIR_NORMAL_WB      0xEE        /* Normal, Write-Back, Read/Write Allocate */

#define MAIR_IDX_DEVICE     0
#define MAIR_IDX_NORMAL     1

/* Create attribute index bits for PTE */
#define PTE_ATTR(idx)   ((idx) << 2)

/* User/Kernel access permissions */
#define PTE_AP_RW_EL1   (0UL << 6)      /* Read/Write at EL1, no access at EL0 */

/* Memory regions */
#define KERNEL_BASE     0x40000000UL    /* Kernel load address */
#define KERNEL_SIZE     (64 * 1024 * 1024)  /* 64 MB */
#define UART_BASE       0x09000000UL    /* PL011 UART base address */

/* Page table entry macros */
#define L1_ENTRY(addr)  (((addr) & 0x0000FFFFC0000000UL) | PTE_TABLE | PTE_VALID)
#define L2_ENTRY(addr)  (((addr) & 0x0000FFFFFFE00000UL) | PTE_TABLE | PTE_VALID)
#define L3_ENTRY_DEVICE (PTE_VALID | PTE_PAGE | PTE_AF | PTE_ATTR(MAIR_IDX_DEVICE) | PTE_AP_RW_EL1 | PTE_SH_INNER)
#define L2_BLOCK_NORMAL (PTE_VALID | PTE_BLOCK | PTE_AF | PTE_ATTR(MAIR_IDX_NORMAL) | PTE_AP_RW_EL1 | PTE_SH_INNER)

/*
 * Page table structures
 * - L1 table covers 512 GB (512 entries × 1 GB)
 * - L2 table covers 1 GB (512 entries × 2 MB)
 * - L3 table covers 2 MB (512 entries × 4 KB)
 */
static uint64_t l1_table[512] __attribute__((aligned(4096), section(".bss")));
static uint64_t l2_table_kernel[512] __attribute__((aligned(4096), section(".bss")));
static uint64_t l2_table_uart[512] __attribute__((aligned(4096), section(".bss")));
static uint64_t l3_table_uart[512] __attribute__((aligned(4096), section(".bss")));

/*
 * MMU Initialization
 * Called from start.S before jumping to main()
 */
void mmu_init(void)
{
    uint64_t mair, tcr, sctlr;
    
    /*
     * Step 1: Configure Memory Attribute Indirection Register (MAIR_EL1)
     * Defines memory types used by page table entries
     */
    mair = ((uint64_t)MAIR_DEVICE_nGnRnE << (8 * MAIR_IDX_DEVICE)) |
           ((uint64_t)MAIR_NORMAL_WB << (8 * MAIR_IDX_NORMAL));
    
    __asm__ volatile("msr mair_el1, %0" : : "r"(mair));
    
    /*
     * Step 2: Set up page tables
     */
    
    /* L1 table: map kernel region (1 GB at 0x40000000) and UART region (1 GB at 0x00000000) */
    l1_table[0] = L1_ENTRY((uint64_t)l2_table_uart);    /* 0x00000000 - 0x3FFFFFFF (UART in this region) */
    l1_table[1] = L1_ENTRY((uint64_t)l2_table_kernel);  /* 0x40000000 - 0x7FFFFFFF (Kernel RAM) */
    
    /* L2 table for kernel: identity map 64 MB as 2 MB blocks (Normal memory) */
    for (int i = 0; i < 32; i++) {
        /* Each entry maps 2 MB, starting at 0x40000000 */
        l2_table_kernel[i] = (KERNEL_BASE + (i * 0x200000)) | L2_BLOCK_NORMAL;
    }
    
    /* L2 table for UART region: point to L3 table for fine-grained (4KB) mapping */
    /* UART is at 0x09000000, which is index 9 in the L2 table (9 * 2MB = 0x01200000, but we need entry for 0x09000000) */
    /* 0x09000000 / 0x200000 = 72, but within the 0x00000000-0x3FFFFFFF range, it's entry 72 of that L2 table */
    l2_table_uart[72] = L2_ENTRY((uint64_t)l3_table_uart);
    
    /* L3 table for UART: map the UART registers (4 KB page) as Device memory */
    /* UART_BASE 0x09000000, offset within 2MB block = 0x09000000 - (72 * 0x200000) = 0x09000000 - 0x09000000 = 0 */
    /* So UART is at index 0 of this L3 table */
    l3_table_uart[0] = UART_BASE | L3_ENTRY_DEVICE;
    
    /*
     * Step 3: Configure Translation Control Register (TCR_EL1)
     */
    tcr = (16UL << 0)       /* T0SZ = 16, 48-bit address space (2^(64-16) = 2^48) */
        | (0UL << 6)        /* Reserved */
        | (0UL << 7)        /* EPD0 = 0, enable TTBR0_EL1 */
        | (3UL << 8)        /* IRGN0 = 0b11, inner write-back read/write-allocate cacheable */
        | (3UL << 10)       /* ORGN0 = 0b11, outer write-back read/write-allocate cacheable */
        | (3UL << 12)       /* SH0 = 0b11, inner shareable */
        | (0UL << 14)       /* TG0 = 0b00, 4KB granule */
        | (16UL << 16)      /* T1SZ = 16, 48-bit address space for TTBR1_EL1 (unused) */
        | (0UL << 22)       /* A1 = 0, TTBR0_EL1.ASID defines ASID */
        | (1UL << 23)       /* EPD1 = 1, disable TTBR1_EL1 (we don't use it) */
        | (3UL << 24)       /* IRGN1 = 0b11 */
        | (3UL << 26)       /* ORGN1 = 0b11 */
        | (3UL << 28)       /* SH1 = 0b11 */
        | (0UL << 30)       /* TG1 = 0b00, 4KB granule */
        | (0UL << 32)       /* IPS = 0b000, 32-bit physical address space (sufficient for QEMU virt) */
        | (0UL << 37)       /* TBI0 = 0, top byte not ignored */
        | (0UL << 38);      /* TBI1 = 0 */
    
    __asm__ volatile("msr tcr_el1, %0" : : "r"(tcr));
    
    /*
     * Step 4: Set Translation Table Base Register (TTBR0_EL1)
     */
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"((uint64_t)l1_table));
    
    /*
     * Step 5: Invalidate TLB
     */
    __asm__ volatile("tlbi vmalle1" ::: "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    
    /*
     * Step 6: Enable MMU, caches, and instruction cache
     * Set SCTLR_EL1: M (MMU), C (data cache), I (instruction cache)
     */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0);    /* M: Enable MMU */
    sctlr |= (1UL << 2);    /* C: Enable data cache */
    sctlr |= (1UL << 12);   /* I: Enable instruction cache */
    __asm__ volatile("msr sctlr_el1, %0" : : "r"(sctlr));
    
    /* Ensure changes are visible */
    __asm__ volatile("isb" ::: "memory");
}
