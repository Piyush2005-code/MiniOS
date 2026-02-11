/*
 * linker.ld - Linker Script for ARM64 Bare-Metal Kernel
 * 
 * Memory layout:
 * - Load address: 0x40080000 (QEMU virt default)
 * - Text section: code and read-only data
 * - Data section: initialized data
 * - BSS section: uninitialized data (zeroed by start.S)
 */

OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    /* Load address for QEMU -kernel */
    . = 0x40080000;
    
    /*
     * Boot code section - must be first
     * Contains _start entry point
     */
    .text.boot : {
        *(.text.boot)
    }
    
    /*
     * Main text section - executable code
     */
    .text : {
        *(.text*)
    }
    
    /*
     * Read-only data section
     * String constants, const variables, etc.
     */
    .rodata : {
        *(.rodata*)
    }
    
    /*
     * Initialized data section
     * Global and static variables with initial values
     */
    .data : {
        *(.data*)
    }
    
    /*
     * Uninitialized data section
     * Global and static variables without initializers
     * Must be zeroed by boot code
     */
    .bss ALIGN(4096) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4096);
        __bss_end = .;
    }
    
    /*
     * Discard unnecessary sections to reduce binary size
     */
    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }
}
