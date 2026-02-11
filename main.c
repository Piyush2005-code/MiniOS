// main.c
// Entry point for ML Inference Unikernel Logic

#include <stdint.h>

// PL011 UART registers (QEMU virt)
#define UART_BASE       0x09000000
#define UART_DR         (*(volatile uint32_t *)(UART_BASE + 0x000))
#define UART_FR         (*(volatile uint32_t *)(UART_BASE + 0x018))
#define UART_IBRD       (*(volatile uint32_t *)(UART_BASE + 0x024))
#define UART_FBRD       (*(volatile uint32_t *)(UART_BASE + 0x028))
#define UART_LCRH       (*(volatile uint32_t *)(UART_BASE + 0x02C))
#define UART_CR         (*(volatile uint32_t *)(UART_BASE + 0x030))
#define UART_IMSC       (*(volatile uint32_t *)(UART_BASE + 0x038))
#define UART_ICR        (*(volatile uint32_t *)(UART_BASE + 0x044))

// Initialise PL011 UART (115200 baud, 8n1)
static void uart_init(void) {
    // Disable UART while configuring
    UART_CR = 0x0;
    // Wait for TX FIFO not full (just in case)
    while (UART_FR & (1 << 5));

    // Set baud rate (assuming 24MHz reference clock on virt)
    UART_IBRD = 13;      // 24MHz / (16 * 115200) = 13.02
    UART_FBRD = 1;       // Fractional part

    // Configure: 8-bit, FIFO enabled, no parity, 1 stop bit
    UART_LCRH = (0x3 << 5) | (1 << 4); // 8-bit, FIFO enable
    // Mask all interrupts
    UART_IMSC = 0x0;
    // Clear any pending interrupts
    UART_ICR = 0x7FF;
    // Enable UART, TX enable
    UART_CR = (1 << 0) | (1 << 8) | (1 << 9); // UARTEN, TXE, RXE
}

static void uart_putc(char c) {
    // Wait if TX FIFO is full
    while (UART_FR & (1 << 5));
    UART_DR = c;
}

static void print(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void main(void) {
    // Initialise UART
    uart_init();

    print("\n\n========================================\n");
    print("MiniOS ML Inference Unikernel (ARM64)\n");
    print("SRS Version 3.0 - Bootstrap Complete\n");
    print("========================================\n\n");
    print("Status: FP/SIMD (NEON) Enabled.\n");
    print("Status: MMU Enabled (Flat Map + UART).\n");
    print("Status: Ready for ML Runtime Initialization.\n\n");

    // TODO: FR-006: ONNX Parser Initialization
    // TODO: FR-016: Static Memory Allocator Setup
    // TODO: FR-013: NEON Kernel Registration

    print("Entering idle loop. System is running.\n");

    // Idle loop
    while (1) {
        __asm__ volatile ("wfi");
    }
}