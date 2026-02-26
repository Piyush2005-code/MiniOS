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
           $(SRC_DIR)/boot/vectors.S

# C sources
C_SRCS   = $(SRC_DIR)/hal/uart.c \
           $(SRC_DIR)/hal/mmu.c \
           $(SRC_DIR)/kernel/main.c \
           $(SRC_DIR)/onnx/onnx_types.c \
           $(SRC_DIR)/onnx/onnx_graph.c \
           $(SRC_DIR)/onnx/onnx_runtime.c \
           $(SRC_DIR)/onnx/onnx_loader.c \
           $(SRC_DIR)/onnx/onnx_demo.c \
           $(SRC_DIR)/onnx/onnx_loader_demo.c

# ---- Object files ----
ASM_OBJS = $(patsubst $(SRC_DIR)/%.S, $(OBJ_DIR)/%.o, $(ASM_SRCS))
C_OBJS   = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(C_SRCS))
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

.PHONY: all clean run debug disasm size

all: $(TARGET_ELF) $(TARGET_BIN)
	@echo ""
	@echo "=== Build complete ==="
	@$(SIZE) $(TARGET_ELF)
	@echo ""

# ---- Link ----
$(TARGET_ELF): $(ALL_OBJS) linker.ld
	@echo "[LD] Linking $@..."
	@$(LD) $(LDFLAGS) $(ALL_OBJS) -o $@

# ---- Binary ----
$(TARGET_BIN): $(TARGET_ELF)
	@echo "[OBJCOPY] Creating flat binary $@..."
	@$(OBJCOPY) -O binary $< $@

# ---- Assembly files ----
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

# ---- C files ----
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ---- Run in QEMU ----
run: $(TARGET_ELF)
	@echo ""
	@echo "=== Starting QEMU ==="
	@echo "    Press Ctrl+A then X to exit"
	@echo ""
	@bash scripts/run.sh

# ---- Debug ----
debug: $(TARGET_ELF)
	@echo "[QEMU] Starting with GDB server on :1234..."
	@$(QEMU) $(QEMU_FLAGS) -S -gdb tcp::1234

# ---- Disassembly ----
disasm: $(TARGET_ELF)
	@$(OBJDUMP) -d $< | less

# ---- Size analysis ----
size: $(TARGET_ELF)
	@$(SIZE) -A $<

# ---- Clean ----
clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD_DIR)
