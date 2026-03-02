# MiniOS Project Documentation

## Current Build Overview

### Project Structure
- **Source Code (`src/`)**:
  - `boot/`: Contains assembly files for bootloader and initialization.
  - `hal/`: Implements the Hardware Abstraction Layer (HAL), including memory management unit (MMU) and UART drivers.
  - `kernel/`: Core kernel logic, including the main entry point.
- **Include Files (`include/`)**:
  - `hal/`: Header files for HAL components.
  - `status.h`, `types.h`: General-purpose headers for status codes and type definitions.
- **Build Artifacts (`build/`)**:
  - `kernel.bin`: Compiled binary for the kernel.
  - `kernel.elf`: ELF file for debugging.
  - `obj/`: Object files for modular compilation.
- **Scripts (`scripts/`)**:
  - `run.sh`: Script to execute or test the kernel.

### Build System
- **Makefile**: Automates the build process, likely compiling source files into object files and linking them into the final kernel binary.
- **Toolchain**: Assumes GCC/Clang for ARM64 cross-compilation.

### Current Functionality
- **Bootloader**:
  - Initializes the ARM64 processor.
  - Sets up exception vectors.
- **HAL**:
  - Provides low-level hardware access (e.g., MMU, UART).
- **Kernel**:
  - Contains the main entry point for the operating system.

---

## Next Steps

### 1. Kernel API Development
#### Goals:
- Define a set of kernel APIs to manage processes, memory, and scheduling.
- Ensure modularity and extensibility for future features.

#### Proposed Functions:
- **Process Management**:
  - `int CreateProcess(void (*entry_point)(), void* stack, size_t stack_size);`
  - `void TerminateProcess(int pid);`
  - `void Yield();`
- **Memory Management**:
  - `void* AllocateMemory(size_t size);`
  - `void FreeMemory(void* ptr);`
  - `void MapMemory(void* virtual_addr, void* physical_addr, size_t size);`
- **Inter-Process Communication (IPC)**:
  - `int SendMessage(int pid, const void* message, size_t size);`
  - `int ReceiveMessage(int* pid, void* buffer, size_t size);`

### 2. Process Management Design
#### Goals:
- Implement a process table to track process states (e.g., running, ready, blocked).
- Design a context-switching mechanism.

#### Steps:
1. **Process Table**:
   - Structure to store process metadata (PID, state, stack pointer, etc.).
2. **Context Switching**:
   - Save and restore CPU registers during a switch.
   - Use ARM64 assembly for low-level context management.
3. **Scheduler**:
   - Implement a round-robin scheduler as a starting point.

### 3. Memory Management
#### Goals:
- Enable dynamic memory allocation for processes.
- Implement virtual memory support using the MMU.

#### Steps:
1. **Static Allocator**:
   - Pre-allocate memory regions for kernel and user processes.
2. **Virtual Memory**:
   - Use page tables to map virtual addresses to physical memory.
3. **Memory Protection**:
   - Isolate process memory to prevent corruption.

### 4. Scheduler Design
#### Goals:
- Implement a cooperative scheduler for predictable execution.
- Extend to preemptive scheduling if required.

#### Steps:
1. **Cooperative Scheduling**:
   - Processes yield control explicitly using `Yield()`.
2. **Preemptive Scheduling**:
   - Use timer interrupts to enforce time slices.
3. **Priority Scheduling**:
   - Assign priorities to processes for better resource utilization.

---

## Roadmap
1. **Kernel API**:
   - Define and implement core APIs for process and memory management.
2. **Process Management**:
   - Design and implement the process table and context-switching mechanism.
3. **Memory Management**:
   - Develop a static allocator and integrate MMU-based virtual memory.
4. **Scheduler**:
   - Start with cooperative scheduling and extend to preemptive scheduling.
5. **Testing**:
   - Write unit tests for each module.
   - Use QEMU for integration testing.

---

## References
- ARM Architecture Reference Manual ARMv8-A.
- IEEE 830-1998 Guidelines for Software Requirements Specifications.
- ONNX Specification for Model Compatibility.