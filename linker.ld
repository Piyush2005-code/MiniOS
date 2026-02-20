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

    /* ---- Stack ---- */
    /* Stack grows downward from _stack_top.
     * We place it 8MB from RAM origin to leave room
     * for code and data below.
     */
    _stack_top = ORIGIN(RAM) + 0x800000;   /* 0x40800000 */

    /* ---- Heap region (for future memory allocator) ---- */
    /* Starts after BSS, before stack */
    _heap_start = ALIGN(_bss_end, 4096);
    _heap_end   = _stack_top - 0x10000;    /* 64KB below stack top */

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
