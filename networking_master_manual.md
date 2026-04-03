# 🌐 MiniOS: Comprehensive Networking & Inference Serving Manual (v1.0)

## 📖 Introduction: The Unikernel Networking Philosophy

Welcome to the definitive technical reference for the **MiniOS Networking Ecosystem**. In traditional operating systems like Linux or Windows, the networking stack is a massive, general-purpose beast designed to handle millions of concurrent connections, complex routing tables, and dozens of different hardware drivers. 

**MiniOS** takes a different path. It is a **Unikernel**—a single-address-space machine where the application and the kernel are compiled into a static image. Because of this, our networking stack doesn't need to be "everything to everyone." Instead, it is a surgically precise implementation of exactly what a high-performance ML inference server requires.

### Core Principles
1.  **Zero dynamic allocation (`malloc`)**: Networking is the bridge between the world and your ML model. If the allocator fragments while processing a 10MB tensor, the system dies. MiniOS uses static buffers, pre-allocated pools, and fixed-length queues to guarantee deterministic performance.
2.  **Zero-Copy (where possible)**: We avoid copying data between "kernel space" and "user space" because no such distinction exists in MiniOS. A packet arriving in a DMA buffer is often processed directly in-place.
3.  **Cooperative Multi-Tasking**: The network driver yields to the inference engine, and the inference engine yields back. This ensures that a burst of incoming traffic doesn't starve the mathematical execution of the ONNX model.
4.  **Security via Minimalism**: By only implementing the protocols we need (VirtIO-Net, Ethernet II, ARP, IPv4, UDP, and our custom SFU), we drastically reduce the attack surface. There is no ICMP (ping) response implemented in the kernel, making the OS "stealthy" to standard network scanners.

---

## 🛠️ Section 1: The Hardware Interface (VirtIO-Net Driver)

At the very bottom of the stack lies the **VirtIO-Net** driver. MiniOS targets the **QEMU `virt` machine**, which provides paravirtualized hardware. This isn't a "real" NIC like an Intel e1000; it is a shared-memory communication channel between the Guest (MiniOS) and the Host (QEMU).

### 1.1 MMIO & Device Discovery
On ARM64, VirtIO devices are exposed via **MMIO (Memory-Mapped I/O)**. This means certain physical addresses are reserved. When MiniOS writes to `0x0A000000`, it isn't writing to RAM; it is sending commands to the virtual network card.

**Device Scanning Logic (`virtio_net.c`):**
```c
/**
 * @brief Scan the MMIO window for a VirtIO-Net device.
 * @return The base physical address if found, or 0.
 */
uint64_t VNIC_Discover(void) {
    uint64_t base;
    
    /* QEMU virt machine typically maps VirtIO between 0x0A000000 and 0x0A000200 */
    for (base = VNET_MMIO_BASE; base <= VNET_MMIO_END; base += VNET_MMIO_STEP) {
        /* Read the Magic Value (must be "virt" in ASCII) */
        uint32_t magic = REG32(base + VNET_MAGIC_OFF);
        
        /* Read the Device ID (Net = 1) */
        uint32_t devid = REG32(base + VNET_DEVID_OFF);
        
        if (magic == 0x74726976 && devid == 1) {
            UART_Printf("[VNIC] Found VirtIO-Net at 0x%x\n", base);
            return base;
        }
    }
    return 0;
}
```

### 1.2 The VirtQueue Mechanism: Rings & Descriptors
VirtIO-Net relies on **VirtQueues**. A VirtQueue is essentially a three-part ring-buffer resident in Guest RAM:

| Structure | Purpose | Managed By |
| :--- | :--- | :--- |
| **Descriptor Table** | References to memory buffers (address + length + flags) | Guest |
| **Available Ring** | Indices of descriptors that are ready for the Host to process | Guest |
| **Used Ring** | Indices of descriptors that the Host has finished processing | Host |

#### Memory Layout of a Descriptor (`virtio_net.h`):
```c
typedef struct {
    uint64_t addr;   /* Physical address of the buffer */
    uint32_t len;    /* Length of the buffer */
    uint16_t flags;  /* F_NEXT (chained), F_WRITE (device-writeable) */
    uint16_t next;   /* Next descriptor index in a chain */
} __attribute__((packed)) vring_desc_t;
```

When receiving a packet:
1.  **MiniOS** fills the Descriptor Table with the address of a 1514-byte static buffer.
2.  **MiniOS** adds the index of that descriptor to the **Available Ring**.
3.  **MiniOS** notifies the Device (writing to a "Queue Notify" register).
4.  **QEMU** (The Host) sees the notify, pulls the buffer address, writes the packet data from the physical network, and puts the index into the **Used Ring**.
5.  **QEMU** sends an interrupt to the CPU.
6.  **MiniOS** interrupt handler reads the **Used Ring**, processes the data, and recycles the descriptor.

### 1.3 Interrupt Handling & GIC Integration
The VirtIO-Net device on QEMU `virt` machine is typically mapped to **SPI (Shared Peripheral Interrupt) 79**.

In `gic.c`, we must unmask this interrupt:
```c
/* Enable VirtIO-Net Interrupt */
GIC_EnableIRQ(79);
GIC_SetPriority(79, 0xA0); /* Medium-high priority */
```

The interrupt handler `VNIC_IRQ_Handler` is called by the kernel's central dispatcher:
```c
void VNIC_IRQ_Handler(void) {
    /* Read CSR to acknowledge the interrupt */
    uint32_t status = REG32(g_vnic.base + VNET_ISR_STATUS_OFF);
    
    if (status & 0x01) { /* Queue Interrupt */
        /* Process completed descriptors from Queue 0 (RX) */
        VNIC_ProcessRX();
    }
}
```

---

## 📡 Section 2: Building the Protocol Stack (L2 to L4)

Once the raw bytes arrive from the VirtIO DMA buffer, they are passed to the **Ethernet Layer**. 

### 2.1 Ethernet II (The Framing Layer)
MiniOS implements **Ethernet II**, the standard for 99.9% of modern traffic.

**Ethernet Header Structure:**
- **Destination MAC (6 bytes)**
- **Source MAC (6 bytes)**
- **EtherType (2 bytes)**:
    - `0x0806`: ARP
    - `0x0800`: IPv4

**Code Snippet: Dispatching the Frame (`ethernet.c`)**
```c
void ETH_Receive(uint8_t* frame, uint32_t len) {
    if (len < 14) return; /* Too small for a header */
    
    uint16_t ethertype = (frame[12] << 8) | frame[13];
    uint8_t* payload = frame + 14;
    uint32_t payload_len = len - 14;
    
    switch(ethertype) {
        case 0x0806: ARP_Handle(payload, payload_len); break;
        case 0x0800: IPV4_Handle(payload, payload_len); break;
        default:     /* Silently drop unknown types */ break;
    }
}
```

### 2.2 ARP: Resolving physical addresses
Because there is no DHCP client in MiniOS, we use a **static IP address** (`10.0.2.15` is the QEMU default). However, to send a packet back to the Host, we need the Host's MAC address.

MiniOS uses **Opportunistic ARP Caching**. Instead of starting an expensive ARP Request/Reply exchange, we simply look at the source MAC of incoming packets from the Gateway (`10.0.2.2`) and cache it immediately.

### 2.3 IPv4: The Network Layer
The IPv4 implementation in MiniOS is **Stateless**. Every packet is treated as an independent unit. We only support a 20-byte fixed header (no IP options) to keep the processing speed at its maximum.

### 2.4 UDP: Transport for Inference
We choose **UDP (User Datagram Protocol)** over TCP for one critical reason: **Real-time Determinism**.
TCP involves complex congestion control, retransmission timers, and multi-threaded state management that can stall an inference cycle. SFU (Simple Framed UDP) handles reliability *above* the transport layer, giving us full control over when a packet is resent.

---

## 🔒 Section 3: The SFU Protocol (Simple Framed UDP)

The **SFU** protocol is the custom session-layer developed for MiniOS. It bridges the gap between unreliable UDP packets and reliable ML model interaction.

### 3.1 The SFU Header (24 Bytes)
Every SFU packet starts with a structured header. This ensures that even if we are receiving raw bytes from a socket, we can perfectly reconstruct the message.

| Field | Bytes | Description |
| :--- | :--- | :--- |
| **Magic** | 4 | Fixed value `0x53465521` ("SFU!") to reject random noise. |
| **Version** | 1 | Currently `0x01`. |
| **Msg Type** | 1 | `PING`, `ACK`, `INFER_REQUEST`, etc. |
| **Flags** | 2 | Bitfield for reliability or streaming. |
| **Req ID** | 4 | Unique ID to match Responses to Requests. |
| **Seq Num** | 4 | For multi-packet transfers. |
| **Total Seq** | 4 | Total number of fragments. |
| **Checksum** | 2 | CRC16-CCITT integrity check. |
| **Payload Len** | 2 | Size of the data following the header. |

### 3.2 Integrity via CRC16-CCITT
Traditional UDP checksums are often disabled in virtual environments or are too weak for large tensors. SFU calculates a **CRC16** over the entire payload.

```c
/**
 * @brief Bitwise CRC16-CCITT implementation (Poly: 0x1021)
 */
uint16_t SFU_Checksum(uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}
```

### 3.3 Reliability: The In-Flight Table
SFU implements an **ACK/NACK** system. When MiniOS sends a "Reliable" message (like an inference result), it doesn't just forget about it. It places it in the **In-Flight Table**.

1.  **SFU_SendReliable()** copies the packet into a private buffer.
2.  The packet stays there until a matching **SFU_MSG_ACK** arrives from the host.
3.  A background thread calls **SFU_Tick()** every 100ms.
4.  If a packet has been in the table for >500ms without an ACK, it is retransmitted.
5.  After 5 failed retries, the connection is considered dead.

---

## 🧠 Section 4: ONNX Inference Integration

The core purpose of the MiniOS networking stack is to serve ML inference. This section details how a remote request is transformed into a mathematical result.

### 4.1 The Inference Workflow
1.  **Host** sends an `SFU_MSG_INFER_REQUEST` on port 9000.
2.  **UDP Layer** dispatches to `SFU_OnReceive`.
3.  **SFU Layer** validates the CRC16 and matches the `msg_type`.
4.  **Inference Server** (Application Layer) receives a pointer to the raw payload (float array).
5.  **ONNX Runtime** executes the model.
6.  Results are serialized into an `SFU_MSG_INFER_RESPONSE` and sent back.

### 4.2 Tensor Serialization
To minimize overhead, MiniOS expects tensors in a **packed-binary** format. For a model expecting a `[1, 10]` float32 input, the payload should be exactly 40 bytes (10 floats * 4 bytes).

### 4.3 Cooperative Execution
Because ONNX inference is CPU-intensive, the runtime explicitly calls `THREAD_Yield()` at each operator boundary. 
```c
/* simplified onnx_runtime.c snippet */
for (int i = 0; i < node_count; i++) {
    ExecuteOperator(node[i]);
    THREAD_Yield(); // Give the network stack a chance to poll VirtIO
}
```
This ensures that while a long-running matrix multiplication is happening, the kernel can still ACK incoming pings or keepalives.

---

## 🚀 Section 5: Integration with MiniOS & QEMU

### 5.1 QEMU Command Line
To enable networking, the following flags are required in your `run.sh` script:
```bash
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a53 \
  -m 512M \
  -kernel build/kernel.elf \
  -netdev user,id=net0,hostfwd=udp::9000-:9000 \
  -device virtio-net-device,netdev=net0
```
- `-netdev user,id=net0`: Creates a user-mode networking backend (SLIRP).
- `hostfwd=udp::9000-:9000`: Forwards traffic from your Host computer's port 9000 to the Guest's port 9000.
- `-device virtio-net-device`: Attaches the paravirtualized network card.

### 5.2 Boot-time Initialization
In `main.c`, the networking stack is initialized in Step 9:
```c
/* Step 9: Initialize Networking Subsystems */
VNIC_Init();       /* Discovery and VirtQueue setup */
SFU_Init();        /* Registration of port 9000 handler */
INFER_ServerInit(); /* Load base ONNX model into memory */
```

---

## 🖥️ Section 6: User Guide & Tooling

### 6.1 Interactive Shell Commands
MiniOS provides built-in commands for real-time network debugging:

| Command | Description |
| :--- | :--- |
| `netstat` | Shows cumulative packet counts, byte counts, and CRC errors. |
| `netlog on` | Enables per-packet hex dumps to the UART console. |
| `netlive` | A "top-like" live view of rolling bandwidth and packet frequency. |

### 6.2 Host-side Python Tools
We provide `sfu_client.py` to interact with the unikernel.
```bash
# Get a list of models on the device
python3 sfu_client.py --cmd list_models

# Run inference on a local model
python3 sfu_client.py --infer input_vals.bin --model tiny_mlp.onnx
```

---

## 🧬 Section 9: Detailed Register Maps & Bitfields

This section provides exhaustive documentation for the MMIO registers used by the MiniOS networking stack.

### 9.1 VirtIO-Net MMIO Registers (Base + Offset)

| Offset | Name | Read/Write | Description |
| :--- | :--- | :--- | :--- |
| `0x00` | `MagicValue` | R | Always `0x74726976` ("virt") |
| `0x04` | `Version` | R | MiniOS supports version 1 and 2 |
| `0x08` | `DeviceID` | R | `1` for Networking |
| `0x0C` | `VendorID` | R | `0x554D4551` ("QEMU") |
| `0x10` | `DeviceFeatures` | R | Bitmask of features the host supports |
| `0x14` | `DeviceFeaturesSel` | W | Select active feature set |
| `0x20` | `DriverFeatures` | W | Bitmask of features the guest supports |
| `0x24` | `DriverFeaturesSel` | W | Select driver feature set |
| `0x30` | `GuestPageSize` | W | For legacy memory mapping (usually 4KB) |
| `0x34` | `QueueSel` | W | Select current VirtQueue (0=RX, 1=TX) |
| `0x38` | `QueueNumMax` | R | Max entries in selected queue |
| `0x3C` | `QueueNum` | W | Set queue size |
| `0x40` | `QueueAlign` | W | Queue alignment (usually 4KB) |
| `0x44` | `QueuePFN` | W | Base Page Frame Number of the queue |
| `0x50` | `QueueNotify` | W | Signal the device that data is ready |
| `0x60` | `InterruptStatus` | R | Why was the IRQ raised? |
| `0x64` | `InterruptACK` | W | Acknowledge IRQ |
| `0x70` | `Status` | RW | Device status (ACK, DRIVER, OK, FAILED) |

### 9.2 Device Feature Bits (`VIRTIO_NET_F_*`)

By negotiating these bits, MiniOS and QEMU agree on the protocol capabilities:

- **Bit 0 (`CSUM`)**: Host verifies checksums for us. (Enabled)
- **Bit 5 (`MAC`)**: Host provides a fixed MAC address. (Enabled)
- **Bit 21 (`GUEST_ANNOUNCE`)**: MiniOS will announce its MAC on boot. (Enabled)
- **Bit 23 (`VIRTIO_F_VERSION_1`)**: Use modern 1.0 protocol. (Enabled)

---

## 📜 Section 10: Full Header Appendices (Annotated Source)

### 10.1 `include/net/sfu.h` (The Complete Protocol Spec)

```c
/**
 * @file sfu.h
 * @brief Simple Framed UDP (SFU) Session Layer Protocol Definitions
 */

#ifndef MINI_SFU_H
#define MINI_SFU_H

#include "types.h"

/* 
 * SFU Magic Number: "SFU!" in ASCII
 * Total Header Size: 24 Bytes (Fixed-length for zero-alloc parsing)
 */
#define SFU_MAGIC      0x53465521
#define SFU_VERSION    0x01
#define SFU_PORT       9000
#define SFU_MAX_PAYLOAD 1448

/* Message Type Definitions */
typedef enum {
    SFU_MSG_PING            = 0x01, // Keep-alive request
    SFU_MSG_PONG            = 0x02, // Keep-alive response
    SFU_MSG_INFER_REQUEST   = 0x10, // Binary tensor input
    SFU_MSG_INFER_RESPONSE  = 0x11, // Binary tensor output
    SFU_MSG_ACK             = 0x20, // Reliability confirmation
    SFU_MSG_NACK            = 0x21, // Reliability failure/retry-request
    SFU_MSG_CMD             = 0x30, // ASCII command (list_models, etc.)
    SFU_MSG_CMD_RESPONSE    = 0x31, // ASCII command results
    SFU_MSG_ERROR           = 0xFF  // Generic failure notification
} sfu_msg_type_t;

/* Header Layout (Standardized to 24 Bytes) */
typedef struct {
    uint32_t magic;       // 0x53465521
    uint8_t  version;     // 0x01
    uint8_t  msg_type;    // sfu_msg_type_t
    uint16_t flags;       // RESERVED
    uint32_t req_id;      // Unique ID for request-matching
    uint32_t seq_num;     // Fragment index (0..total-1)
    uint32_t total_seq;   // Total fragments in message
    uint16_t checksum;    // CRC16-CCITT of payload
    uint16_t payload_len; // Size of bytes following header
} __attribute__((packed)) sfu_header_t;

/* Reliability Configuration */
#define SFU_TIMEOUT_MS  500u  // Time before retransmission
#define SFU_MAX_RETRIES 5     // Max attempts before failure
#define SFU_MAX_INFLIGHT 16   // Parallel outstanding reliable messages

#endif // MINI_SFU_H
```

---

## 🗺️ Section 11: The Future Roadmap: Scaling Networking

While the current stack is highly optimized for single-session inference, future sprints will expand these capabilities:

### 11.1 Multiprocessor Driver (VMM/GICv3)
In the current design, all network IRQs are handled by Core 0. As model sizes grow, we will migrate to a **Multiqueue** setup where each VirtQueue is assigned to a specific CPU core, allowing parallel packet parsing and inference.

### 11.2 PCI Support
Currently, we use MMIO. For production hardware (like AWS Firecracker or real silicon), we will implement the **VirtIO-over-PCI** transport, allowing the stack to run at 10GbE+ speeds by leveraging MSI-X message-signaled interrupts.

### 11.3 ULFS File Streaming (Remote Mounting)
Using the SFU protocol, we intend to provide "Remote Flash" capabilities—where the unikernel can boot with a minimal OS image and stream its ONNX models directly over the network into memory, reducing the boot image size from 64MB down to <1MB.

---

## 📚 Section 12: Glossary of Terms

- **Unikernel**: A specialized, single-address-space virtual machine.
- **VirtIO**: A paravirtualization standard for hypervisors.
- **Descriptors**: Hardware-readable pointers to memory regions.
- **SLIRP**: The user-mode NAT engine inside QEMU.
- **CRC16-CCITT**: A 16-bit Cyclic Redundancy Check used for high-integrity data validation.
- **Determinism**: The quality of having a predictable execution time and memory footprint.

---

## 🧪 Section 13: Testing & Command Reference

Testing the MiniOS networking stack requires a dual-pronged approach: monitoring the **Guest (MiniOS)** internal state via its shell and interacting from the **Host (Linux/macOS)** using our Python-based CLI tools.

### 13.1 MiniOS In-Kernel Commands

Once MiniOS is running in QEMU, you can use the interactive serial console to run the following diagnostic commands:

#### `netstat` — Cumulative Statistics
Provides a total count of all SFU traffic since the kernel booted. Use this to verify that packets are being received and that the CRC16 engine isn't dropping data due to corruption.

**Example Output:**
```text
miniOS> netstat

  SFU Network Statistics
  ─────────────────────────────────────
  RX packets  : 142
  RX bytes    : 5680
  TX packets  : 142
  TX bytes    : 5680
  ─────────────────────────
  PINGs rx    : 10
  PONGs tx    : 10
  Infer reqs  : 120
  Infer resps : 120
  CMD reqs    : 12
  ─────────────────────────
  CRC errors  : 0
  Bad magic   : 0
  netlog      : OFF
```

#### `netlog [on|off]` — Packet Inspection
Enables real-time UART logging of every SFU packet. **WARNING:** This can be very verbose and may slow down the system during high-throughput inference; use only for debugging protocol-level issues.

#### `netlive` — Rolling Monitor
A real-time, "top-like" monitor that displays networking deltas every 500ms. Press **Enter** or **'q'** to exit back to the shell.
- **RX-pkt / TX-pkt**: Packets per 500ms window.
- **RX-B / TX-B**: Throughput in bytes.
- **INFER**: Number of inference requests serviced in the last window.

---

### 13.2 Host-Side: `minios_cli.py` API

The `minios_cli.py` tool (found in the root directory) is your primary interface for interacting with the SFU server from your development machine.

#### Basic Usage & Connection
```bash
# Connect to default (127.0.0.1:9000)
python3 minios_cli.py

# Connect to a remote hardware target
python3 minios_cli.py --host 192.168.1.50 --port 9000 --timeout 2.0
```

#### CLI Command Reference (Interactive Mode)

| Command | Usage | Description |
| :--- | :--- | :--- |
| `ping [n]` | `ping 10` | Sends `n` SFU PINGs and calculates avg/min/max RTT. |
| `status` | `status` | Queries the server for its active model and latency. |
| `models` | `models` | Queries MiniOS ULFS for a list of available `.onnx` files. |
| `use <name>` | `use tiny_mlp.onnx` | Tells the server to switch to a specific model. |
| `infer <vals>` | `infer 1.0 0.5 -1.2` | Sends input floats and returns the model output. |
| `bench [n]` | `bench 100` | Runs `n` inferences and calculates P99 latency statistics. |
| `watch` | `watch` | Continuous latency monitor (Ctrl+C to stop). |

#### One-Shot Execution (Scripting Mode)
You can run any command directly from your terminal shell without entering the interactive loop:
```bash
# Get the model list and exit
python3 minios_cli.py models

# Run a single benchmark
python3 minios_cli.py --timeout 5.0 bench 500
```

---

### 13.3 End-to-End Testing Procedure

To verify a fresh build of the networking stack, follow this standard procedure:

1.  **Launch MiniOS**: Run `make run` in one terminal window.
2.  **Check Driver**: Verify the boot log shows `[VNIC] Found VirtIO-Net at 0x...` and `[SFU ] init ok`.
3.  **Host Connectivity**: In a second terminal, run `python3 minios_cli.py ping`. You should see `reply from miniOS: time=~0.5ms`.
4.  **Model Listing**: Run `models` in the CLI. This verifies that **SFU <-> ULFS** integration is working.
5.  **Inference Test**: Run `infer 0.1 0.2 0.3 0.4`. Ensure the output matches the expected mathematical result of the `simple_add` model.
6.  **Load Test**: Run `bench 1000`. Switch back to the MiniOS window and run `netlive`. You should see a high packet frequency (e.g., 200+ pkt/sec).

---
