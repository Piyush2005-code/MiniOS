#include <stdint.h>
#include <string.h>

#include "net/ethernet.h"

#define NET_CAPTURE_MAX 1600

static uint8_t g_last_eth_payload[NET_CAPTURE_MAX];
static uint16_t g_last_eth_payload_len = 0;
static uint16_t g_last_ethertype = 0;

void net_stub_reset(void) {
    g_last_eth_payload_len = 0;
    g_last_ethertype = 0;
    memset(g_last_eth_payload, 0, sizeof(g_last_eth_payload));
}

uint16_t net_stub_last_ethertype(void) {
    return g_last_ethertype;
}

uint16_t net_stub_last_payload_len(void) {
    return g_last_eth_payload_len;
}

const uint8_t *net_stub_last_payload(void) {
    return g_last_eth_payload;
}

int ETH_Send(uint8_t dst_mac[6], uint16_t ethertype, uint8_t *payload, uint16_t payload_len) {
    (void)dst_mac;
    g_last_ethertype = ethertype;
    g_last_eth_payload_len = payload_len;

    if (payload_len > NET_CAPTURE_MAX) {
        payload_len = NET_CAPTURE_MAX;
        g_last_eth_payload_len = NET_CAPTURE_MAX;
    }

    if (payload != NULL && payload_len > 0) {
        memcpy(g_last_eth_payload, payload, payload_len);
    }
    return 0;
}

void ARP_GetHostMAC(uint8_t mac[6]) {
    static const uint8_t host[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    memcpy(mac, host, 6);
}

void VNIC_Poll(void) {
    /* no-op in host tests */
}
