# UART Communication Protocol for ML Inference Unikernel

This repository contains a portable C implementation of the UART protocol defined in the **Software Requirements Specification (SRS) Version 2.0**, Appendix F. The protocol is designed for communication between a host and the ARM64 bare‑metal unikernel.

## Protocol Format

Each message has the following structure (all fields are 8‑bit bytes):

| START (0xAA) | LENGTH | COMMAND | PAYLOAD (0–254 bytes) | CRC |
|--------------|--------|---------|------------------------|-----|

- **START**: Fixed byte `0xAA` to mark the beginning of a message.
- **LENGTH**: Number of bytes in the `COMMAND` + `PAYLOAD` fields (1 to 255).  
  Since `COMMAND` occupies 1 byte, `PAYLOAD` can be at most 254 bytes.
- **COMMAND**: One of the predefined command codes (see `uart_protocol.h`).
- **PAYLOAD**: Optional data bytes (omitted if LENGTH == 1).
- **CRC**: 8‑bit XOR checksum over all preceding bytes (START, LENGTH, COMMAND, PAYLOAD).

## Command Codes (from SRS)

| Code | Command           | Description                 |
|------|-------------------|-----------------------------|
| 0x01 | CMD_LOAD_MODEL    | Load ONNX model             |
| 0x02 | CMD_SET_INPUT     | Set input tensor            |
| 0x03 | CMD_RUN_INFERENCE | Start inference             |
| 0x04 | CMD_GET_RESULTS   | Retrieve results            |
| 0x05 | CMD_SYSTEM_STATUS | Query system health         |
| 0x06 | CMD_CONFIG_UPDATE | Update configuration        |

## Building and Testing

### Prerequisites

- A C compiler (gcc or clang)
- `make`

### Compilation

Clone the repository and run:

```bash
cd uart-protocol
make