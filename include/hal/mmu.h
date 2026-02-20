/**
 * @file mmu.h
 * @brief MMU and cache management interface for MiniOS
 *
 * Provides identity-mapped virtual memory with 4KB granule
 * for ARM64 (AArch64). Configures memory attributes for
 * device MMIO and normal cacheable RAM.
 *
 * @note Per SRS FR-002, FR-003
 */

#ifndef MINIOS_HAL_MMU_H
#define MINIOS_HAL_MMU_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Page table constants                                              */
/* ------------------------------------------------------------------ */

/* 4KB granule */
#define PAGE_SIZE           4096UL
#define PAGE_SHIFT          12

/* Block sizes */
#define BLOCK_SIZE_2MB      (2UL * 1024 * 1024)
#define BLOCK_SIZE_1GB      (1UL * 1024 * 1024 * 1024)

/* Page table entry types */
#define PTE_TYPE_BLOCK      0x1     /* Block descriptor (L1/L2) */
#define PTE_TYPE_TABLE      0x3     /* Table descriptor (points to next level) */
#define PTE_TYPE_PAGE       0x3     /* Page descriptor (L3) */

/* Access flag */
#define PTE_AF              (1UL << 10)

/* Shareability */
#define PTE_SH_INNER        (3UL << 8)   /* Inner shareable */
#define PTE_SH_OUTER        (2UL << 8)   /* Outer shareable */

/* Access permissions */
#define PTE_AP_RW_EL1       (0UL << 6)   /* EL1 read/write */
#define PTE_AP_RO_EL1       (2UL << 6)   /* EL1 read-only */

/* Memory attribute index (into MAIR_EL1) */
#define PTE_ATTR_DEVICE     (0UL << 2)   /* MAIR index 0: Device-nGnRnE */
#define PTE_ATTR_NORMAL     (1UL << 2)   /* MAIR index 1: Normal WB */

/* Execute Never */
#define PTE_UXN             (1UL << 54)  /* Unprivileged Execute Never */
#define PTE_PXN             (1UL << 53)  /* Privileged Execute Never */

/* ------------------------------------------------------------------ */
/*  MAIR attribute definitions                                        */
/* ------------------------------------------------------------------ */
#define MAIR_DEVICE_nGnRnE  0x00UL
#define MAIR_NORMAL_WB      0xFFUL  /* Write-Back, Read/Write Allocate */

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the MMU with identity-mapped page tables
 *
 * Sets up MAIR_EL1, TCR_EL1, builds page tables,
 * writes TTBR0_EL1, and enables the MMU with data
 * and instruction caches.
 *
 * @return STATUS_OK on success
 *
 * @warning Must be called before any cache-dependent operations.
 *          Must only be called once during boot.
 */
Status HAL_MMU_Init(void);

/**
 * @brief Invalidate all TLB entries at EL1
 */
void HAL_MMU_InvalidateTLB(void);

/**
 * @brief Clean and invalidate all data caches
 */
void HAL_MMU_CleanInvalidateDCache(void);

#endif /* MINIOS_HAL_MMU_H */
