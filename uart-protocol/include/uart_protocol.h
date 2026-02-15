#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol constants */
#define UART_START_BYTE     0xAA
#define UART_MAX_PAYLOAD    254   /* because length field is 1 byte (0-255) and includes command */
#define UART_CRC_INIT       0x00

/* Command codes (from Appendix F) */
#define CMD_LOAD_MODEL      0x01
#define CMD_SET_INPUT       0x02
#define CMD_RUN_INFERENCE   0x03
#define CMD_GET_RESULTS     0x04
#define CMD_SYSTEM_STATUS   0x05
#define CMD_CONFIG_UPDATE   0x06

/**
 * Calculate 8‑bit XOR checksum over a data buffer.
 * @param data  Pointer to data bytes
 * @param len   Number of bytes
 * @return      XOR checksum
 */
uint8_t uart_calculate_crc(const uint8_t* data, size_t len);

/**
 * Pack a message into a buffer according to the protocol.
 * Format: [START][LEN][CMD][PAYLOAD...][CRC]
 *   START = UART_START_BYTE
 *   LEN   = 1 + payload_len  (command byte + payload)
 *   CMD   = command code
 *   PAYLOAD = payload bytes
 *   CRC   = XOR of START, LEN, CMD, and PAYLOAD
 *
 * @param cmd          Command code (0x01..0x06)
 * @param payload      Pointer to payload bytes (may be NULL if payload_len==0)
 * @param payload_len  Number of payload bytes (0 .. UART_MAX_PAYLOAD-1)
 * @param buffer       Output buffer (must be at least payload_len + 4 bytes)
 * @param buffer_size  Size of output buffer
 * @return             Total message length on success, -1 on error
 */
int uart_pack_message(uint8_t cmd, const uint8_t* payload, uint8_t payload_len,
                      uint8_t* buffer, size_t buffer_size);

/**
 * Unpack a received message and validate its CRC.
 * Expects a buffer that starts with a valid START byte.
 * On success, extracts command, payload, and payload length.
 *
 * @param buffer       Input buffer containing the message
 * @param buffer_len   Number of bytes available in buffer
 * @param cmd          Output: command code
 * @param payload      Output buffer for payload (must be at least UART_MAX_PAYLOAD bytes)
 * @param payload_len  Output: number of payload bytes
 * @return             Total message length (including start and CRC) on success,
 *                     -1 if invalid or CRC mismatch
 */
int uart_unpack_message(const uint8_t* buffer, size_t buffer_len,
                        uint8_t* cmd, uint8_t* payload, uint8_t* payload_len);

#ifdef __cplusplus
}
#endif

#endif /* UART_PROTOCOL_H */