/**
 * @file uart.c
 * @brief PL011 UART driver implementation for MiniOS
 *
 * Supports two platforms selected via the PLATFORM_RPI4 preprocessor flag:
 *
 * QEMU virt (default):
 *   Base: 0x09000000, reference clock: 24 MHz
 *   Baud 115200: IBRD=13, FBRD=2
 *   UART is pre-initialized by QEMU; we still configure it fully.
 *
 * Raspberry Pi 4B (PLATFORM_RPI4):
 *   Base: 0xFE201000 (UART0 = PL011 freed from Bluetooth via config.txt)
 *   Reference clock: 48 MHz (locked by init_uart_clock=48000000 in config.txt)
 *   Baud 115200: IBRD=26, FBRD=3
 *   GPIO14=TXD0, GPIO15=RXD0 (connect USB-TTL adapter here)
 *
 * @complexity Time: O(1) per character, Space: O(1)
 */

#include "hal/uart.h"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static inline void uart_write_reg(uint32_t offset, uint32_t value)
{
    REG32(UART0_BASE + offset) = value;
}

static inline uint32_t uart_read_reg(uint32_t offset)
{
    return REG32(UART0_BASE + offset);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Status HAL_UART_Init(void)
{
    /* Disable UART while configuring */
    uart_write_reg(UART_CR, 0);

    /* Clear all pending interrupts */
    uart_write_reg(UART_ICR, 0x7FF);

#ifdef PLATFORM_RPI4
    /*
     * Set baud rate to 115200 with 48 MHz reference clock.
     * config.txt sets: init_uart_clock=48000000
     * Divisor = 48000000 / (16 * 115200) = 26.0416
     * Integer part  = 26
     * Fractional    = 0.0416 * 64 + 0.5 = 3.17 ≈ 3
     */
    uart_write_reg(UART_IBRD, 26);
    uart_write_reg(UART_FBRD, 3);
#else
    /*
     * Set baud rate to 115200 with 24 MHz reference clock (QEMU default).
     * Divisor = 24000000 / (16 * 115200) = 13.0208
     * Integer part  = 13
     * Fractional    = 0.0208 * 64 + 0.5 = 1.83 ≈ 2
     */
    uart_write_reg(UART_IBRD, 13);
    uart_write_reg(UART_FBRD, 2);
#endif

    /* 8 data bits, no parity, 1 stop bit, enable FIFOs */
    uart_write_reg(UART_LCR_H, UART_LCR_WLEN8 | UART_LCR_FEN);

    /* Mask all interrupts (we use polling) */
    uart_write_reg(UART_IMSC, 0);

    /* Enable UART, TX, and RX */
    uart_write_reg(UART_CR, UART_CR_EN | UART_CR_TXE | UART_CR_RXE);

    return STATUS_OK;
}

void HAL_UART_PutChar(char c)
{
    /* Wait while transmit FIFO is full */
    while (uart_read_reg(UART_FR) & UART_FR_TXFF) {
        /* spin */
    }
    uart_write_reg(UART_DR, (uint32_t)c);
}

char HAL_UART_GetChar(void)
{
    /* Wait while receive FIFO is empty */
    while (uart_read_reg(UART_FR) & UART_FR_RXFE) {
        /* spin */
    }
    return (char)(uart_read_reg(UART_DR) & 0xFF);
}

int HAL_UART_TryGetChar(char *c)
{
    if (uart_read_reg(UART_FR) & UART_FR_RXFE) {
        return 0; /* Nothing available */
    }
    *c = (char)(uart_read_reg(UART_DR) & 0xFF);
    return 1;
}

void HAL_UART_PutString(const char* s)
{
    if (s == NULL) return;

    while (*s) {
        if (*s == '\n') {
            HAL_UART_PutChar('\r');  /* CR before LF for terminal */
        }
        HAL_UART_PutChar(*s);
        s++;
    }
}

void HAL_UART_PutHex(uint64_t value)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    int i;
    bool leading = true;

    HAL_UART_PutString("0x");

    for (i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        if (nibble != 0) leading = false;
        if (!leading || i == 0) {
            HAL_UART_PutChar(hex_digits[nibble]);
        }
    }
}

void HAL_UART_PutDec(uint64_t value)
{
    char buf[21]; /* max uint64 = 20 digits + null */
    int i = 0;

    if (value == 0) {
        HAL_UART_PutChar('0');
        return;
    }

    while (value > 0) {
        buf[i++] = '0' + (char)(value % 10);
        value /= 10;
    }

    /* Print in reverse */
    while (--i >= 0) {
        HAL_UART_PutChar(buf[i]);
    }
}
