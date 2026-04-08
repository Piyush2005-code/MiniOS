/**
 * @file flash.c
 * @brief Flash Driver Implementation for QEMU virt (Intel CFI01)
 */

#include "hal/flash.h"
#include "hal/uart.h"

static volatile uint32_t *flash_mem = (volatile uint32_t *)FLASH_BASE_ADDR;

/* ------------------------------------------------------------------ */
/*  Internal Helpers                                                  */
/* ------------------------------------------------------------------ */

static void flash_write_cmd(uint32_t offset, uint32_t cmd)
{
    flash_mem[offset / 4] = cmd;
}

static Status flash_wait_ready(uint32_t offset)
{
    uint32_t status;
    int timeout = 1000000;
    
    do {
        flash_write_cmd(offset, 0x70); /* Read Status Register */
        status = flash_mem[offset / 4];
        if (status & 0x80) break;      /* SR.7 (Ready) bit */
        timeout--;
    } while (timeout > 0);
    
    if (timeout <= 0) {
        return STATUS_ERROR_TIMEOUT;
    }
    
    /* Errors: Erase (SR.5), Program (SR.4), VPP (SR.3) */
    if (status & 0x38) {
        flash_write_cmd(offset, 0x50); /* Clear Status Register */
        flash_write_cmd(offset, 0xFF); /* Back to Read Array */
        return STATUS_ERROR_HARDWARE_FAULT;
    }
    
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status HAL_Flash_Init(void)
{
    /* Reset flash to Read Array mode */
    flash_write_cmd(0, 0xFF);
    return STATUS_OK;
}

Status HAL_Flash_Read(uint32_t offset, uint32_t *data)
{
    if (offset >= FLASH_TOTAL_SIZE || (offset % 4) != 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Ensure Read Array mode */
    flash_write_cmd(offset, 0xFF);
    *data = flash_mem[offset / 4];
    return STATUS_OK;
}

Status HAL_Flash_ReadBuffer(uint32_t offset, uint8_t *buf, size_t length)
{
    if (offset >= FLASH_TOTAL_SIZE || (offset % 4) != 0 || (length % 4) != 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Issue Read Array command ONCE for the entire buffer read */
    flash_write_cmd(offset, 0xFF);
    
    uint32_t words = length / 4;
    uint32_t *target = (uint32_t *)buf;
    
    /* Direct MMIO mapped memory reading without trapping into command processing every word */
    for (uint32_t i = 0; i < words; i++) {
        target[i] = flash_mem[(offset / 4) + i];
    }
    
    return STATUS_OK;
}

Status HAL_Flash_EraseSector(uint32_t sector_offset)
{
    if (sector_offset >= FLASH_TOTAL_SIZE || (sector_offset % FLASH_SECTOR_SIZE) != 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Block Erase */
    flash_write_cmd(sector_offset, 0x20); /* Erase Setup */
    flash_write_cmd(sector_offset, 0xD0); /* Erase Confirm */
    
    Status s = flash_wait_ready(sector_offset);
    
    /* Return to Read Array */
    flash_write_cmd(sector_offset, 0xFF);
    return s;
}

Status HAL_Flash_Write(uint32_t offset, uint32_t data)
{
    if (offset >= FLASH_TOTAL_SIZE || (offset % 4) != 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    /* Single Word Program */
    flash_write_cmd(offset, 0x40); /* Program Setup */
    flash_mem[offset / 4] = data;  /* Write Data */
    
    Status s = flash_wait_ready(offset);
    
    /* Return to Read Array */
    flash_write_cmd(offset, 0xFF);
    return s;
}

Status HAL_Flash_WriteBuffer(uint32_t offset, const uint8_t *buf, size_t length)
{
    if (offset >= FLASH_TOTAL_SIZE || (offset % 4) != 0 || (length % 4) != 0) {
        return STATUS_ERROR_INVALID_ARGUMENT;
    }
    
    uint32_t words = length / 4;
    const uint32_t *data = (const uint32_t *)buf;
    
    for (uint32_t i = 0; i < words; i++) {
        if (data[i] != 0xFFFFFFFF) {
            Status s = HAL_Flash_Write(offset + (i * 4), data[i]);
            if (s != STATUS_OK) {
                return s;
            }
        }
    }
    
    return STATUS_OK;
}
