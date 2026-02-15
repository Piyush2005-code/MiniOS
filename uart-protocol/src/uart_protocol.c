#include "uart_protocol.h"
#include <string.h>

uint8_t uart_calculate_crc(const uint8_t* data, size_t len)
{
    uint8_t crc = UART_CRC_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}

int uart_pack_message(uint8_t cmd, const uint8_t* payload, uint8_t payload_len,
                      uint8_t* buffer, size_t buffer_size)
{
    /* Validate parameters */
    if (payload_len > UART_MAX_PAYLOAD - 1) {   /* 1 byte reserved for command */
        return -1;
    }
    size_t total_len = 1 + 1 + 1 + payload_len + 1; /* START + LEN + CMD + PAYLOAD + CRC */
    if (buffer_size < total_len) {
        return -1;
    }
    if (cmd < CMD_LOAD_MODEL || cmd > CMD_CONFIG_UPDATE) {
        return -1;
    }

    uint8_t* p = buffer;

    /* START byte */
    *p++ = UART_START_BYTE;

    /* LENGTH field (command + payload) */
    uint8_t len_field = 1 + payload_len;
    *p++ = len_field;

    /* COMMAND */
    *p++ = cmd;

    /* PAYLOAD (if any) */
    if (payload && payload_len > 0) {
        memcpy(p, payload, payload_len);
        p += payload_len;
    }

    /* CRC over START, LEN, CMD, PAYLOAD */
    uint8_t crc = uart_calculate_crc(buffer, p - buffer);
    *p++ = crc;

    return (int)total_len;
}

int uart_unpack_message(const uint8_t* buffer, size_t buffer_len,
                        uint8_t* cmd, uint8_t* payload, uint8_t* payload_len)
{
    /* Minimum message: START + LEN + CMD + CRC (no payload) = 4 bytes */
    if (buffer_len < 4) {
        return -1;
    }

    /* Check START byte */
    if (buffer[0] != UART_START_BYTE) {
        return -1;
    }

    uint8_t len_field = buffer[1];   /* length of (CMD + PAYLOAD) */
    if (len_field < 1) {              /* at least command byte must be present */
        return -1;
    }
    uint8_t payload_len_from_field = len_field - 1;

    /* Total message length: START(1) + LEN(1) + CMD(1) + PAYLOAD(n) + CRC(1) */
    size_t msg_len = 3 + payload_len_from_field + 1; /* 1+1+1 + payload + 1 */
    if (buffer_len < msg_len) {
        return -1;   /* incomplete message */
    }

    /* Verify CRC */
    uint8_t expected_crc = uart_calculate_crc(buffer, msg_len - 1);
    if (expected_crc != buffer[msg_len - 1]) {
        return -1;
    }

    /* Extract fields */
    *cmd = buffer[2];
    if (payload_len_from_field > 0) {
        memcpy(payload, &buffer[3], payload_len_from_field);
    }
    *payload_len = payload_len_from_field;

    return (int)msg_len;
}