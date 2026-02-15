#include "uart_protocol.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Simple test harness */
static void test_pack_unpack(void)
{
    uint8_t buffer[256];
    uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t cmd = CMD_SET_INPUT;
    uint8_t payload_len = sizeof(payload);

    /* Pack message */
    int packed_len = uart_pack_message(cmd, payload, payload_len,
                                        buffer, sizeof(buffer));
    assert(packed_len == 1 + 1 + 1 + payload_len + 1); /* START + LEN + CMD + PAYLOAD + CRC */

    /* Unpack message */
    uint8_t out_cmd;
    uint8_t out_payload[UART_MAX_PAYLOAD];
    uint8_t out_len;
    int unpacked_len = uart_unpack_message(buffer, packed_len,
                                            &out_cmd, out_payload, &out_len);
    assert(unpacked_len == packed_len);
    assert(out_cmd == cmd);
    assert(out_len == payload_len);
    assert(memcmp(out_payload, payload, payload_len) == 0);

    printf("pack/unpack test passed\n");
}

static void test_crc_error(void)
{
    uint8_t buffer[256];
    uint8_t payload[] = {0xAA, 0xBB};
    uint8_t cmd = CMD_LOAD_MODEL;

    int len = uart_pack_message(cmd, payload, sizeof(payload),
                                 buffer, sizeof(buffer));
    assert(len > 0);

    /* Corrupt a byte in the payload */
    buffer[3] ^= 0xFF;   /* flip bits in first payload byte */

    uint8_t out_cmd, out_payload[UART_MAX_PAYLOAD], out_len;
    int result = uart_unpack_message(buffer, len, &out_cmd, out_payload, &out_len);
    assert(result == -1);   /* CRC should fail */

    printf("CRC error detection test passed\n");
}

static void test_incomplete_message(void)
{
    uint8_t buffer[] = {UART_START_BYTE, 0x02, CMD_SYSTEM_STATUS}; /* missing CRC */
    uint8_t out_cmd, out_payload[UART_MAX_PAYLOAD], out_len;
    int result = uart_unpack_message(buffer, sizeof(buffer),
                                      &out_cmd, out_payload, &out_len);
    assert(result == -1);

    printf("incomplete message test passed\n");
}

static void test_max_payload(void)
{
    /* Use a buffer large enough for the maximum possible message.
     * Maximum payload = UART_MAX_PAYLOAD (254), total message length = 258.
     * Allocate 512 bytes to be safe.
     */
    uint8_t buffer[512];
    uint8_t payload[UART_MAX_PAYLOAD - 1]; /* maximum allowed payload */
    for (int i = 0; i < (int)sizeof(payload); i++) {
        payload[i] = (uint8_t)i;
    }
    int len = uart_pack_message(CMD_CONFIG_UPDATE, payload, sizeof(payload),
                                 buffer, sizeof(buffer));
    /* Expected length: START(1) + LEN(1) + CMD(1) + payload_len + CRC(1) */
    int expected_len = 1 + 1 + 1 + sizeof(payload) + 1;
    assert(len == expected_len);

    uint8_t out_cmd, out_payload[UART_MAX_PAYLOAD], out_len;
    int unpacked = uart_unpack_message(buffer, len,
                                        &out_cmd, out_payload, &out_len);
    assert(unpacked == len);
    assert(out_len == sizeof(payload));
    assert(memcmp(out_payload, payload, sizeof(payload)) == 0);

    printf("max payload test passed\n");
}

int main(void)
{
    printf("UART Protocol Test Suite\n");
    printf("\n\n");

    test_pack_unpack();
    test_crc_error();
    test_incomplete_message();
    test_max_payload();

    printf("\nAll tests passed.\n");
    return 0;
}