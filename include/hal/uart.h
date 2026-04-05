/**
 * @file uart.h
 * @brief PL011 UART driver interface for MiniOS
 *
 * Provides serial I/O for debugging and communication.
 * Targets the PL011 UART on QEMU virt machine (base: 0x09000000).
 *
 * @note Per SRS FR-021/FR-022 and Appendix F
 */

#ifndef MINIOS_HAL_UART_H
#define MINIOS_HAL_UART_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  PL011 UART register offsets                                       */
/* ------------------------------------------------------------------ */
#define UART0_BASE      0x09000000UL

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
