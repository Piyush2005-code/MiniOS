# ============================================================================
# MiniOS — ARM64 Unikernel for ML Inference
# Makefile
# ============================================================================

# ---- Toolchain ----
CROSS    = aarch64-linux-gnu-
CC       = $(CROSS)gcc
AS       = $(CROSS)as
LD       = $(CROSS)ld
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
SIZE     = $(CROSS)size

# ---- Directories ----
SRC_DIR     = src
INC_DIR     = include
BUILD_DIR   = build
OBJ_DIR     = $(BUILD_DIR)/obj

# ---- Output ----
TARGET_ELF  = $(BUILD_DIR)/kernel.elf
TARGET_BIN  = $(BUILD_DIR)/kernel.bin

# ---- Flags ----
CFLAGS   = -std=c11 \
           -ffreestanding \
           -nostdlib \
           -nostartfiles \
           -Wall \
           -Wextra \
           -Werror \
           -O2 \
           -mcpu=cortex-a53 \
           -I$(INC_DIR) \
           -g

ASFLAGS  = -mcpu=cortex-a53 \
           -g

LDFLAGS  = -nostdlib \
           -T linker.ld

# ---- Source files ----
# Assembly sources (order matters: boot.S must be first for linker)
ASM_SRCS = $(SRC_DIR)/boot/boot.S \
           $(SRC_DIR)/boot/vectors.S \
           $(SRC_DIR)/kernel/context.S

# C sources (kernel_main replaced by test runner when TEST=1)
ifeq ($(TEST),1)
C_SRCS   = $(SRC_DIR)/hal/uart.c \
           $(SRC_DIR)/hal/mmu.c \
           $(SRC_DIR)/hal/gic.c \
           $(SRC_DIR)/hal/timer.c \
           $(SRC_DIR)/lib/string.c \
           $(SRC_DIR)/kernel/kmem.c \
           $(SRC_DIR)/kernel/thread.c \
           tests/qemu/test_uart.c \
           tests/qemu/test_timer.c \
           tests/qemu/test_mmu.c \
           tests/qemu/test_ctx.c \
           tests/qemu/test_exception.c \
           tests/qemu/test_system.c \
           tests/qemu/test_runner_main.c
CFLAGS  += -DTEST=1 -I$(INC_DIR) -Wno-unused-variable
else
C_SRCS   = $(SRC_DIR)/hal/uart.c \
           $(SRC_DIR)/hal/mmu.c \
           $(SRC_DIR)/hal/gic.c \
           $(SRC_DIR)/hal/timer.c \
           $(SRC_DIR)/lib/string.c \
           $(SRC_DIR)/kernel/kmem.c \
           $(SRC_DIR)/kernel/thread.c \
           $(SRC_DIR)/kernel/main.c
endif

# ---- Object files ----
ASM_OBJS = $(patsubst $(SRC_DIR)/%.S, $(OBJ_DIR)/%.o, $(ASM_SRCS))
# Kernel C objects (src/ → build/obj/)
KERNEL_C_SRCS = $(filter $(SRC_DIR)/%, $(C_SRCS))
KERNEL_C_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(KERNEL_C_SRCS))
# Test C objects (tests/ → build/obj/tests/)
TEST_C_SRCS   = $(filter tests/%, $(C_SRCS))
TEST_C_OBJS   = $(patsubst %.c, $(OBJ_DIR)/%.o, $(TEST_C_SRCS))
C_OBJS        = $(KERNEL_C_OBJS) $(TEST_C_OBJS)
ALL_OBJS = $(ASM_OBJS) $(C_OBJS)

# ---- QEMU ----
QEMU     = qemu-system-aarch64
QEMU_FLAGS = -machine virt \
             -cpu cortex-a53 \
             -m 512M \
             -nographic \
             -kernel $(TARGET_ELF)

# ============================================================================
# Targets
# ============================================================================

.PHONY: all clean run debug disasm size test_qemu test_qemu_docker

all: $(TARGET_ELF) $(TARGET_BIN)
	@echo ""
	@echo "=== Build complete ==="
	@$(SIZE) $(TARGET_ELF)
	@echo ""

# ---- Link ----
$(TARGET_ELF): $(ALL_OBJS) linker.ld
	@echo "[LD]   $@"
	@mkdir -p $(BUILD_DIR)
	@$(LD) $(LDFLAGS) $(ALL_OBJS) -o $@

# ---- Binary image ----
$(TARGET_BIN): $(TARGET_ELF)
	@echo "[BIN]  $@"
	@$(OBJCOPY) -O binary $< $@

# ---- Compile assembly ----
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.S
	@echo "[AS]   $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# ---- Compile C ----
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "[CC]   $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# ---- Compile test C files ----
$(OBJ_DIR)/tests/%.o: tests/%.c
	@echo "[CC]   $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# ---- Run in QEMU ----
run: $(TARGET_ELF)
	@echo "=== Starting QEMU (Ctrl+A then X to exit) ==="
	@$(QEMU) $(QEMU_FLAGS)

# ---- Build and run QEMU tests (TEST=1) ----
test_qemu:
	@$(MAKE) TEST=1 $(TARGET_ELF)
	@echo "=== Running QEMU Test Suite ==="
	@$(QEMU) $(QEMU_FLAGS) -semihosting-config enable=on,target=native ; true

# ---- Build and run QEMU tests inside Docker (no local QEMU required) ----
test_qemu_docker:
	@$(MAKE) TEST=1 $(TARGET_ELF)
	@echo "=== Running QEMU Test Suite (Docker) ==="
	@bash scripts/docker_qemu_test.sh

# ---- Debug with GDB ----
debug: $(TARGET_ELF)
	@echo "=== Starting QEMU in debug mode (GDB port 1234) ==="
	@echo "    Connect with: aarch64-elf-gdb $(TARGET_ELF)"
	@echo "    Then:         target remote :1234"
	@$(QEMU) $(QEMU_FLAGS) -S -s

# ---- Disassembly ----
disasm: $(TARGET_ELF)
	@$(OBJDUMP) -d $< | less

# ---- Print section sizes ----
size: $(TARGET_ELF)
	@$(SIZE) $<

# ---- Clean ----
clean:
	@echo "[CLEAN]"
	@rm -rf $(BUILD_DIR)
