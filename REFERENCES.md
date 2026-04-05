# 📚 MiniOS Technical References & Bibliography

MiniOS is built upon decades of research in operating systems, computer architecture, and machine learning. This document sites the primary technical standards, academic papers, and hardware manuals that informed its design.

## 🏛️ Architecture & Hardware

1. **ARMv8-A Architecture Reference Manual**  
   *ARM DDI 0487*. The definitive guide for ARM64 instruction set, exception levels (EL0-EL3), memory management unit (MMU) configuration, and system registers. Used specifically for `boot.S` and `mmu.c`.

2. **GICv2 Architecture Specification**  
   *ARM IHI 0048*. Technical details for the Generic Interrupt Controller (GIC) v2, used for implementing prioritized IRQ handling in `gic.c`.

3. **Cortex-A53 Technical Reference Manual**  
   *ARM 100032*. Specific performance and pipeline details for the Cortex-A53 CPU, targeting our primary QEMU emulation layer.

## 🧠 Machine Learning & ONNX

4. **ONNX (Open Neural Network Exchange) Specification**  
    *Version 1.8*. The formal specification for the Protobuf-based binary format, graph metadata, and operator semantics (MatMul, Conv2D, etc.). Foundational for `src/onnx/`.

5. **Protocol Buffers (Protobuf) Language Guide**  
   *Google Developers*. Guidance on the binary wire format used for ingestion of exported models.

6. **Gemm: General Matrix Multiplication**  
   *High-Performance Computing Literature*. Theoretical basis for optimizing matrix-matrix multiplication kernels using ARM NEON SIMD instructions.

## 🌐 Networking & Protocols

7. **RFC 908: Reliable Data Protocol (RDP)**  
   *Velten et al.* and **RFC 1151**. Historical context and state machine logic for implementing reliable transport over packet-switched networks. Inspired our `src/net/rudp.c`.

8. **IEEE 802.3 Ethernet Standard**  
   The underlying frame format used for raw packet transmission via the QEMU `virt` machine's virtio-net or similar simulated interfaces.

## ⚙️ Operating System Theory

9. **Operating System Concepts (10th Ed.)**  
   *Silberschatz, Galvin, and Gagne*. Principles of cooperative multitasking, scheduling algorithms (MLQ, Lottery, SJF), and memory management (Pool/Arena allocation).

10. **Unikernels: Library Operating Systems for the Cloud**  
    *Madhavapeddy et al. (ASPLOS 2013)*. The architectural blueprint for the library-OS design pattern, emphasizing a single address space and zero-overhead execution.

11. **IEEE 830-1998**  
    *Recommended Practice for Software Requirements Specifications*. The standard followed for the project's documentation baseline and requirements traceability.

---

**Developed for the MiniOS Project Architecture.**  
*Last Updated: 2026-04-05*
