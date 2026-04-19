/**
 * @file uart.h
 * @brief PL011 UART driver interface for MiniOS
 *
 * Provides serial I/O for debugging and communication.
 *
 * Targets:
 *   QEMU virt machine:     PL011 at 0x09000000, 24 MHz ref clock
 *   Raspberry Pi 4B:       PL011 (UART0) at 0xFE201000, 48 MHz ref clock
 *                          (locked via config.txt: init_uart_clock=48000000)
 *                          UART0 is freed from Bluetooth via dtoverlay=disable-bt
 *
 * @note Per SRS FR-021/FR-022 and Appendix F
 */

#ifndef MINIOS_HAL_UART_H
#define MINIOS_HAL_UART_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  PL011 UART base addresses (platform-conditional)                  */
/* ------------------------------------------------------------------ */
#ifdef PLATFORM_RPI4
/*
 * BCM2711 (Pi 4B): UART0 (PL011) at peripheral base 0xFE000000.
 * UART0 is the full PL011 implementation. By default on Pi 4B it is
 * assigned to Bluetooth; config.txt dtoverlay=disable-bt reassigns
 * it to GPIO 14/15 (TXD/RXD) for serial debugging.
 */
#  define UART0_BASE    0xFE201000UL
#else
/*
 * QEMU virt: PL011 at 0x09000000 (pre-initialized by QEMU).
 */
#  define UART0_BASE    0x09000000UL
#endif

#define UART_DR         0x000   /* Data Register */
#define UART_FR         0x018   /* Flag Register */
#define UART_IBRD       0x024   /* Integer Baud Rate Divisor */
#define UART_FBRD       0x028   /* Fractional Baud Rate Divisor */
#define UART_LCR_H      0x02C   /* Line Control Register */
#define UART_CR         0x030   /* Control Register */
#define UART_IMSC       0x038   /* Interrupt Mask Set/Clear */
#define UART_ICR        0x044   /* Interrupt Clear Register */

/* Flag register bits */
#define UART_FR_TXFF    (1 << 5)    /* Transmit FIFO full */
#define UART_FR_RXFE    (1 << 4)    /* Receive FIFO empty */
#define UART_FR_BUSY    (1 << 3)    /* UART busy */

/* Control register bits */
#define UART_CR_RXE     (1 << 9)    /* Receive enable */
#define UART_CR_TXE     (1 << 8)    /* Transmit enable */
#define UART_CR_EN      (1 << 0)    /* UART enable */

/* Line control bits */
#define UART_LCR_FEN    (1 << 4)    /* FIFO enable */
#define UART_LCR_WLEN8  (3 << 5)    /* 8-bit word length */

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the PL011 UART
 * @return STATUS_OK on success
 */
Status HAL_UART_Init(void);

/**
 * @brief Send a single character via UART
 * @param[in] c Character to send
 */
void HAL_UART_PutChar(char c);

/**
 * @brief Receive a single character from UART (blocking)
 * @return The received character
 */
char HAL_UART_GetChar(void);

/**
 * @brief Try to receive a character from UART (non-blocking)
 * @param[out] c  Receives the character if one is available
 * @return 1 if a character was available and written to *c, 0 if RX FIFO empty
 */
int HAL_UART_TryGetChar(char *c);

/**
 * @brief Send a null-terminated string via UART
 * @param[in] s Pointer to the string
 */
void HAL_UART_PutString(const char* s);

/**
 * @brief Print a 64-bit value in hexadecimal
 * @param[in] value The value to print
 */
void HAL_UART_PutHex(uint64_t value);

/**
 * @brief Print a decimal unsigned integer
 * @param[in] value The value to print
 */
void HAL_UART_PutDec(uint64_t value);

#endif /* MINIOS_HAL_UART_H */
