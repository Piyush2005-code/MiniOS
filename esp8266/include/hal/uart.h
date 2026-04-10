/**
 * @file uart.h
 * @brief UART HAL API for MiniOS-ESP8266
 *
 * API-compatible with the original ARM64 PL011 uart.h so that all
 * higher-level code (SFU, shell, ONNX debug prints) compiles unchanged.
 *
 * Implementation targets ESP8266 UART0 (GPIO1=TX, GPIO3=RX).
 */

#ifndef MINIOS_ESP8266_HAL_UART_H
#define MINIOS_ESP8266_HAL_UART_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Public API (matches ARM64 original)                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize UART0 at 115200 baud, 8-N-1.
 * @return STATUS_OK always.
 */
Status HAL_UART_Init(void);

/**
 * @brief Transmit one character (blocking).
 * @param c Character to send.
 */
void HAL_UART_PutChar(char c);

/**
 * @brief Receive one character (blocking).
 * @return Character received.
 */
char HAL_UART_GetChar(void);

/**
 * @brief Check if a character is available to read.
 * @return 1 if RX FIFO non-empty, 0 otherwise.
 */
int HAL_UART_HasChar(void);

/**
 * @brief Transmit a null-terminated string.
 *        Inserts CR before every LF for terminal compatibility.
 * @param s String to send.
 */
void HAL_UART_PutString(const char *s);

/**
 * @brief Print a 64-bit value as "0x..." hex (leading zeros suppressed).
 * @param value Value to print.
 */
void HAL_UART_PutHex(uint32_t value);  /* ESP8266: 32-bit only */

/**
 * @brief Print an unsigned decimal integer.
 * @param value Value to print.
 */
void HAL_UART_PutDec(uint32_t value);

#endif /* MINIOS_ESP8266_HAL_UART_H */
