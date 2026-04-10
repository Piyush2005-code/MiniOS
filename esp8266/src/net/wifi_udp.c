/**
 * @file wifi_udp.c
 * @brief UDP Abstraction Layer for MiniOS-ESP8266
 *
 * Implements the standard UDP_Init/Bind/Send API over ESP8266's espconn
 * UDP interface. The espconn RX callback dispatches received datagrams
 * to port-registered handlers (same dispatch model as the original udp.c).
 *
 * All SFU traffic arrives on port 9000.
 * Supports up to UDP_MAX_HANDLERS simultaneously bound ports.
 */

#include "net/udp.h"
#include "hal/uart.h"
#include "hal/wifi.h"
#include "types.h"

/* ESP8266 NonOS SDK */
#include "ets_sys.h"
#include "osapi.h"
#include "espconn.h"
#include "mem.h"

/* ------------------------------------------------------------------ */
/*  Port binding table                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t      port;
    udp_handler_t handler;
    uint8_t       in_use;
} udp_binding_t;

static udp_binding_t g_bindings[UDP_MAX_HANDLERS];

/* ------------------------------------------------------------------ */
/*  espconn state for each bound port                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    struct espconn  conn;
    esp_udp         udp_cfg;
    uint8_t         active;
    uint16_t        port;
} udp_slot_t;

static udp_slot_t g_slots[UDP_MAX_HANDLERS];

/* ------------------------------------------------------------------ */
/*  Static TX buffer                                                  */
/* ------------------------------------------------------------------ */

static uint8_t g_udp_tx_buf[1472]; /* max UDP payload without IP/UDP headers */

/* ------------------------------------------------------------------ */
/*  Internal: espconn receive callback                                */
/* ------------------------------------------------------------------ */

static void ICACHE_FLASH_ATTR udp_recv_cb(void *arg,
                                          char *pdata, unsigned short len)
{
    struct espconn *conn = (struct espconn *)arg;
    if (!conn || !conn->proto.udp) return;

    uint16_t dst_port = conn->proto.udp->local_port;
    uint32_t src_ip   = 0;
    uint16_t src_port = conn->proto.udp->remote_port;

    /* Build src_ip from remote_ip byte array (little-endian) */
    uint8_t *rip = conn->proto.udp->remote_ip;
    src_ip = ((uint32_t)rip[0])
           | ((uint32_t)rip[1] <<  8)
           | ((uint32_t)rip[2] << 16)
           | ((uint32_t)rip[3] << 24);

    /* Dispatch to registered handler for this port */
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (g_bindings[i].in_use && g_bindings[i].port == dst_port) {
            g_bindings[i].handler(src_ip, src_port,
                                  (uint8_t *)pdata, (uint16_t)len);
            return;
        }
    }
    /* No handler: silently drop */
}

/* ------------------------------------------------------------------ */
/*  UDP_Init                                                          */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR UDP_Init(void)
{
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        g_bindings[i].port    = 0;
        g_bindings[i].handler = (udp_handler_t)0;
        g_bindings[i].in_use  = 0;
        g_slots[i].active     = 0;
    }
    HAL_UART_PutString("[UDP ] init ok\n");
}

/* ------------------------------------------------------------------ */
/*  UDP_Bind                                                          */
/* ------------------------------------------------------------------ */

int ICACHE_FLASH_ATTR UDP_Bind(uint16_t port, udp_handler_t handler)
{
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (!g_bindings[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        HAL_UART_PutString("[UDP ] ERROR: binding table full\n");
        return -1;
    }

    /* Register in binding table */
    g_bindings[slot].port    = port;
    g_bindings[slot].handler = handler;
    g_bindings[slot].in_use  = 1;

    /* Create and bind an espconn UDP socket */
    udp_slot_t *s = &g_slots[slot];
    os_memset(&s->conn,    0, sizeof(s->conn));
    os_memset(&s->udp_cfg, 0, sizeof(s->udp_cfg));

    s->udp_cfg.local_port = port;
    s->conn.type          = ESPCONN_UDP;
    s->conn.state         = ESPCONN_NONE;
    s->conn.proto.udp     = &s->udp_cfg;
    s->port               = port;
    s->active             = 1;

    espconn_regist_recvcb(&s->conn, udp_recv_cb);
    espconn_create(&s->conn);

    HAL_UART_PutString("[UDP ] bound port ");
    HAL_UART_PutDec(port);
    HAL_UART_PutString("\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  UDP_Send                                                          */
/* ------------------------------------------------------------------ */

int ICACHE_FLASH_ATTR UDP_Send(uint32_t dst_ip, uint16_t dst_port,
                                uint16_t src_port,
                                uint8_t *payload, uint16_t len)
{
    if (len > 1472) {
        HAL_UART_PutString("[UDP ] ERROR: payload too large\n");
        return -1;
    }

    /* Find the espconn slot for src_port */
    int slot = -1;
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (g_slots[i].active && g_slots[i].port == src_port) {
            slot = i; break;
        }
    }

    /* If no slot match, use slot 0 (SFU port) */
    if (slot < 0) slot = 0;
    if (!g_slots[slot].active) return -1;

    udp_slot_t *s = &g_slots[slot];

    /* Configure remote endpoint */
    s->udp_cfg.remote_port = dst_port;
    /* Fill remote_ip from dst_ip (little-endian uint32 → byte array) */
    s->udp_cfg.remote_ip[0] = (uint8_t)(dst_ip);
    s->udp_cfg.remote_ip[1] = (uint8_t)(dst_ip >>  8);
    s->udp_cfg.remote_ip[2] = (uint8_t)(dst_ip >> 16);
    s->udp_cfg.remote_ip[3] = (uint8_t)(dst_ip >> 24);

    /* Copy payload into TX staging buffer */
    for (uint16_t i = 0; i < len; i++) g_udp_tx_buf[i] = payload[i];

    int ret = espconn_sendto(&s->conn, g_udp_tx_buf, len);
    return (ret == ESPCONN_OK) ? 0 : -1;
}
