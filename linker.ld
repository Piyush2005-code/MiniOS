/*
 * linker.ld — Linker script for MiniOS ARM64 unikernel
 *
 * Memory layout for QEMU virt machine:
 *   RAM starts at 0x40000000 (512 MB)
 *
 * Stack placed near the top of RAM to give maximum heap space.
 * Heap starts just after BSS and extends to 256 KB below the stack.
 * This allows ML tensor buffers up to ~500 MB (SRS PR-004).
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
        *(.text.boot)
        *(.text .text.*)
        _text_end = .;
    } > RAM

    /* ---- Read-only data ---- */
    .rodata : ALIGN(4096) {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    } > RAM

    /* ---- Initialised data ---- */
    .data : ALIGN(4096) {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    } > RAM

    /* ---- Zero-initialised data ---- */
    .bss : ALIGN(4096) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    /*
     * Stack top: 64 KB from the very end of RAM.
     * The kernel boot stack is at most a few hundred bytes deep, so
     * 64 KB of headroom before the end of RAM is ample.
     */
    _stack_top = ORIGIN(RAM) + LENGTH(RAM) - 0x10000;

    /*
     * Heap: from end of BSS to 256 KB below the stack top.
     * 256 KB guard prevents stack-overflow silent corruption.
     */
    _heap_start = ALIGN(_bss_end, 4096);
    _heap_end   = _stack_top - 0x40000;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
