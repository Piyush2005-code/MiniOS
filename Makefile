# ============================================================================
# MiniOS-NetProtocol Makefile
# Branch: net-protocol (9th branch)
#
# Targets:
#   make              — build ARM64 bare-metal kernel.elf + kernel.bin
#   make ETH=lan9118  — build with LAN9118 driver instead of virtio-net
#   make test         — build and run RUDP test suite on host (no ARM64 needed)
#   make clean        — remove build artefacts
#
# Requires: aarch64-linux-gnu-gcc (ARM64 cross-compiler)
#           For tests: any host C compiler (gcc / clang)
# ============================================================================

# -----------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------
CROSS   ?= aarch64-linux-gnu-
CC       = $(CROSS)gcc
LD       = $(CROSS)ld
OBJCOPY  = $(CROSS)objcopy
HOST_CC  = gcc

# -----------------------------------------------------------------------
# Ethernet driver selection (virtio = QEMU default, lan9118 = bare-metal RPi)
# -----------------------------------------------------------------------
ETH     ?= virtio
ifeq ($(ETH), lan9118)
  ETH_FLAG = -DETH_DRIVER_LAN9118
else
  ETH_FLAG = -DETH_DRIVER_VIRTIO
endif

# -----------------------------------------------------------------------
# Directories
# -----------------------------------------------------------------------
SRCDIR   = src
INCDIR   = include
BUILDDIR = build
OBJDIR   = $(BUILDDIR)/obj
TESTDIR  = tests

# -----------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------
SRCS_NET    = $(SRCDIR)/net/rudp.c \
              $(SRCDIR)/net/crc16.c \
              $(SRCDIR)/drivers/eth_driver.c

SRCS_KERNEL = $(SRCDIR)/kernel/main.c

# HAL sources from MiniOS-BootLoader_and_HAL (relative path)
HAL_BASE    = ../MiniOS-BootLoader_and_HAL
HAL_SRCS    = $(HAL_BASE)/src/hal/uart.c \
              $(HAL_BASE)/src/hal/mmu.c

# Boot entry (re-use from BootLoader branch)
BOOT_SRCS   = $(HAL_BASE)/src/boot/boot.S \
              $(HAL_BASE)/src/boot/vectors.S

ALL_SRCS    = $(SRCS_NET) $(SRCS_KERNEL) $(HAL_SRCS) $(BOOT_SRCS)

OBJS        = $(patsubst %.c,$(OBJDIR)/%.o,$(filter %.c,$(ALL_SRCS))) \
              $(patsubst %.S,$(OBJDIR)/%.o,$(filter %.S,$(ALL_SRCS)))

# -----------------------------------------------------------------------
# Flags
# -----------------------------------------------------------------------
ARCH_FLAGS  = -march=armv8-a -mtune=cortex-a53 -mstrict-align
CFLAGS      = $(ARCH_FLAGS) \
              -std=c11 \
              -ffreestanding \
              -fno-builtin \
              -fno-stack-protector \
              -Wall -Wextra \
              -O2 \
              -I$(INCDIR) \
              -I$(HAL_BASE)/include \
              $(ETH_FLAG)

LDFLAGS     = -T linker.ld \
              -nostdlib \
              --gc-sections

# -----------------------------------------------------------------------
# Default target
# -----------------------------------------------------------------------
all: $(BUILDDIR)/kernel.bin

$(BUILDDIR)/kernel.bin: $(BUILDDIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "[net-protocol] Built kernel.bin ($(shell wc -c < $@) bytes)"

$(BUILDDIR)/kernel.elf: $(OBJS) linker.ld | $(BUILDDIR)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "[net-protocol] Linked kernel.elf"

# -----------------------------------------------------------------------
# Compile rules
# -----------------------------------------------------------------------
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.S | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# -----------------------------------------------------------------------
# Directories
# -----------------------------------------------------------------------
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# -----------------------------------------------------------------------
# Host test target (no ARM64 toolchain required)
# -----------------------------------------------------------------------
TEST_BIN    = $(BUILDDIR)/test_rudp
TEST_CFLAGS = -std=c11 -Wall -Wextra -O0 -g -DHOST_TEST \
              -I$(INCDIR) -Itests

test: $(TEST_BIN)
	@echo "[net-protocol] Running RUDP test suite..."
	./$(TEST_BIN)

$(TEST_BIN): $(TESTDIR)/test_rudp.c | $(BUILDDIR)
	$(HOST_CC) $(TEST_CFLAGS) $< -o $@
	@echo "[net-protocol] Test binary built"

# -----------------------------------------------------------------------
# QEMU run helper
# -----------------------------------------------------------------------
QEMU_FLAGS = -machine virt \
             -cpu cortex-a53 \
             -m 256M \
             -nographic \
             -kernel $(BUILDDIR)/kernel.bin \
             -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
             -device virtio-net-device,netdev=net0,mac=52:54:00:12:34:56

run: all
	@echo "[net-protocol] Starting QEMU..."
	@echo "Peer MAC: 52:54:00:12:34:57 (configure tap0 on host)"
	qemu-system-aarch64 $(QEMU_FLAGS)

# -----------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------
clean:
	rm -rf $(BUILDDIR)
	@echo "[net-protocol] Cleaned"

.PHONY: all test run clean