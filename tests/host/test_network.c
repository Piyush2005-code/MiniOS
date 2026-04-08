#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "net/udp.h"
#include "net/ipv4.h"
#include "net/sfu.h"
#include "net/ethernet.h"
#include "net/arp.h"

extern void net_stub_reset(void);
extern uint16_t net_stub_last_ethertype(void);
extern uint16_t net_stub_last_payload_len(void);
extern const uint8_t *net_stub_last_payload(void);

static uint32_t g_rx_src_ip = 0;
static uint16_t g_rx_src_port = 0;
static uint16_t g_rx_len = 0;
static uint8_t g_rx_payload[64];
static int g_rx_called = 0;

static uint16_t host_to_be16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static uint32_t parse_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void udp_rx_probe(uint32_t src_ip, uint16_t src_port, uint8_t *payload, uint16_t len) {
    g_rx_called++;
    g_rx_src_ip = src_ip;
    g_rx_src_port = src_port;
    g_rx_len = len;
    if (len > sizeof(g_rx_payload)) {
        len = sizeof(g_rx_payload);
    }
    memcpy(g_rx_payload, payload, len);
}

void setUp(void) {
    g_rx_src_ip = 0;
    g_rx_src_port = 0;
    g_rx_len = 0;
    g_rx_called = 0;
    memset(g_rx_payload, 0, sizeof(g_rx_payload));
    net_stub_reset();
    UDP_Init();
    IPV4_Init();
}

void tearDown(void) {}

void test_CT_NET_001_udp_bind_capacity_enforced(void) {
    for (uint16_t i = 0; i < UDP_MAX_HANDLERS; i++) {
        TEST_ASSERT_EQUAL_INT(0, UDP_Bind((uint16_t)(8000 + i), udp_rx_probe));
    }
    TEST_ASSERT_EQUAL_INT(-1, UDP_Bind(9999, udp_rx_probe));
}

void test_CT_NET_002_udp_receive_dispatches_by_port(void) {
    uint8_t packet[8 + 5] = {0};
    UDPHdr_t *h = (UDPHdr_t *)packet;

    TEST_ASSERT_EQUAL_INT(0, UDP_Bind(9000, udp_rx_probe));

    h->src_port = host_to_be16(7777);
    h->dst_port = host_to_be16(9000);
    h->length = host_to_be16((uint16_t)sizeof(packet));
    h->checksum = 0;
    memcpy(packet + 8, "hello", 5);

    UDP_Receive(HOST_IP, packet, (uint16_t)sizeof(packet));

    TEST_ASSERT_EQUAL_INT(1, g_rx_called);
    TEST_ASSERT_EQUAL_HEX32(HOST_IP, g_rx_src_ip);
    TEST_ASSERT_EQUAL_UINT16(7777, g_rx_src_port);
    TEST_ASSERT_EQUAL_UINT16(5, g_rx_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", g_rx_payload, 5);
}

void test_IT_NET_003_udp_send_builds_ipv4_udp_frame(void) {
    uint8_t payload[3] = {1, 2, 3};

    TEST_ASSERT_EQUAL_INT(0, UDP_Send(HOST_IP, 9000, 4321, payload, 3));

    TEST_ASSERT_EQUAL_HEX16(ETH_TYPE_IPV4, net_stub_last_ethertype());
    TEST_ASSERT_TRUE(net_stub_last_payload_len() >= 28);

    const uint8_t *ip = net_stub_last_payload();
    TEST_ASSERT_EQUAL_HEX8(0x45, ip[0]);
    TEST_ASSERT_EQUAL_UINT8(IP_PROTO_UDP, ip[9]);

    const uint8_t *udp = ip + 20;
    uint16_t src_port = (uint16_t)((udp[0] << 8) | udp[1]);
    uint16_t dst_port = (uint16_t)((udp[2] << 8) | udp[3]);

    TEST_ASSERT_EQUAL_UINT16(4321, src_port);
    TEST_ASSERT_EQUAL_UINT16(9000, dst_port);
}

void test_CT_NET_004_sfu_serialize_deserialize_roundtrip(void) {
    sfu_header_t hdr = {0};
    uint8_t payload[4] = {0x10, 0x20, 0x30, 0x40};
    uint8_t out[SFU_MAX_PACKET];
    uint16_t out_len = 0;

    hdr.magic = SFU_MAGIC;
    hdr.version = SFU_VERSION;
    hdr.msg_type = SFU_MSG_CMD;
    hdr.flags = 0;
    hdr.req_id = 0xAABBCCDDu;
    hdr.seq_num = 0;
    hdr.total_seq = 1;
    hdr.payload_len = 4;

    TEST_ASSERT_EQUAL_INT(0, SFU_Serialize(&hdr, payload, out, &out_len));
    TEST_ASSERT_EQUAL_UINT16(SFU_HEADER_SIZE + 4, out_len);

    sfu_header_t parsed = {0};
    uint8_t *parsed_payload = NULL;
    uint16_t parsed_len = 0;

    TEST_ASSERT_EQUAL_INT(0, SFU_Deserialize(out, out_len, &parsed, &parsed_payload, &parsed_len));
    TEST_ASSERT_EQUAL_UINT8(SFU_MSG_CMD, parsed.msg_type);
    TEST_ASSERT_EQUAL_HEX32(0xAABBCCDDu, parsed.req_id);
    TEST_ASSERT_EQUAL_UINT16(4, parsed_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, parsed_payload, 4);
}

void test_IT_NET_005_sfu_ping_receive_triggers_pong_response(void) {
    sfu_header_t ping = {0};
    uint8_t wire[SFU_MAX_PACKET];
    uint16_t wire_len = 0;

    SFU_Init();

    ping.magic = SFU_MAGIC;
    ping.version = SFU_VERSION;
    ping.msg_type = SFU_MSG_PING;
    ping.flags = 0;
    ping.req_id = 0x12345678u;
    ping.seq_num = 0;
    ping.total_seq = 1;
    ping.payload_len = 0;

    TEST_ASSERT_EQUAL_INT(0, SFU_Serialize(&ping, NULL, wire, &wire_len));

    net_stub_reset();
    SFU_OnReceive(HOST_IP, SFU_PORT, wire, wire_len);

    TEST_ASSERT_EQUAL_HEX16(ETH_TYPE_IPV4, net_stub_last_ethertype());
    TEST_ASSERT_TRUE(net_stub_last_payload_len() >= 28 + SFU_HEADER_SIZE);

    const uint8_t *ip = net_stub_last_payload();
    const uint8_t *udp = ip + 20;
    const uint8_t *sfu = udp + 8;

    TEST_ASSERT_EQUAL_UINT8(SFU_MSG_PONG, sfu[5]);

    uint32_t req_id = (uint32_t)sfu[8] |
                      ((uint32_t)sfu[9] << 8) |
                      ((uint32_t)sfu[10] << 16) |
                      ((uint32_t)sfu[11] << 24);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, req_id);
}

void test_UT_NET_006_udp_receive_short_packet_is_ignored(void) {
    uint8_t short_buf[7] = {0};

    TEST_ASSERT_EQUAL_INT(0, UDP_Bind(9100, udp_rx_probe));
    UDP_Receive(HOST_IP, short_buf, (uint16_t)sizeof(short_buf));

    TEST_ASSERT_EQUAL_INT(0, g_rx_called);
}

void test_UT_NET_007_udp_send_payload_limit_boundary(void) {
    static uint8_t max_ok[1472];
    static uint8_t too_large[1473];

    memset(max_ok, 0x5A, sizeof(max_ok));
    memset(too_large, 0xA5, sizeof(too_large));

    net_stub_reset();
    TEST_ASSERT_EQUAL_INT(0, UDP_Send(HOST_IP, 9999, 8888, max_ok, (uint16_t)sizeof(max_ok)));
    TEST_ASSERT_EQUAL_UINT16(1500, net_stub_last_payload_len());

    net_stub_reset();
    TEST_ASSERT_EQUAL_INT(-1, UDP_Send(HOST_IP, 9999, 8888, too_large, (uint16_t)sizeof(too_large)));
    TEST_ASSERT_EQUAL_UINT16(0, net_stub_last_payload_len());
}

void test_UT_NET_008_sfu_deserialize_truncated_payload_rejected(void) {
    sfu_header_t hdr = {0};
    uint8_t payload[4] = {1, 2, 3, 4};
    uint8_t wire[SFU_MAX_PACKET];
    uint16_t wire_len = 0;
    sfu_header_t parsed = {0};
    uint8_t *parsed_payload = NULL;
    uint16_t parsed_len = 0;

    hdr.magic = SFU_MAGIC;
    hdr.version = SFU_VERSION;
    hdr.msg_type = SFU_MSG_CMD;
    hdr.req_id = 0xABCDEF01u;
    hdr.total_seq = 1;
    hdr.payload_len = 4;

    TEST_ASSERT_EQUAL_INT(0, SFU_Serialize(&hdr, payload, wire, &wire_len));
    TEST_ASSERT_EQUAL_INT(-1,
        SFU_Deserialize(wire, (uint16_t)(SFU_HEADER_SIZE + 2), &parsed, &parsed_payload, &parsed_len));
}

void test_UT_NET_009_sfu_onreceive_checksum_mismatch_sends_nack(void) {
    sfu_header_t hdr = {0};
    uint8_t payload[3] = {0x11, 0x22, 0x33};
    uint8_t wire[SFU_MAX_PACKET];
    uint16_t wire_len = 0;

    SFU_Init();

    hdr.magic = SFU_MAGIC;
    hdr.version = SFU_VERSION;
    hdr.msg_type = SFU_MSG_CMD;
    hdr.req_id = 0x11223344u;
    hdr.total_seq = 1;
    hdr.payload_len = 3;

    TEST_ASSERT_EQUAL_INT(0, SFU_Serialize(&hdr, payload, wire, &wire_len));
    wire[SFU_HEADER_SIZE] ^= 0xFFu;

    net_stub_reset();
    SFU_OnReceive(HOST_IP, SFU_PORT, wire, wire_len);

    TEST_ASSERT_EQUAL_HEX16(ETH_TYPE_IPV4, net_stub_last_ethertype());
    TEST_ASSERT_TRUE(net_stub_last_payload_len() >= 28 + SFU_HEADER_SIZE);

    const uint8_t *ip = net_stub_last_payload();
    const uint8_t *udp = ip + 20;
    const uint8_t *sfu = udp + 8;

    TEST_ASSERT_EQUAL_UINT8(SFU_MSG_NACK, sfu[5]);
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, parse_le32(&sfu[8]));
}

void test_UT_NET_010_udp_bind_rejects_null_handler(void) {
    TEST_ASSERT_EQUAL_INT(-1, UDP_Bind(9200, NULL));
}

void test_UT_NET_011_udp_receive_rejects_header_length_smaller_than_header(void) {
    uint8_t packet[8 + 2] = {0};
    UDPHdr_t *h = (UDPHdr_t *)packet;

    TEST_ASSERT_EQUAL_INT(0, UDP_Bind(9300, udp_rx_probe));

    h->src_port = host_to_be16(1111);
    h->dst_port = host_to_be16(9300);
    h->length = host_to_be16(4);
    h->checksum = 0;
    packet[8] = 0xAA;
    packet[9] = 0xBB;

    UDP_Receive(HOST_IP, packet, (uint16_t)sizeof(packet));

    TEST_ASSERT_EQUAL_INT(0, g_rx_called);
}

void test_UT_NET_012_udp_receive_rejects_declared_length_bigger_than_frame(void) {
    uint8_t packet[8 + 2] = {0};
    UDPHdr_t *h = (UDPHdr_t *)packet;

    TEST_ASSERT_EQUAL_INT(0, UDP_Bind(9400, udp_rx_probe));

    h->src_port = host_to_be16(2222);
    h->dst_port = host_to_be16(9400);
    h->length = host_to_be16(64);
    h->checksum = 0;
    packet[8] = 0xCC;
    packet[9] = 0xDD;

    UDP_Receive(HOST_IP, packet, (uint16_t)sizeof(packet));

    TEST_ASSERT_EQUAL_INT(0, g_rx_called);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_CT_NET_001_udp_bind_capacity_enforced);
    RUN_TEST(test_CT_NET_002_udp_receive_dispatches_by_port);
    RUN_TEST(test_IT_NET_003_udp_send_builds_ipv4_udp_frame);
    RUN_TEST(test_CT_NET_004_sfu_serialize_deserialize_roundtrip);
    RUN_TEST(test_IT_NET_005_sfu_ping_receive_triggers_pong_response);
    RUN_TEST(test_UT_NET_006_udp_receive_short_packet_is_ignored);
    RUN_TEST(test_UT_NET_007_udp_send_payload_limit_boundary);
    RUN_TEST(test_UT_NET_008_sfu_deserialize_truncated_payload_rejected);
    RUN_TEST(test_UT_NET_009_sfu_onreceive_checksum_mismatch_sends_nack);
    RUN_TEST(test_UT_NET_010_udp_bind_rejects_null_handler);
    RUN_TEST(test_UT_NET_011_udp_receive_rejects_header_length_smaller_than_header);
    RUN_TEST(test_UT_NET_012_udp_receive_rejects_declared_length_bigger_than_frame);
    return UNITY_END();
}
