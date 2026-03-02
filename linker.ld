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
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 512M
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

    /* ---- Kernel Heap ---- */
    /* Used by the bump allocator (MEM_Init).
     * Starts after BSS, page-aligned.
     * Extends up to 128KB below the stack top.
     */
    _heap_start = ALIGN(_bss_end, 4096);
    _heap_end   = ORIGIN(RAM) + 0x780000;  /* 0x40780000 */

    /* ---- Stack ---- */
    /* Main kernel stack: 512KB (grows downward).
     * Stack top at 8MB from RAM origin.
     * Guard gap of 128KB between heap and stack.
     */
    _stack_top = ORIGIN(RAM) + 0x800000;   /* 0x40800000 */

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
