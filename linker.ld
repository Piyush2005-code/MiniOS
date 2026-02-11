// linker.ld
// Linker script for ARM64 bare-metal [citation:6]
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    /* Load kernel at 1MB */
    . = 0x40080000;

    .text.boot : {
        KEEP(*(.text.boot))
    }

    .text : {
        *(.text*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(4096);
    __end = .;
}