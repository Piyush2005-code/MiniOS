/*
 * linker.ld — MiniOS-NetProtocol (branch: net-protocol)
 *
 * Fully consistent with all other MiniOS branches:
 *   MiniOS-BootLoader_and_HAL/linker.ld
 *   MiniOS-build/minios-ml/linker.ld
 *   MiniOS-feat-onnx/linker.ld
 *
 * Memory layout for QEMU 'virt' machine:
 *   RAM starts at 0x40000000 (QEMU loads kernel here and jumps to _start)
 *   Stack at 0x40800000 (8MB into RAM), grows downward
 *
 * Symbols used by boot.S (must match exactly):
 *   _start       — entry point in boot.S (.text.boot)
 *   _stack_top   — initial SP value (0x40800000)
 *   _bss_start   — start of BSS (cleared to 0 by boot.S)
 *   _bss_end     — end of BSS
 *
 * Symbol used by this branch's main.c:
 *   kernel_main  — C entry point called by boot.S
 */

ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 512M
}

SECTIONS
{
    .text : {
        _text_start = .;
        KEEP(*(.text.boot))
        *(.text .text.*)
        _text_end = .;
    } > RAM

    .rodata : ALIGN(4096) {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    } > RAM

    .data : ALIGN(4096) {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    } > RAM

    .bss : ALIGN(4096) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    .net_buffers : ALIGN(64) {
        _net_buf_start = .;
        *(.net_buffers)
        _net_buf_end   = .;
    } > RAM

    _stack_top  = ORIGIN(RAM) + 0x800000;
    _heap_start = ALIGN(_net_buf_end, 4096);
    _heap_end   = _stack_top - 0x10000;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}