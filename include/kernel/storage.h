/**
 * @file storage.h
 * @brief Simple Non-Volatile Storage Manager (NVRAM API)
 *
 * Uses the underlying HAL flash interface to provide persistent read/write
 * configuration storage (treating the 4MB flash region as linear storage).
 */

#ifndef KERNEL_STORAGE_H
#define KERNEL_STORAGE_H

#include "types.h"
#include "status.h"

/** Total capacity available through this API (first 4MB of Flash) */
#define STORAGE_CAPACITY 0x400000

/**
 * @brief Initialize the storage subsystem.
 *
 * Scans the first 4 MB of flash. Erases and formats the initial sector
 * if it doesn't contain a valid MINIOS configuration magic.
 *
 * @return STATUS_OK on success
 */
Status STORAGE_Init(void);

/**
 * @brief Program raw data into storage.
 *
 * NOTE: If the region is not erased (value != 0xFF), the flash driver
 * cannot program 0 bits back to 1. Using this requires an erased sector.
 *
 * @param offset    Byte offset within the 4MB storage region
 * @param buffer    Data pointer (must be 4-byte aligned)
 * @param length    Number of bytes to write (must be multiple of 4)
 * @return STATUS_OK or error
 */
Status STORAGE_Write(uint32_t offset, const uint8_t *buffer, size_t length);

/**
 * @brief Read raw data from storage.
 *
 * @param offset    Byte offset within the storage region
 * @param buffer    Destination buffer
 * @param length    Number of bytes to read
 * @return STATUS_OK or error
 */
Status STORAGE_Read(uint32_t offset, uint8_t *buffer, size_t length);

/**
 * @brief Safely erase a 256KB sector in storage.
 *
 * Call this before overwriting data that needs to reset bits 0->1.
 *
 * @param offset Byte offset (will be rounded down to sector boundary)
 * @return STATUS_OK or error
 */
Status STORAGE_EraseSector(uint32_t offset);

#endif // KERNEL_STORAGE_H
