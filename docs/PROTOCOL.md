# RUDP Protocol Reference — MiniOS-NetProtocol

## 1. Design Goals

MiniOS Reliable UDP (RUDP) is a custom transport protocol designed to sit
directly over raw Ethernet (no IP/UDP layer) for bare-metal ARM64 operation.

Goals:
- **No OS socket API** — runs on bare-metal unikernel
- **Selective reliability** — ACK only for ML control frames
- **Cooperative scheduler compatible** — non-blocking poll API
- **CRC-16 integrity** — stronger than UART CRC-8
- **Fragmentation** — for ONNX models > 1 Ethernet frame

## 2. Wire Format

### 2.1 Ethernet Frame
```
[DST_MAC 6B][SRC_MAC 6B][EtherType 0x88B5 2B][RUDP Frame]
```
EtherType `0x88B5` is in the locally administered range (no IANA registration needed).

### 2.2 RUDP Header (12 bytes)
```
Offset  Size  Field
0       1     Magic (0xAE)
1       1     Flags (bitmask)
2       1     Command (NetCommand enum)
3       1     Reserved (0x00)
4       4     Sequence Number (big-endian uint32)
8       2     Payload Length (big-endian uint16, 0–1472)
10      2     ACK/NACK Sequence (big-endian uint16)
```

### 2.3 CRC-16
2 bytes (big-endian) appended after payload.
Polynomial: CRC-16/CCITT-FALSE (0x1021, init=0xFFFF).
Covers: Ethernet header + RUDP header + payload.

## 3. Reliability Protocol

```
RELIABLE send:
  Sender ──[DATA, seq=N, FLAG_RELIABLE]──▶ Receiver
  Sender ◀──[ACK, ack_seq=N]──────────── Receiver

  If no ACK within RUDP_RETRY_TIMEOUT_MS (200ms):
    Retransmit, retry_count++
    After RUDP_MAX_RETRIES (5): session dead

NACK (out-of-order):
  Receiver ──[NACK, ack_seq=expected]──▶ Sender
  Sender retransmits expected sequence immediately

BEST-EFFORT:
  Sender ──[DATA, seq=N, no FLAG_RELIABLE]──▶ Receiver
  No ACK sent or expected
```

## 4. Fragmentation

Used for ONNX models larger than `RUDP_MAX_PAYLOAD` (1472 bytes).

```
Fragment 0: CMD=LOAD_MODEL, FLAGS=FRAG,     seq=N,   ack_seq=0
Fragment 1: CMD=FRAG_DATA,  FLAGS=FRAG,     seq=N+1, ack_seq=1
...
Fragment k: CMD=FRAG_DATA,  FLAGS=FRAG_END, seq=N+k, ack_seq=k
```

Each fragment is individually ACKed (stop-and-wait per fragment).
The `ack_seq` field carries the fragment index for reassembly ordering.

## 5. Session Lifecycle

```
Client                              Server (ARM64 unikernel)
  |                                     |
  |──[KEEPALIVE, FLAG_RELIABLE, seq=1]─▶|
  |◀─[ACK, ack_seq=1]─────────────────|   Session open
  |                                     |
  |──[LOAD_MODEL, seq=2, ...]──────────▶|
  |◀─[ACK, ack_seq=2]─────────────────|
  |                                     |
  |──[RUN_INFERENCE, seq=3]────────────▶|
  |◀─[ACK, ack_seq=3]─────────────────|   Inference executes
  |                                     |
  |──[GET_RESULTS, seq=4]──────────────▶|
  |◀─[ACK + result payload, seq=5]────|
  |                                     |
  |──[RESET, FLAG_RESET]───────────────▶|   Session closed
```

## 6. Configuration

All compile-time via Makefile / `-D` flags:

| Macro | Default | Description |
|-------|---------|-------------|
| `RUDP_WINDOW_SIZE` | 8 | Unacked frames in flight |
| `RUDP_MAX_RETRIES` | 5 | Max retransmit attempts |
| `RUDP_RETRY_TIMEOUT_MS` | 200 | Retransmit timeout (ms) |
| `RUDP_KEEPALIVE_MS` | 5000 | Keepalive interval (ms) |
| `RUDP_SESSION_TIMEOUT_MS` | 15000 | Dead session threshold (ms) |
| `ETH_DRIVER_VIRTIO` | set | Use QEMU virtio-net |
| `ETH_DRIVER_LAN9118` | unset | Use SMSC LAN9118 |
| `NET_LOCAL_MAC` | `52:54:00:12:34:56` | Unikernel MAC |
| `NET_PEER_MAC` | `52:54:00:12:34:57` | Client MAC |

## 7. Comparison with UART Protocol (SRS Appendix F)

| Property | UART (Branch 3) | RUDP (Branch 9) |
|----------|----------------|-----------------|
| Transport | Serial (115200 baud) | Ethernet (100Mbps+) |
| Frame overhead | 4 bytes | 26 bytes (ETH+RUDP+CRC) |
| Max payload | 254 bytes | 1472 bytes |
| Integrity | CRC-8 XOR | CRC-16/CCITT |
| Reliability | None | Selective ACK |
| Fragmentation | None | Yes (for large models) |
| Session | None | Lightweight |
| Command codes | Identical (0x01–0x06) | Identical + extended |