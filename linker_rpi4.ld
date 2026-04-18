/*
 * linker_rpi4.ld — Linker script for MiniOS on Raspberry Pi 4B
 *
 * Memory layout:
 *   The BCM2711 GPU firmware (start4.elf) loads kernel8.img at 0x80000
 *   in AArch64 mode. This is the standard load address for 64-bit Pi kernels.
 *
 *   RAM: 0x00080000 – 0x2007FFFF (512 MB starting at 0x80000)
 *   Stack at top of RAM, grows downward.
 *
 * Sections:
 *   .text    → Code (.text.boot entry point first)
 *   .rodata  → Read-only data
 *   .data    → Initialized data
 *   .bss     → Zero-initialized data
 *
 * The entry point is _start in boot.S
 */

ENTRY(_start)

MEMORY
{
    /*
     * Pi 4B: GPU firmware places kernel at 0x80000.
     * We map 512 MB from that point.
     * Physical RAM on Pi 4B (8 GB model) starts at 0x00000000.
     */
    RAM (rwx) : ORIGIN = 0x80000, LENGTH = 512M
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
    /*
     * Stack grows downward from _stack_top.
     * Placed 64 KB below the top of the 512 MB window.
     *   Top of window: 0x80000 + 512M = 0x20080000
     *   Stack top:     0x20080000 - 0x10000 = 0x20070000
     */
    _stack_top = ORIGIN(RAM) + LENGTH(RAM) - 0x10000;

    /* ---- Heap region (for kernel memory allocator) ---- */
    /*
     * Starts immediately after BSS (4 KB aligned).
     * Extends to 256 KB below the stack top as a guard zone.
     * Provides ~500 MB of heap for thread stacks, arenas,
     * tensor buffers, and the ONNX runtime.
     */
    _heap_start = ALIGN(_bss_end, 4096);
    _heap_end   = _stack_top - 0x40000;    /* 256 KB below stack */

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
