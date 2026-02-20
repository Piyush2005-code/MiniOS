/**
 * @file uart.c
 * @brief PL011 UART driver implementation for MiniOS
 *
 * Targets the PL011 UART on QEMU virt machine.
 * Base address: 0x09000000
 *
 * On QEMU, the UART is pre-initialized so we can send
 * characters immediately. We still configure it properly
 * for completeness and physical hardware compatibility.
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

    /*
     * Set baud rate to 115200 with 24MHz reference clock (QEMU default).
     * Divisor = 24000000 / (16 * 115200) = 13.0208
     * Integer part = 13
     * Fractional part = 0.0208 * 64 + 0.5 = 1.83 ≈ 2
     */
    uart_write_reg(UART_IBRD, 13);
    uart_write_reg(UART_FBRD, 2);

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
