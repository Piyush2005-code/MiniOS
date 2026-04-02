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
GEN_DIR     = $(BUILD_DIR)/gen
STORAGE_DIR = $(SRC_DIR)/storage

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
           -I$(GEN_DIR) \
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

# C sources
C_SRCS   = $(SRC_DIR)/hal/uart.c \
           $(SRC_DIR)/hal/mmu.c \
           $(SRC_DIR)/hal/gic.c \
           $(SRC_DIR)/hal/timer.c \
           $(SRC_DIR)/hal/flash.c \
           $(SRC_DIR)/lib/string.c \
           $(SRC_DIR)/kernel/kmem.c \
           $(SRC_DIR)/kernel/thread.c \
           $(SRC_DIR)/kernel/daemon.c \
           $(SRC_DIR)/kernel/ulfs.c \
           $(SRC_DIR)/kernel/fs_cmds.c \
           $(SRC_DIR)/kernel/cmd.c \
           $(SRC_DIR)/kernel/shell.c \
           $(SRC_DIR)/kernel/storage.c \
           $(SRC_DIR)/kernel/initfs.c \
           $(SRC_DIR)/kernel/main.c \
           $(SRC_DIR)/drivers/virtio_net.c \
           $(SRC_DIR)/net/ethernet.c \
           $(SRC_DIR)/net/arp.c \
           $(SRC_DIR)/net/ipv4.c \
           $(SRC_DIR)/net/udp.c \
           $(SRC_DIR)/onnx/onnx_types.c \
           $(SRC_DIR)/onnx/onnx_graph.c \
           $(SRC_DIR)/onnx/onnx_runtime.c \
           $(SRC_DIR)/onnx/onnx_loader.c \
           $(SRC_DIR)/onnx/onnx_cmds.c \
           $(SRC_DIR)/onnx/test_models/simple_add_model.c \
           $(SRC_DIR)/onnx/test_models/simple_mul_model.c \
           $(SRC_DIR)/onnx/test_models/simple_relu_model.c \
           $(SRC_DIR)/onnx/test_models/two_op_model_model.c \
           $(SRC_DIR)/onnx/test_models/matmul_model_model.c \
           $(SRC_DIR)/onnx/onnx_test.c \
           $(SRC_DIR)/onnx/onnx_loader_demo.c

# ---- Generated sources (initfs file embedding) ----
GEN_SRCS = $(GEN_DIR)/initfs_data.c

# ---- Object files ----
ASM_OBJS = $(patsubst $(SRC_DIR)/%.S, $(OBJ_DIR)/%.o, $(ASM_SRCS))
C_OBJS   = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(C_SRCS))
GEN_OBJS = $(OBJ_DIR)/gen/initfs_data.o
ALL_OBJS = $(ASM_OBJS) $(C_OBJS) $(GEN_OBJS)

# ---- QEMU ----
QEMU     = qemu-system-aarch64
QEMU_FLAGS = -machine virt \
             -cpu cortex-a53 \
             -m 512M \
             -nographic \
             -kernel $(TARGET_ELF) \
             -drive if=pflash,file=flash.img,format=raw,index=1 \
             -netdev user,id=net0,hostfwd=udp::9000-:9000 \
             -device virtio-net-device,netdev=net0

# ============================================================================
# Targets
# ============================================================================

.PHONY: all clean run debug disasm size generate_initfs

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

# ---- Generated initfs data (embed src/storage/ files) ----
generate_initfs: $(GEN_SRCS)

$(GEN_SRCS) $(GEN_DIR)/initfs_data.h: $(wildcard $(STORAGE_DIR)/*) $(wildcard $(STORAGE_DIR)/**/*) scripts/embed_storage.py
	@mkdir -p $(GEN_DIR)
	@echo "[GEN] Embedding storage files..."
	@python3 scripts/embed_storage.py $(STORAGE_DIR) $(GEN_DIR)

# initfs.c depends on the generated header
$(OBJ_DIR)/kernel/initfs.o: $(GEN_DIR)/initfs_data.h

$(OBJ_DIR)/gen/initfs_data.o: $(GEN_DIR)/initfs_data.c $(GEN_DIR)/initfs_data.h
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ---- Run in QEMU ----
run: $(TARGET_ELF) flash.img
	@echo ""
	@echo "=== Starting QEMU ==="
	@echo "    Press Ctrl+A then X to exit"
	@echo ""
	@bash scripts/run.sh

flash.img:
	@echo "Creating empty 64MB flash.img..."
	@dd if=/dev/zero of=flash.img bs=1M count=64

# ---- Debug with GDB ----
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
