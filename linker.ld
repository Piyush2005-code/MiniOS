/*
 * linker.ld — Linker script for MiniOS ARM64 unikernel
 *
 * Memory layout for QEMU virt machine:
 *   RAM starts at 0x40000000
 *   Stack at 0x40800000 (8MB into RAM), grows downward
 *
 * Sections are laid out sequentially:
 *   .text    → Code (starts at 0x40000000)
 *   .rodata  → Read-only data
 *   .data    → Initialized data
 *   .bss     → Zero-initialized data
 *
 * The entry point is _start in boot.S
 */

ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 2048M
}

SECTIONS
{
    /* ---- Code section ---- */
    .text : {
        _text_start = .;

        /* boot.S must be first — it contains the entry point */
        *(.text.boot)

        /* All other code */
        *(.text .text.*)

        _text_end = .;
    } > RAM

    /* ---- Read-only data ---- */
    .rodata : ALIGN(4096) {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    } > RAM

    /* ---- Initialized data ---- */
    .data : ALIGN(4096) {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    } > RAM

    /* ---- Zero-initialized data ---- */
    .bss : ALIGN(4096) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    /* ---- Stack ---- */
    /* Stack grows downward from _stack_top.
     * Placed near the top of RAM to maximize heap space
     * for ML tensor memory (SRS PR-004: up to 64MB).
     * Kernel stack gets 256KB (0x40000) of space.
     */
    _stack_top = ORIGIN(RAM) + LENGTH(RAM) - 0x10000;   /* 64KB from top of RAM */

    /* ---- Heap region (for kernel memory allocator) ---- */
    /* Starts after BSS, extends to 256KB below stack for guard space.
     * This provides ~500MB of heap for thread stacks, arenas,
     * pools, and tensor memory.
     */
    _heap_start = ALIGN(_bss_end, 4096);
    _heap_end   = _stack_top - 0x40000;    /* 256KB below stack top */

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
