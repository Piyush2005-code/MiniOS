# MiniOS SFU (Simple Framed UDP) Networking Protocol — Deep Technical Documentation

> **Audience:** Network engineers, systems programmers, and OS researchers working with MiniOS RPC frameworks.

---

## Table of Contents

1. [What is SFU?](#1-what-is-sfu)
2. [Why Not TCP?](#2-why-not-tcp)
3. [Binary Wire Format](#3-binary-wire-format)
4. [Reliability and the ARQ Pipeline](#4-reliability-and-the-arq-pipeline)
5. [The Packet Lifecycle (RX/TX Pass)](#5-the-packet-lifecycle-rxtx-pass)
6. [Message Types and Dispatching](#6-message-types-and-dispatching)
7. [Inference Server Integration (`infer_server.c`)](#7-inference-server-integration-infer_serverc)
8. [Memory Management constraints](#8-memory-management-constraints)

---

## 1. What is SFU?

**Simple Framed UDP (SFU)** is a custom Layer-7 Remote Procedure Call (RPC) protocol built specifically for MiniOS. It provides a lightweight, authenticated, and reliable message framing mechanism on top of standard raw UDP (User Datagram Protocol). 

It is designed with a single goal: **allow a host machine to securely request hardware-accelerated ONNX inference from a bare-metal MiniOS node with zero protocol overhead, zero dynamic memory allocations, and high reliability.**

Everything is routed specifically over **UDP Port 9000**.

---

## 2. Why Not TCP?

A full TCP state machine (handling SYN/ACKs, sliding windows, out-of-order reassembly buffers, congestion control, and connection teardowns) takes tens of thousands of lines of code and extensive dynamic memory buffers (e.g. `sk_buff` queues). 

MiniOS restricts memory rigorously. SFU achieves the guarantees we actually need (reliable transmission of ML inference parameters) without the overhead:
* **Connectionless but Reliable:** A Stop-and-Wait/ARQ (Automatic Repeat reQuest) system tracks request IDs.
* **Deterministic Memory:** Max transmission size is restricted exactly to fit in one Ethernet MTU (1500 bytes).
* **Predictable Latency:** No slow-start algorithms; packets hit the bare-metal VirtIO network controller directly.

---

## 3. Binary Wire Format

Every packet exchanged between the host and MiniOS is enveloped by the exact same **24-byte static header**.

```
 ════  SFU Packet layout (sits inside the UDP payload)  ════
 
  Offset  Size  Field
  ──────  ────  ─────────────────────────────────────────────
   0      4     magic        (0xDEAD6969 — rejects everything else)
   4      1     version      (0x01)
   5      1     msg_type     (SFU_MSG_* tag)
   6      2     flags        (SFU_FLAG_* bitmask)
   8      4     req_id       (unique request ID, echoed in replies)
  12      4     seq_num      (fragment index, 0-based for ML models)
  16      4     total_seq    (total fragment count)
  20      2     checksum     (CRC16-CCITT of payload bytes only)
  22      2     payload_len  (byte count immediately following header)
  24      N     payload      (tensor data, commands, results...)
```

### 3.1 Constraints and Compile-Time Safety
The protocol leverages C's static analysis to prevent compilation if padding accidentally ruins the structure:
```c
typedef struct __attribute__((packed)) { ... } sfu_header_t;
_Static_assert(sizeof(sfu_header_t) == 24u, "sfu_header_t must be 24 bytes");
```
All multi-byte integer fields are mapped to **Little-Endian** byte order locally since MiniOS runs on ARM64 inside QEMU/RPi4 in Little-Endian mode.

### 3.2 Maximum Constraints
* **IP MTU:** 1500 bytes
* **SFU Packet Limit:** `1472 bytes` (MTU - 20 bytes IP header - 8 bytes UDP header)
* **Maximum ML Payload:** `1448 bytes` per packet (1472 bytes max - 24 bytes SFU Header)

---

## 4. Reliability and the ARQ Pipeline

Because UDP throws packets into the void, SFU implements its own **Reliability Layer** inside `SFU_SendReliable`.

### 4.1 In-Flight Table (`sfu_inflight`)
When the OS sends a "reliable" packet (like an Inference result, or a Command reply), it reserves one of `SFU_MAX_INFLIGHT` (total: 8) static slots.

```c
typedef struct {
    uint8_t  in_use;
    uint32_t req_id;         // The sequence ID expected to be ACKed
    uint64_t sent_at_ms;     // Hardware timer timestamp (CNTPCT_EL0)
    uint8_t  retries;        // Retransmission attempt count
    uint8_t  buf[1472];      // Fully backed copy of the wire packet
} sfu_inflight_t;
```

### 4.2 The Tick System and Retransmission
Host and guest environments drop packets frequently. 
To counteract this, the `SFU_Tick()` function is invoked by the OS scheduler continuously:
1. It compares `HAL_Timer_GetSystemTicks()` with `sent_at_ms`.
2. If `SFU_TIMEOUT_MS` (200 milliseconds) has elapsed with no ACK received, **the packet is fired out the network adapter again**.
3. It gives up after `SFU_MAX_RETRIES` (5 times, meaning 1 full second) and invokes the timeout handler.

---

## 5. The Packet Lifecycle (RX/TX Pass)

### 5.1 Receiving Data
1. `virtio_net.c` generates an interrupt, indicating an Ethernet frame.
2. `ethernet.c` verifies the MAC address and strips the frame, passing it to `ipv4.c`.
3. `ipv4.c` checks IP checksums. If protocol is `17` (UDP), passes it to `udp.c`.
4. `udp.c` checks if destination port is `9000`. If so, passes it directly into the SFU layer via `SFU_OnReceive`.
5. `SFU_Deserialize` extracts the datagram:
   - Evaluates `0xDEAD6969` magic string to prevent junk packets.
   - Evaluates CRC16-CCITT algorithm directly on the payload memory slice.
   - Triggers `SFU_SendNack` (Negative ACK) if corrupted.

### 5.2 Transmitting Data
1. Request calls `SFU_Serialize()`.
2. The layer calculates a CRC16 of the payload string/tensors in O(len) byte-by-byte calculation.
3. Because we don't have `malloc()`, we copy headers and payloads linearly into a statically allocated contiguous 1472-byte buffer (`sfu_tx_buf`).
4. We push into `UDP_Send()`, traversing down through `ipv4.c` -> `mac.c` -> `virtio_net.c` to the host.

---

## 6. Message Types and Dispatching

When a verified packet passes the SFU deserializer, the system dispatches behaviour based on `SFU_MSG_*` enum.

| Opcode | Name | Protocol Action |
|--------|------|-----------------|
| `0x01` | `SFU_MSG_INFER_REQUEST` | Hands the payload vector list directly to ML backend (`infer_server.c`), instantly shoots an `ACK` ping back to the host. |
| `0x02` | `SFU_MSG_INFER_RESPONSE`| Contains raw floating-point ML prediction arrays. |
| `0x03` | `SFU_MSG_ACK` | Drops the specified `req_id` from the active retry table. |
| `0x04` | `SFU_MSG_NACK` | Invalidates current packet immediately; requests a re-submit. |
| `0x05` | `SFU_MSG_PING` | Internal network tester routing protocol. Will instantly trigger a `PONG` callback. |
| `0x07` | `SFU_MSG_CMD` | Sends ASCII strings requesting file-system listings (e.g. "LIST_MODELS"). |

---

## 7. Inference Server Integration (`infer_server.c`)

SFU acts as the gateway to the `ONNX_Runtime`.

The module `src/net/infer_server.c` hooks onto SFU via two central callbacks:
1. **`SFU_SetInferHandler(INFER_OnRequest)`** 
2. **`SFU_SetCmdHandler(INFER_OnCmd)`**

### Command Dispatching (`INFER_OnCmd`)
If the payload is a `SFU_MSG_CMD`, `infer_server` reads ASCII text:
* Requesting `LIST_MODELS`? Opens the backend ULFS (MiniOS Flash Storage) filesystem via `ULFS_Readdir` and reads through `/storage` for any file ending in `.onnx`.
* Requesting `SELECT_MODEL {name}`? Strips the extension, clears the Arena Allocator memory, uses the ONNX ProtoReader to parse the weights, and builds an entirely new `ONNX_Graph` in bare-metal RAM.

### Tensor Request (`INFER_OnRequest`)
When an `INFER_REQUEST` comes in, containing raw Little-Endian bytes, `infer_server`:
1. Directly casts the string memory arrays over to a floating vector `float infer_input_buf[]`. 
2. Invokes `ONNX_Runtime_InferenceSimple()`. 
3. After evaluating the matrix dot-products/activations in the backend, it dumps the memory back into an SFU payload.
4. It calls `SFU_SendRaw()` with Opcode `SFU_MSG_INFER_RESPONSE` to return the ML predictions back out of the RPi/QEMU kernel directly to the Python client terminal.

---

## 8. Memory Management Constraints

Like the rest of MiniOS, **SFU explicitly outlaws the use of dynamic HEAP memory**.

* **Staging Area (`sfu_tx_buf`)**: There is only 1 unified buffer globally instanced for outgoing transmission to avoid fragmentation and system crash:
  ```c
  static uint8_t sfu_tx_buf[SFU_MAX_PACKET];
  ```
* **No `memcpy()` overhead**: MiniOS completely bypasses standard `libc` routines for security predictability. Copying memory from payload block to wire buffer is done locally through `sfu_memcpy()` byte-by-iteration matching.
* **Bounded Models**: Max acceptable network tensor array allowed in to limit OOM crashes is capped statically stringently at `INFER_MAX_INPUT_FLOATS 4096 = 16.3KB` footprint. 
