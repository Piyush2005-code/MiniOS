# ===========================================================================
#  MiniOS-ESP8266 Build System  (Checkpoint 1 — Boot & Scheduler)
#  Target: ESP8266MOD ESP-12E  (4 MB flash, 80 KB dRAM, 32 KB iRAM)
#  SDK:    ESP8266 NonOS SDK v3.0 (or compatible v2.2+)
#  Chain:  xtensa-lx106-elf-gcc
# ===========================================================================

# ---------------------------------------------------------------------------
#  Toolchain — set SDK_ROOT to your NonOS SDK installation
#  e.g.  make SDK_ROOT=$HOME/esp8266/ESP8266_NONOS_SDK
# ---------------------------------------------------------------------------

# Toolchain — absolute path to /opt install; override with CROSS= on command line
CROSS       ?= /opt/xtensa-lx106-elf-gcc/bin/xtensa-lx106-elf-
CC          := $(CROSS)gcc
AR          := $(CROSS)ar
LD          := $(CROSS)gcc
OBJCOPY     := $(CROSS)objcopy
OBJDUMP     := $(CROSS)objdump
SIZE        := $(CROSS)size

# Explicit path to OUR toolchain's libgcc (8.4.0).
# Do NOT use -lgcc: with -L$(SDK_ROOT)/lib the linker would find the SDK's
# bundled gcc-4.8.5 libgcc.a first, causing l32r dangerous relocation errors.
TOOLCHAIN_LIBGCC := $(shell $(CC) -print-libgcc-file-name)


# SDK location — override on command line or export SDK_ROOT=...
SDK_ROOT    ?= $(HOME)/esp8266/ESP8266_NONOS_SDK

# esptool — prefer explicit binaries, then python module if installed
ESPTOOL     ?= $(shell \
	if command -v esptool.py >/dev/null 2>&1; then \
		printf '%s\n' esptool.py; \
	elif command -v esptool >/dev/null 2>&1; then \
		printf '%s\n' esptool; \
	elif python3 -m esptool version >/dev/null 2>&1; then \
		printf '%s\n' "python3 -m esptool"; \
	else \
		printf '%s\n' ""; \
	fi)

# Flash / serial settings — override per machine
FLASH_PORT  ?= /dev/ttyUSB0
FLASH_BAUD  ?= 921600
FLASH_FREQ  ?= 80m
FLASH_MODE  ?= dio
FLASH_SIZE  ?= 4MB

# ---------------------------------------------------------------------------
#  Directory layout
# ---------------------------------------------------------------------------

BUILD_DIR   := build
SRC_DIR     := src
INC_DIR     := include

# ---------------------------------------------------------------------------
#  Source files
#
#  Checkpoint 1 adds:
#    src/kernel/sched_esp.c   — cooperative scheduler (system_os_task + timer)
#
#  All other sources are unchanged from the initial ESP8266 port.
# ---------------------------------------------------------------------------

SRCS := \
    $(SRC_DIR)/main.c \
    $(SRC_DIR)/hal/uart_esp.c \
    $(SRC_DIR)/hal/timer_esp.c \
    $(SRC_DIR)/hal/wifi_esp.c \
    $(SRC_DIR)/kernel/kmem_esp.c \
    $(SRC_DIR)/kernel/sched_esp.c \
    $(SRC_DIR)/kernel/shell_esp.c \
    $(SRC_DIR)/onnx/onnx_runtime_tiny.c \
    $(SRC_DIR)/onnx/onnx_loader_tiny.c \
    $(SRC_DIR)/net/wifi_udp.c \
    $(SRC_DIR)/net/sfu_esp.c \
    $(SRC_DIR)/net/infer_server_esp.c

OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# ---------------------------------------------------------------------------
#  Compiler flags
#
#  Key flags for Xtensa LX106:
#    -mlongcalls            allow calls to functions > 512 KB away (flash)
#    -mtext-section-literals  place literals alongside code (iRAM-safe)
#    -Os                    size-optimise (iRAM is only 32 KB)
#    -ffunction-sections    enable --gc-sections dead-code elimination
#    -fdata-sections        same for data
#    -fno-exceptions        no C++ exception tables (bare-metal)
#
#  Defines:
#    USE_ESP_SDK            routes types.h to <c_types.h> + <osapi.h>
#    ICACHE_FLASH_ATTR      section attribute for code placed in flash
#    ARCH_ESP8266           guard for ESP8266-specific code paths
#                           (use to suppress #ifdef ARCH_ARM64 blocks)
# ---------------------------------------------------------------------------

CFLAGS := \
    -Os \
    -std=c11 \
    -mlongcalls \
    -mtext-section-literals \
    -ffunction-sections \
    -fdata-sections \
    -fno-exceptions \
    -Wall \
    -Wextra \
    -Wno-unused-parameter \
    -Wno-missing-field-initializers \
    -DUSE_ESP_SDK \
    -DARCH_ESP8266 \
    -DICACHE_FLASH_ATTR='__attribute__((section(".irom0.text")))' \
    -DIRAM_ATTR='__attribute__((section(".iram0.text")))' \
    -I$(INC_DIR) \
    -I$(INC_DIR)/sdk \
    -I$(SDK_ROOT)/include \
    -I$(SDK_ROOT)/include/json \
    -I$(SDK_ROOT)/driver_lib/include

# Expose user_config.h (sits in the esp8266/ root, not under include/)
CFLAGS += -I$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---------------------------------------------------------------------------
#  Linker flags
#
#  -nostdlib              no automatic libc/libgcc pull-in
#  --gc-sections          discard unused functions/data (critical for 32 KB iRAM)
#  -Map=...               produce map file for size analysis
#  -T linker.ld           custom linker script (iRAM / iROM / dRAM sections)
# ---------------------------------------------------------------------------

LDSCRIPT    := linker.ld

LDFLAGS := \
    -nostdlib \
    -Wl,--no-check-sections \
    -Wl,--gc-sections \
    -Wl,-static \
    -Wl,-Map=$(BUILD_DIR)/minios_esp8266.map \
    -Wl,-u,call_user_start \
    -T$(LDSCRIPT) \
    -L$(SDK_ROOT)/lib \
    -L$(SDK_ROOT)/ld

# SDK libraries wrapped in --start-group/--end-group to resolve circular
# symbol references between libmain, libwpa, libcrypto, libnet80211, etc.
# ROM symbols (ets_memset, ets_memcpy ...) come from eagle.rom.addr.v6.ld
# which is INCLUDE-d by linker.ld and found via -L$(SDK_ROOT)/ld above.
LIBS := \
    -Wl,--start-group \
    $(SDK_ROOT)/lib/libmain.a \
    $(SDK_ROOT)/lib/libnet80211.a \
    $(SDK_ROOT)/lib/libwpa.a \
    $(SDK_ROOT)/lib/liblwip.a \
    $(SDK_ROOT)/lib/libpp.a \
    $(SDK_ROOT)/lib/libphy.a \
    $(SDK_ROOT)/lib/libcrypto.a \
    $(TOOLCHAIN_LIBGCC) \
    -lc \
    -Wl,--end-group

# ---------------------------------------------------------------------------
#  Derived targets
# ---------------------------------------------------------------------------

TARGET      := $(BUILD_DIR)/minios_esp8266
ELF         := $(TARGET).elf
MAP         := $(TARGET).map
BIN0        := $(TARGET)-0x00000.bin
BIN4        := $(TARGET)-0x10000.bin

# ---------------------------------------------------------------------------
#  Phonies
# ---------------------------------------------------------------------------

.PHONY: all clean flash monitor size dirs info

all: dirs $(ELF) $(BIN0) $(BIN4) size

# ---------------------------------------------------------------------------
#  Create output subdirectories
#  NOTE: sched_esp.c lives in kernel/, already covered by $(BUILD_DIR)/kernel
# ---------------------------------------------------------------------------

dirs:
	@mkdir -p \
	    $(BUILD_DIR)/hal \
	    $(BUILD_DIR)/kernel \
	    $(BUILD_DIR)/onnx \
	    $(BUILD_DIR)/net

# ---------------------------------------------------------------------------
#  Compile each C source
# ---------------------------------------------------------------------------

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
#  Link ELF
# ---------------------------------------------------------------------------

$(ELF): $(OBJS)
	@echo "  LD  $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

# ---------------------------------------------------------------------------
#  Generate NonOS SDK two-image firmware binaries
#
#  The NonOS SDK requires two binary blobs:
#    0x00000.bin  — iRAM code + vector table  (flashed at offset 0x00000)
#    0x10000.bin  — iROM (flash-cached) code   (flashed at offset 0x10000)
#
#  esptool elf2image produces both from the ELF automatically.
# ---------------------------------------------------------------------------

$(BIN0) $(BIN4): $(ELF)
	@if [ -z "$(ESPTOOL)" ]; then \
		echo "  ERROR: esptool not found. Install it with 'pip install esptool' or set ESPTOOL=/path/to/esptool.py"; \
		exit 127; \
	fi
	@echo "  BIN $@"
	@$(ESPTOOL) elf2image \
	    --flash_freq $(FLASH_FREQ) \
	    --flash_mode $(FLASH_MODE) \
	    --flash_size $(FLASH_SIZE) \
	    -o $(TARGET)- \
	    $(ELF)

# ---------------------------------------------------------------------------
#  Flash firmware to ESP8266
# ---------------------------------------------------------------------------

flash: all
	@if [ -z "$(ESPTOOL)" ]; then \
		echo "  ERROR: esptool not found. Install it with 'pip install esptool' or set ESPTOOL=/path/to/esptool.py"; \
		exit 127; \
	fi
	@echo "  Flashing to $(FLASH_PORT) @ $(FLASH_BAUD) baud..."
	$(ESPTOOL) \
	    --port $(FLASH_PORT) \
	    --baud $(FLASH_BAUD) \
	    write_flash \
	    --flash_freq $(FLASH_FREQ) \
	    --flash_mode $(FLASH_MODE) \
	    --flash_size $(FLASH_SIZE) \
	    0x00000 $(BIN0) \
	    0x10000 $(BIN4)

# ---------------------------------------------------------------------------
#  Open a serial monitor (requires GNU screen)
#  Alternatives: picocom, minicom, or python3 -m serial.tools.miniterm
# ---------------------------------------------------------------------------

monitor:
	@echo "  Monitoring $(FLASH_PORT) at 115200 baud (Ctrl-A K to exit)"
	screen $(FLASH_PORT) 115200

# ---------------------------------------------------------------------------
#  Print binary size analysis (iRAM headroom is critical — must stay < 32 KB)
# ---------------------------------------------------------------------------

size: $(ELF)
	@echo ""
	@$(SIZE) --format=berkeley $(ELF)
	@echo ""
	@echo "  iRAM budget: 32768 bytes"
	@echo "  dRAM budget: 81920 bytes (80 KB; Wi-Fi uses ~30 KB)"
	@echo ""

# ---------------------------------------------------------------------------
#  Print build configuration summary
# ---------------------------------------------------------------------------

info:
	@echo "SDK_ROOT    = $(SDK_ROOT)"
	@echo "CC          = $(CC)"
	@echo "FLASH_PORT  = $(FLASH_PORT)"
	@echo "FLASH_SIZE  = $(FLASH_SIZE)"
	@echo "FLASH_MODE  = $(FLASH_MODE)"
	@echo "HEAP (KMEM) = 24 KB (user_config.h: KMEM_HEAP_SIZE)"
	@echo "SCHED_TICK  = $(shell grep SCHED_TICK_MS include/kernel/sched.h | head -1)"

# ---------------------------------------------------------------------------
#  Clean build artefacts
# ---------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR)
