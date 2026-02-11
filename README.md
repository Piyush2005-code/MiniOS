# ML Inference Unikernel - ARM64 Bare-Metal

A minimal, self-contained ARM64 bare-metal kernel designed for ML inference workloads. Runs on QEMU virt machine and can be adapted for Raspberry Pi 3/4.

## Features

- **ARMv8-A Architecture**: Full 64-bit ARM support with FP/SIMD (NEON) enabled
- **Exception Level Management**: Automatic EL3 → EL2 → EL1 transitions
- **MMU Configuration**: 3-level page tables with 4KB granule
  - Identity mapping for kernel RAM (64 MB at 0x40000000)
  - Device mapping for PL011 UART (0x09000000)
  - Normal cacheable memory for code/data
- **UART Driver**: PL011 driver for console output (115200 8N1)
- **Static Memory**: All memory statically allocated (no heap, no malloc)
- **Well-Documented**: Extensive comments explaining MMU setup, exception handling, and hardware initialization

## Project Structure

```
.
├── start.S          # ARM64 boot assembly code
├── mmu.c            # MMU initialization (page tables, cache config)
├── main.c           # UART driver and C entry point
├── linker.ld        # Linker script
├── Makefile.macos   # macOS build configuration
├── Makefile.arch    # Arch Linux build configuration
├── Makefile.ubuntu  # Ubuntu build configuration
└── README.md        # This file
```

## Prerequisites

### macOS (Homebrew)

```bash
# Install ARM64 cross-compiler and QEMU
brew install aarch64-elf-gcc qemu
```

### Arch Linux

```bash
# Install ARM64 cross-compiler and QEMU
sudo pacman -S aarch64-linux-gnu-gcc qemu-system-aarch64
```

### Ubuntu 22.04+

```bash
# Update package list
sudo apt update

# Install ARM64 cross-compiler and QEMU
sudo apt install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-arm
```

## Building

### macOS

```bash
# Clean previous build
make -f Makefile.macos clean

# Build kernel
make -f Makefile.macos

# Build and run in QEMU
make -f Makefile.macos run
```

### Arch Linux

```bash
# Clean previous build
make -f Makefile.arch clean

# Build kernel
make -f Makefile.arch

# Build and run in QEMU
make -f Makefile.arch run
```

### Ubuntu

```bash
# Clean previous build
make -f Makefile.ubuntu clean

# Build kernel
make -f Makefile.ubuntu

# Build and run in QEMU
make -f Makefile.ubuntu run
```

## Running

After running `make run`, you should see output similar to:

```
==============================================
  ML Inference Unikernel - ARM64 Bare Metal
==============================================

System Information:
  Architecture: ARMv8-A (AArch64)
  Exception Level: EL1
  MMU: Enabled (4KB granule, 3-level tables)
  FP/SIMD: Enabled (NEON)
  UART: PL011 @ 0x09000000 (115200 8N1)

Status: Ready for ML inference workload

----------------------------------------------
Add your ML inference code in main.c:main()
----------------------------------------------

Entering idle state (WFI loop)...
```

To exit QEMU, press `Ctrl-A` then `X`.

## Code Structure

### Boot Process (start.S)

1. **Exception Level Detection**: Determines if started at EL3, EL2, or EL1
2. **EL Transitions**: Safely drops from EL3 → EL2 → EL1 if necessary
3. **BSS Zeroing**: Clears uninitialized data section
4. **Stack Setup**: Configures 64 KB stack
5. **Exception Vectors**: Installs vector table for exception handling
6. **FP/SIMD Enable**: Sets CPACR_EL1.FPEN for NEON support
7. **MMU Init**: Calls C function to configure page tables
8. **Jump to main()**: Transfers control to C code

### MMU Configuration (mmu.c)

The MMU setup uses a 3-level translation scheme:

- **L1 Table**: Covers 512 GB (each entry = 1 GB)
- **L2 Table**: Covers 1 GB (each entry = 2 MB)
- **L3 Table**: Covers 2 MB (each entry = 4 KB)

**Memory Regions**:

- **Kernel RAM** (0x40000000 - 64 MB): Identity mapped as Normal Cacheable memory
  - Write-back, read/write allocate
  - Inner shareable
  - Mapped with L2 blocks (2 MB each) for efficiency
  
- **UART Device** (0x09000000): Mapped as Device-nGnRnE memory
  - No gathering, no reordering, no early write acknowledgment
  - Uses L3 page (4 KB) for fine-grained control

**Cache Configuration**:
- Data cache enabled (SCTLR_EL1.C)
- Instruction cache enabled (SCTLR_EL1.I)
- Write-back, read/write allocate for Normal memory
- Inner and outer caches configured via TCR_EL1

### UART Driver (main.c)

**PL011 Initialization**:
- Baud rate: 115200
- Data bits: 8
- Parity: None
- Stop bits: 1
- Reference clock: 24 MHz (QEMU default)

**Functions**:
- `uart_init()`: Configure UART hardware
- `uart_putc(char c)`: Send single character
- `print(const char *str)`: Send null-terminated string

### Exception Handling

All 16 exception vector entries point to a simple handler that:
1. Prints an error message to UART
2. Halts the system with `wfi` (wait for interrupt)

This ensures any unexpected exception is caught and reported.

## Implementing ML Inference

The `main()` function in `main.c` is your entry point for ML code. At this stage:

- ✅ MMU is configured and enabled
- ✅ FP/SIMD (NEON) is available for vector operations
- ✅ UART is ready for debug output
- ✅ All memory is statically allocated (no heap)

### Next Steps

1. **Add Model Data**: Define your neural network weights and biases as static arrays
   ```c
   static float weights_layer1[256][784] __attribute__((aligned(16)));
   static float biases_layer1[256] __attribute__((aligned(16)));
   ```

2. **Implement Inference Functions**: Use NEON intrinsics for optimized computation
   ```c
   #include <arm_neon.h>
   
   void matrix_multiply_neon(float *output, const float *input, 
                             const float *weights, int rows, int cols) {
       // Use NEON intrinsics for vectorized operations
       // e.g., vld1q_f32(), vmulq_f32(), vaddq_f32()
   }
   ```

3. **Load Input Data**: Read from UART or use pre-defined test data
   ```c
   static float input_data[784];  // Example: 28x28 image flattened
   ```

4. **Run Inference**: Call your model functions
   ```c
   float result = run_inference(input_data);
   print("Prediction: ");
   // Format and print result
   ```

5. **Output Results**: Use `print()` to display predictions

### Memory Considerations

- **No Dynamic Allocation**: All arrays must be statically allocated
- **Stack Size**: 64 KB (configurable in `start.S`)
- **Alignment**: Use `__attribute__((aligned(N)))` for NEON optimization
- **Total RAM**: 64 MB mapped (expand in `mmu.c` if needed)

### NEON Example

```c
#include <arm_neon.h>

void vector_add_example(float *result, const float *a, const float *b, int n) {
    for (int i = 0; i < n; i += 4) {
        // Load 4 floats at a time
        float32x4_t va = vld1q_f32(&a[i]);
        float32x4_t vb = vld1q_f32(&b[i]);
        
        // Add vectors
        float32x4_t vr = vaddq_f32(va, vb);
        
        // Store result
        vst1q_f32(&result[i], vr);
    }
}
```

## Debugging

### GDB Debugging

```bash
# Terminal 1: Start QEMU in debug mode (waits for GDB)
make -f Makefile.<os> debug

# Terminal 2: Connect GDB
aarch64-<toolchain>-gdb kernel.elf -ex 'target remote :1234'

# GDB commands
(gdb) break main
(gdb) continue
(gdb) info registers
(gdb) x/10i $pc
```

### Disassembly

```bash
# View complete disassembly
make -f Makefile.<os> disasm

# Or manually
aarch64-<toolchain>-objdump -d kernel.elf | less
```

### QEMU Monitor

Press `Ctrl-A` then `C` to enter QEMU monitor:
```
(qemu) info registers
(qemu) x/10i $pc
(qemu) quit
```

## Raspberry Pi Adaptation

To run on real Raspberry Pi hardware:

1. **Update Memory Map**: Change UART base address in `main.c`
   - RPi 3: UART at 0x3F201000
   - RPi 4: UART at 0xFE201000

2. **Adjust MMU Mappings**: Update `mmu.c` to map the correct UART address

3. **Boot Protocol**: 
   - Rename `kernel.elf` to `kernel8.img`
   - Copy to SD card FAT32 partition
   - RPi firmware loads at 0x80000 (update `linker.ld`)

4. **Clock Frequency**: Adjust UART baud rate divisor for RPi's clock (48 MHz or 250 MHz)

## Troubleshooting

### Build Errors

**Problem**: `aarch64-*-gcc: command not found`
- **Solution**: Ensure toolchain is installed and in PATH

**Problem**: Linker errors about undefined symbols
- **Solution**: Verify all `.o` files are being linked (check Makefile)

### Runtime Issues

**Problem**: No output in QEMU
- **Solution**: Check that MMU is correctly mapping UART (0x09000000)

**Problem**: Immediate crash/exception
- **Solution**: Enable debug mode and check exception vector output

**Problem**: Garbled UART output
- **Solution**: Verify baud rate calculation (24 MHz / (16 × 115200))

## License

This is a reference implementation for educational and research purposes. Feel free to modify and extend for your ML inference needs.

## References

- [ARMv8-A Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [ARM Cortex-A Series Programmer's Guide](https://developer.arm.com/documentation/den0024/latest)
- [PL011 UART Technical Reference](https://developer.arm.com/documentation/ddi0183/latest)
- [QEMU ARM System Emulation](https://www.qemu.org/docs/master/system/arm/virt.html)

---

**Ready to Start?** Run `make -f Makefile.<os> run` and begin implementing your ML inference logic in `main.c`!
