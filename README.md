# MiniOS-NetProtocol (Branch: `net-protocol`)

**9th branch of MiniOS — Internet Protocol Layer for ARM64 ML Inference**

---

## Why RUDP? The Protocol Decision

| Feature | Raw UDP | Full TCP | **RUDP (this branch)** |
|---------|---------|----------|------------------------|
| Delivery guarantee | ✗ | ✓ | **Selective (per command class)** |
| Connection state | None | Complex (3-way HS) | *x*Lightweight session** |
| Fragmentation | ✗ | ✓ (via stream) | **✓ (explicit, for ONNX models)** |
| OS socket API | Not on bare-metal | Not on bare-metal | **No OS needed** |
| Cooperative scheduler | — | — | **✓ poll-based, no blocking** |
| Integrity check | None | TCP checksum | **CRC-16/CCITT (stronger than UART CRC-8)** |
| Suitable for unikernel | Partial | ✗ | **✓** |

**Design rationale:**
- ML _control frames_ (load model, set input, run inference, get results) are **RELIABLE** — ACK/NACK with up to 5 retransmits.
- Telemetry/status frames are **BEST-EFFORT** — no ACK overhead.
- Large ONNX models are sent as **fragments** (stop-and-wait per fragment), no dynamic reassembly buffer needed.
- The cooperative scheduler integration means `RUDP_Receive()` and `RUDP_Poll()` are called from the main loop — no interrupts required.

---

## Branch Structure

```
MiniOS-net-protocol/
├── include/
│   └── net/
│       ├── net_types.h      # RUDP frame format, session state, constants
│       ├── rudp.h           # Public RUDP API
│       ├── eth_driver.h     # Ethernet HAL abstraction
│       └── crc16.h          # CRC-16/CCITT-FALSE header
├── src/
│   ├── net/
│   │   ├── rudp.c           # Core RUDP protocol implementation
│   │   └── crc16.c          # CRC-16 lookup table + compute/verify
│   ├── drivers/
│   │   └── eth_driver.c     # virtio-net (QEMU) + LAN9118 (bare-metal)
│   └── kernel/
│       └── main.c           # Cooperative main loop + ML command dispatch
├── tests/
│   └── test_rudp.c          # Host-testable suite (10 tests, no ARM64 needed)
├── scripts/
│   ├── run.sh               # QEMU launch with tap0 networking
│   └── rudp_client.py       # Python host-side RUDP client (scapy)
├── docs/
│   └── PROTOCOL.md          # Frame format reference
├── Makefile                 # ARM64 cross-compile + host test targets
└── linker.ld                # Memory layout (load @ 0x40080000)
```

---

## RUDP Frame Format

```
 0       1       2       3
+-------+-------+-------+-------+
| 0xAE  | FLAGS |  CMD  |  0x00 |   bytes 0-3
+-------+-------+-------+-------+
|         SEQUENCE NUMBER        |   bytes 4-7  (big-endian uint32)
+-------+-------+-------+-------+
|  PAYLOAD LENGTH (2 bytes)      |   bytes 8-9  (big-endian uint16)
+-------+-------+-------+-------+
|   ACK/NACK SEQUENCE (2 bytes)  |   bytes 10-11
+-------+-------+-------+-------+
|         PAYLOAD (0–1472 B)     |
+-------+-------+-------+-------+
|      CRC-16/CCITT (2 bytes)    |   covers everything above
+-------+-------+-------+-------+

Ethernet wrapper: [DST_MAC 6B][SRC_MAC 6B][EtherType 0x88B5 2B] + above
```

**FLAGS bitmask:**

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | `RELIABLE` | Receiver must send ACK |
| 1 | `ACK` | This is an ACK frame |
| 2 | `NACK` | Negative ACK — retransmit requested |
| 3 | `FRAG` | Fragment (more to follow) |
| 4 | `FRAG_END` | Last fragment |
| 5 | `KEEPALIVE` | Session keepalive ping |
| 6 | `RESET` | Session reset |

---

## Command Codes (aligned with SRS Appendix F / UART branch)

| Code | Name | Reliability | Description |
|------|------|-------------|-------------|
| `0x01` | `LOAD_MODEL` | Reliable | Send ONNX binary (fragmented if > 1472B) |
| `0x02` | `SET_INPUT` | Reliable | Input tensor data |
| `0x03` | `RUN_INFERENCE` | Reliable | Trigger graph execution |
| `0x04` | `GET_RESULTS` | Reliable | Request output tensors |
| `0x05` | `SYSTEM_STATUS` | Best-effort | Health / telemetry query |
| `0x06` | `CONFIG_UPDATE` | Reliable | Change runtime parameters |
| `0x10` | `KEEPALIVE` | Best-effort | Session liveness ping |
| `0x11` | `RESET` | Best-effort | Session reset |
| `0x12` | `FRAG_DATA` | Reliable | Fragment continuation frame |
| `0x13` | `BENCHMARK` | Best-effort | Trigger benchmark + print stats |

---

## Build & Run

### Prerequisites
```bash
sudo apt-get install gcc-aarch64-linux-gnu qemu-system-aarch64
pip install scapy   # for Python client
```

### Build (ARM64)
```bash
make                   # QEMU virtio-net
make ETH=lan9118       # SMSC LAN9118 bare-metal
```

### Run tests (no ARM64 required)
```bash
make test
```
Expected output:
```
RUDP Protocol Test Suite
========================
  crc16_known_vector                             PASSED
  crc16_verify_valid                             PASSED
  crc16_detects_corruption                       PASSED
  ...
10/10 tests passed.
```

### Run in QEMU
```bash
# On host: set up tap interface
sudo ip tuntap add dev tap0 mode tap user $(whoami)
sudo ip link set tap0 up

# Launch unikernel
make run
```

### Send commands from host
```bash
# Load an ONNX model
sudo python3 scripts/rudp_client.py --cmd load_model --file model.onnx

# Run inference
sudo python3 scripts/rudp_client.py --cmd run_inference

# Get results
sudo python3 scripts/rudp_client.py --cmd get_results

# Status
sudo python3 scripts/rudp_client.py --cmd status
```

---

## Integration with Other Branches

| Branch | Integration point |
|--------|-------------------|
| `MiniOS-BootLoader_and_HAL` | `HAL_UART_*` for debug output; shares `types.h`, `status.h` |
| `MiniOS-UART_Implementation` | Same command codes (0x01–0x06); RUDP replaces UART as transport |
| `MiniOS-feat-onnx` | `main.c` stubs call into ONNX loader once merged |
| `MiniOS-build` | Link `src/net/` into the ML build target |
| `MiniOS-unikernel` | `RUDP_Poll()` fits into cooperative scheduler loop |

---

## SRS Traceability

| Requirement | Status |
|-------------|--------|
| FR-021 Minimal UART interface for graph input | Extended to RUDP |
| FR-022 Output results | `RUDP_Send(NET_CMD_GET_RESULTS, ...)` |
| FR-023 System status reporting | `NET_CMD_SYSTEM_STATUS` best-effort |
| FR-024 Configuration update | `NET_CMD_CONFIG_UPDATE` reliable |
| FR-025 Validate all inputs | CRC-16 + magic byte + bounds check |
| SR-004 Communication integrity (CRC) | CRC-16/CCITT (vs CRC-8 in UART) |
| PR-005 Interrupt latency < 50µs | Poll-mode; no interrupt dependency |

---

## Authors

Group 12 — IIT Jodhpur  
Piyush Singh Bhati · Baviskar Darpan Chandrashekhar · Harshit · Aashma