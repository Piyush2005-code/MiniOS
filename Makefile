# Makefile for ARM64 Bare-Metal
# Uses Clang for compilation, GNU cross-linker for linking

CC = clang
LD = aarch64-linux-gnu-ld
OBJCOPY = llvm-objcopy

TARGET = aarch64-unknown-none
CFLAGS = -target $(TARGET) -ffreestanding -nostdlib -march=armv8-a+fp+simd -O2 -Wall -Wextra
LDFLAGS = -nostdlib -static -T linker.ld

OBJS = start.o mmu.o main.o

all: kernel.elf kernel.bin

start.o: start.S
	$(CC) $(CFLAGS) -c $< -o $@

mmu.o: mmu.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

run: kernel.elf
	qemu-system-aarch64 -M virt -cpu cortex-a72 -nographic -kernel kernel.elf

clean:
	rm -f *.o *.elf *.bin