/**
 * @file storage.c
 * @brief Simple Non-Volatile Storage Manager Implementation
 */

#include "kernel/storage.h"
#include "hal/flash.h"
#include "hal/uart.h"

#define STORAGE_MAGIC 0x534F4E4D /* "MNOS" byte-reversed */

Status STORAGE_Init(void)
{
    uint32_t magic = 0;
    Status s;

    HAL_UART_PutString("[STOR ] Checking NVRAM...\n");

    /* Ensure flash is initialized */
    HAL_Flash_Init();

    s = HAL_Flash_Read(0, &magic);
    if (s != STATUS_OK) {
        HAL_UART_PutString("[STOR ] Error reading flash\n");
        return s;
    }

    if (magic != STORAGE_MAGIC) {
        HAL_UART_PutString("[STOR ] Formatting NVRAM (Erase sector 0)...\n");
        
        /* Flash sectors are erased to 0xFFFFFFFF, we need to erase it first */
        s = HAL_Flash_EraseSector(0);
        if (s != STATUS_OK) {
            HAL_UART_PutString("[STOR ] Flash Erase Failed!\n");
            return s;
        }

        /* Write magic word at offset 0 */
        s = HAL_Flash_Write(0, STORAGE_MAGIC);
        if (s != STATUS_OK) {
            HAL_UART_PutString("[STOR ] Flash Write Failed!\n");
            return s;
        }
        
        HAL_UART_PutString("[STOR ] NVRAM formatted\n");
    } else {
        HAL_UART_PutString("[STOR ] NVRAM verified\n");
    }

    return STATUS_OK;
}

Status STORAGE_Write(uint32_t offset, const uint8_t *buffer, size_t length)
{
    if (offset >= STORAGE_CAPACITY || (offset + length) > STORAGE_CAPACITY) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    if ((offset % 4) != 0 || (length % 4) != 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    return HAL_Flash_WriteBuffer(offset, buffer, length);
}

Status STORAGE_Read(uint32_t offset, uint8_t *buffer, size_t length)
{
    if (offset >= STORAGE_CAPACITY || (offset + length) > STORAGE_CAPACITY) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    if ((offset % 4) != 0 || (length % 4) != 0) {
        /* Alignments are strictly enforced for simplicity */
        return STATUS_ERROR_INVALID_ARGUMENT;
    }

    return HAL_Flash_ReadBuffer(offset, buffer, length);
}

Status STORAGE_EraseSector(uint32_t offset)
{
    if (offset >= STORAGE_CAPACITY) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Round down to nearest sector */
    uint32_t sector_offset = offset & ~(FLASH_SECTOR_SIZE - 1);
    
    return HAL_Flash_EraseSector(sector_offset);
}
