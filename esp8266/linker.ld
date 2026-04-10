/* ===========================================================================
 * linker.ld — MiniOS-ESP8266 Linker Script
 * Target: ESP8266MOD ESP-12E (Xtensa LX106 @ 80 MHz)
 *
 * Memory layout:
 *   iRAM  (32 KB): instruction RAM — critical hot-path code
 *   dRAM  (80 KB): data RAM — stack, heap, global variables
 *   flash (4 MB):  SPI flash — code (cached), read-only strings, model weights
 *
 * Flash access is via 32 KB instruction cache (.irom0 region).
 * Functions marked ICACHE_FLASH_ATTR go to .irom0.text (flash, cached).
 * Functions NOT marked execute from iRAM and run at full speed.
 * =========================================================================== */

MEMORY
{
    /* 32 KB instruction RAM — no wait states, fastest execution */
    iram1_0_seg  : org = 0x40100000, len = 0x8000

    /* 80 KB data RAM — all variables, stack, heap live here */
    dram0_0_seg  : org = 0x3FFE8000, len = 0x14000

    /* Flash cache window — 32 KB mapped for code access */
    irom0_0_seg  : org = 0x40200000, len = 0x100000
}

/* Stack pointer: top of dRAM */
PROVIDE(_stack_top = ORIGIN(dram0_0_seg) + LENGTH(dram0_0_seg));

ENTRY(call_user_start)

SECTIONS
{
    /* ---- Instruction RAM: hot-path code (interrupt handlers, tight loops) ---- */
    .iram0.vectors :
    {
        _iram_start = ABSOLUTE(.);
        KEEP(*(.DebugExceptionVector.text))
        KEEP(*(.NMIExceptionVector.text))
        KEEP(*(.KernelExceptionVector.text))
        KEEP(*(.UserExceptionVector.text))
        KEEP(*(.DoubleExceptionVector.text))
        KEEP(*(.WindowOverflow4.text))
        KEEP(*(.WindowUnderflow4.text))
        KEEP(*(.WindowOverflow8.text))
        KEEP(*(.WindowUnderflow8.text))
        KEEP(*(.WindowOverflow12.text))
        KEEP(*(.WindowUnderflow12.text))
    } > iram1_0_seg

    .iram0.text :
    {
        /* Timer and interrupt-driven code goes to iRAM */
        *(.iram0.text)
        *(.iram.text)
        *libmain.a:(.text)
        *libpp.a:(.text)
    } > iram1_0_seg

    _iram_end = ABSOLUTE(.);

    /* ---- Flash (cached): most application code, strings, const data ---- */
    .irom0.text : AT(LOADADDR(.iram0.text) + SIZEOF(.iram0.text))
    {
        _irom0_text_start = ABSOLUTE(.);
        /* Functions compiled with ICACHE_FLASH_ATTR run from flash cache */
        *(.irom0.text)
        *(.irom.text)
        /* All other code not explicitly in iRAM goes to flash */
        *(.text)
        *(.text.*)
        *(.stub .gnu.warning .gnu.linkonce.t.*)
        *(.rodata)
        *(.rodata.*)
        /* Model weight arrays (declared const, live in rodata = flash) */
        *(.flash.rodata)
        _irom0_text_end = ABSOLUTE(.);
    } > irom0_0_seg

    /* ---- Data RAM: initialized data ---- */
    .dram0.data :
    {
        _data_start = ABSOLUTE(.);
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
        *(.dram0.data)
        *(.dram1.data)
        _data_end = ABSOLUTE(.);
    } > dram0_0_seg

    /* ---- Data RAM: zero-initialized (BSS) ---- */
    .dram0.bss (NOLOAD) :
    {
        _bss_start = ABSOLUTE(.);
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        _bss_end = ABSOLUTE(.);
    } > dram0_0_seg

    /* ---- Heap starts after BSS ---- */
    _heap_start = _bss_end;
    _heap_end   = _stack_top - 0x1000; /* reserve 4 KB for stack */

    /* ---- SDK reserved areas ---- */
    .dram0.data :
    {
        *libmain.a:(.data .rodata .bss)
        *libnet80211.a:(.data .rodata .bss)
        *liblwip.a:(.data .rodata .bss)
    } > dram0_0_seg

    /* Discard debug sections to save space */
    /DISCARD/ :
    {
        *(.comment)
        *(.pdr)
        *(.note.*)
        *(.debug*)
        *(.gnu_debuglink)
    }
}
