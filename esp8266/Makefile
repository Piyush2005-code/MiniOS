# ===========================================================================
#  MiniOS-ESP8266 Build System
#  Target: ESP8266MOD ESP-12E (4MB flash, 80KB RAM)
#  SDK:    ESP8266 NonOS SDK v3.0 (or compatible)
#  Chain:  xtensa-lx106-elf-gcc
# ===========================================================================

# ---------------------------------------------------------------------------
#  Toolchain — adjust SDK_ROOT for your system
# ---------------------------------------------------------------------------

CROSS       := xtensa-lx106-elf-
CC          := $(CROSS)gcc
AR          := $(CROSS)ar
LD          := $(CROSS)gcc
OBJCOPY     := $(CROSS)objcopy
OBJDUMP     := $(CROSS)objdump
SIZE        := $(CROSS)size

# SDK location — override on command line: make SDK_ROOT=/path/to/sdk
SDK_ROOT    ?= $(HOME)/esp8266/ESP8266_NONOS_SDK
ESPTOOL     ?= esptool.py

# Flash port and baud rate — override per machine
FLASH_PORT  ?= /dev/ttyUSB0
FLASH_BAUD  ?= 921600
FLASH_FREQ  ?= 80m
FLASH_MODE  ?= dio
FLASH_SIZE  ?= 4MB

# ---------------------------------------------------------------------------
#  Directories
# ---------------------------------------------------------------------------

BUILD_DIR   := build
SRC_DIR     := src
INC_DIR     := include

SRCS := \
    $(SRC_DIR)/main.c \
    $(SRC_DIR)/hal/uart_esp.c \
    $(SRC_DIR)/hal/timer_esp.c \
    $(SRC_DIR)/hal/wifi_esp.c \
    $(SRC_DIR)/kernel/kmem_esp.c \
    $(SRC_DIR)/kernel/shell_esp.c \
    $(SRC_DIR)/onnx/onnx_runtime_tiny.c \
    $(SRC_DIR)/onnx/onnx_loader_tiny.c \
    $(SRC_DIR)/net/wifi_udp.c \
    $(SRC_DIR)/net/sfu_esp.c \
    $(SRC_DIR)/net/infer_server_esp.c

OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# ---------------------------------------------------------------------------
#  Compiler flags
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
    -DICACHE_FLASH_ATTR='__attribute__((section(".irom0.text")))' \
    -I$(INC_DIR) \
    -I$(SDK_ROOT)/include \
    -I$(SDK_ROOT)/include/json \
    -I$(SDK_ROOT)/driver_lib/include

# User config include path (relative to src/)
CFLAGS += -I$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---------------------------------------------------------------------------
#  Linker flags
# ---------------------------------------------------------------------------

LDSCRIPT    := linker.ld

LDFLAGS := \
    -nostdlib \
    -Wl,--gc-sections \
    -Wl,-Map=$(BUILD_DIR)/minios_esp8266.map \
    -T$(LDSCRIPT) \
    -L$(SDK_ROOT)/lib

LIBS := \
    -lmain \
    -lnet80211 \
    -lwpa \
    -llwip \
    -lpp \
    -lphy \
    -lc \
    -lgcc

# ---------------------------------------------------------------------------
#  Targets
# ---------------------------------------------------------------------------

TARGET      := $(BUILD_DIR)/minios_esp8266
ELF         := $(TARGET).elf
MAP         := $(TARGET).map
BIN0        := $(TARGET)-0x00000.bin
BIN4        := $(TARGET)-0x10000.bin

.PHONY: all clean flash monitor size dirs

all: dirs $(ELF) $(BIN0) $(BIN4) size

dirs:
	@mkdir -p $(BUILD_DIR)/hal \
	           $(BUILD_DIR)/kernel \
	           $(BUILD_DIR)/onnx \
	           $(BUILD_DIR)/net

# ---------------------------------------------------------------------------
#  Compile C sources
# ---------------------------------------------------------------------------

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
#  Link ELF
# ---------------------------------------------------------------------------

$(ELF): $(OBJS)
	@echo "  LD  $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS) \
	    $(SDK_ROOT)/lib/libmain.a \
	    $(SDK_ROOT)/lib/libnet80211.a \
	    $(SDK_ROOT)/lib/libwpa.a \
	    $(SDK_ROOT)/lib/liblwip.a \
	    $(SDK_ROOT)/lib/libpp.a \
	    $(SDK_ROOT)/lib/libphy.a \
	    $(SDL_ROOT)/lib/libcrypto.a \
	    -lgcc -lc

# ---------------------------------------------------------------------------
#  Generate firmware binaries (NonOS SDK 2-binary format)
# ---------------------------------------------------------------------------

$(BIN0) $(BIN4): $(ELF)
	@echo "  BIN $@"
	@$(ESPTOOL) elf2image \
	    --flash_freq $(FLASH_FREQ) \
	    --flash_mode $(FLASH_MODE) \
	    --flash_size $(FLASH_SIZE) \
	    -o $(TARGET)- \
	    $(ELF)

# ---------------------------------------------------------------------------
#  Flash to ESP8266
# ---------------------------------------------------------------------------

flash: all
	@echo "  Flashing to $(FLASH_PORT)..."
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
#  Serial monitor
# ---------------------------------------------------------------------------

monitor:
	@echo "  Monitoring $(FLASH_PORT) at 115200..."
	screen $(FLASH_PORT) 115200

# ---------------------------------------------------------------------------
#  Show binary size
# ---------------------------------------------------------------------------

size: $(ELF)
	@echo ""
	@$(SIZE) --format=berkeley $(ELF)
	@echo ""

# ---------------------------------------------------------------------------
#  Clean
# ---------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR)
