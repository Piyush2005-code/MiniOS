/**
 * @file flash.h
 * @brief Flash Driver for MiniOS
 *
 * Provides rudimentary access to QEMU's pflash1 device (Intel CFI01).
 * Used for persistent storage of configuration or file system data.
 *
 * Base address: 0x04000000
 * Sector size: 256 KB
 * Total size: 64 MB
 */

#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include "types.h"
#include "status.h"

#define FLASH_BASE_ADDR   0x04000000UL
#define FLASH_SECTOR_SIZE 0x00040000UL   // 256KB — Intel CFI standard
#define FLASH_TOTAL_SIZE  0x04000000UL   // 64MB = size of flash.img

/* Storage layout — byte offsets within flash */
#define STORAGE_HEADER_OFFSET 0x00000000UL  /* Sector 0: 256KB for NVRAM boot header (magic) */
#define STORAGE_FS_OFFSET     0x00040000UL  /* Sector 1+: ulfs filesystem starts here         */
#define STORAGE_FS_SIZE       (FLASH_TOTAL_SIZE - STORAGE_FS_OFFSET)

/**
 * @brief Initialize the flash subsystem.
 * @return STATUS_OK on success
 */
Status HAL_Flash_Init(void);

/**
 * @brief Read a 32-bit word from flash.
 * @param offset Byte offset from FLASH_BASE_ADDR (must be 4-byte aligned)
 * @param data Pointer to receive the read value
 * @return STATUS_OK or error
 */
Status HAL_Flash_Read(uint32_t offset, uint32_t *data);

/**
 * @brief Erase a 256KB sector.
 *
 * Required before programming 0 -> 1.
 * @param sector_offset Byte offset (must be sector aligned)
 * @return STATUS_OK or error
 */
Status HAL_Flash_EraseSector(uint32_t sector_offset);

/**
 * @brief Program a 32-bit word to flash.
 * @param offset Byte offset from FLASH_BASE_ADDR (must be 4-byte aligned)
 * @param data Value to write
 * @return STATUS_OK or error
 */
Status HAL_Flash_Write(uint32_t offset, uint32_t data);

/**
 * @brief Program a buffer of bytes to flash.
 * @param offset Byte offset from FLASH_BASE_ADDR
 * @param buf Pointer to data
 * @param length Size in bytes (must be multiple of 4)
 * @return STATUS_OK or error
 */
Status HAL_Flash_WriteBuffer(uint32_t offset, const uint8_t *buf, size_t length);

/**
 * @brief Read a buffer of bytes from flash using sequential access.
 * @param offset Byte offset from FLASH_BASE_ADDR
 * @param buf Pointer to destination buffer
 * @param length Size in bytes (must be multiple of 4)
 * @return STATUS_OK or error
 */
Status HAL_Flash_ReadBuffer(uint32_t offset, uint8_t *buf, size_t length);

#endif // HAL_FLASH_H
