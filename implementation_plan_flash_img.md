# Goal: Add Non-Volatile Storage to MiniOS

The project SRS (Product Environment) states the requirement for "Minimal non-volatile storage for bootloader and configuration (4MB Flash minimum)."
To fulfill this requirement and the user's request, we will introduce a non-volatile storage segment into MiniOS.

Since QEMU's `virt` machine exposes two 64MB flash memory banks (pflash0 for bootROM at `0x00000000`, and pflash1 at `0x04000000`), we can leverage `pflash1` as our target 4MB Flash hardware. This memory region is already mapped by our MMU as `Device-nGnRnE`. We simply need to implement a driver capable of issuing Common Flash Interface (CFI) commands (Intel CFI01 standard, which QEMU emulates for `virt`) to erase and write flash sectors, and modify `run.sh` to back `pflash1` with a persistent host file (`flash.img`).

## User Review Required

> [!IMPORTANT]
> **Use of QEMU pflash (CFI01)**: The QEMU `virt` machine implements Intel CFI01 compliant flash. Writing to this flash is not a simple memory assignment; it requires unlocking, sending an erase command, waiting, sending a byte-program command, and polling the status register. I plan to implement a basic `hal/flash.c` driver to execute these commands.

> [!TIP]
> **Host Persistence**: We will create a `flash.img` file on the host machine and map it to QEMU via `-drive if=pflash,file=flash.img,format=raw,index=1`. Thus, the non-volatile storage will persist across QEMU reboots!

## Proposed Changes

---

### Hardware Abstraction Layer (HAL)
Provides the CFI driver to read, erase, and program the flash memory located at 0x04000000.

#### [NEW] [flash.h](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/include/hal/flash.h)
Defines the `HAL_Flash_Init`, `HAL_Flash_Read`, `HAL_Flash_EraseSector`, and `HAL_Flash_Write` APIs. Defines target address `0x04000000` and `FLASH_SECTOR_SIZE` (256KB for QEMU virt flash).

#### [NEW] [flash.c](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/hal/flash.c)
Implements Intel CFI01 command sequences:
1. **Read Array Mode** `*(addr) = 0xFF`
2. **Clear Status** `*(addr) = 0x50`
3. **Block Erase** `*(block_addr) = 0x20` followed by `0xD0`
4. **Program Word** `*(addr) = 0x40` followed by writing the word.
Provides polling loops on the Status Register's Ready bit (`0x80`).

---

### Non-Volatile Storage Manager (Kernel)
A high-level interface that uses the HAL flash driver to provide an abstracted NVRAM API.

#### [NEW] [storage.h](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/include/kernel/storage.h)
Defines a `STORAGE_Init()`, `STORAGE_Read()`, and `STORAGE_Write()` API to treat the first 4MB of the flash drive as a linear non-volatile storage region.

#### [NEW] [storage.c](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/storage.c)
Implements wear-leveling or direct buffered erase/writes. For simplicity, since the flash can only be flipped 1 -> 0 without an erase, we will implement a buffered sector erase/write approach if an overwrite is needed, offering a simple configuration storage API.

---

### Initialization & Tooling
Wires the new subsystems into the boot process and build system.

#### [MODIFY] [kapi.h](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/include/kernel/kapi.h)
Include `<kernel/storage.h>`.

#### [MODIFY] [main.c](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/src/kernel/main.c)
Call `HAL_Flash_Init()` and `STORAGE_Init()` during `KERNEL_Init()` before daemons are started.

#### [MODIFY] [Makefile](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/Makefile)
Add `src/hal/flash.c` and `src/kernel/storage.c` to `C_SRCS`.
Update `run` targets to depend on an empty `flash.img` (64MB size) if it doesn't exist. Add `-drive if=pflash,file=build/flash.img,format=raw,index=1` to `QEMU_FLAGS`.

#### [MODIFY] [run.sh](file:///Users/piyushsinghbhati/Documents/Programming/MiniOS/scripts/run.sh)
Modify the launch script to auto-generate `flash.img` formatted to 64MB zeroed/erased state and hook it up to QEMU parameters.

## Open Questions

> [!QUESTION]
> Do you approve this approach for providing True Non-Volatile Storage via QEMU's `pflash` device, or would you prefer a simpler software-only `.nvram` RAM section that isn't cleared by `boot.S` (which persists during soft reboots but not if QEMU closes)? The Hardware Flash approach matches actual physical deployment constraints better.

## Verification Plan

### Automated Tests
- Run `make run` to boot the modified kernel, write a test string "MINIOS_NV_CONF" to flash, read it back.
- Terminate QEMU, run it again, and verify the bootloader can read the previously written "MINIOS_NV_CONF" string from the flash space, proving persistence across emulator restarts.
