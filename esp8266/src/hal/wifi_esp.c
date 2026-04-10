/**
 * @file wifi_esp.c
 * @brief Wi-Fi HAL Implementation — ESP8266 Station Mode
 *
 * Connects the ESP8266 to a Wi-Fi network using the NonOS SDK.
 * Uses event-driven callbacks for connection state tracking.
 * Once connected (WIFI_STATE_GOT_IP), the UDP layer can send/receive.
 *
 * SSID and password are read from user_config.h at compile time.
 * Supports runtime reconfiguration via UART shell "reconnect" command.
 */

#include "hal/wifi.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "types.h"
#include "../../user_config.h"

/* ESP8266 NonOS SDK includes */
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "espconn.h"

/* ------------------------------------------------------------------ */
/*  Module State                                                      */
/* ------------------------------------------------------------------ */

static volatile wifi_state_t g_wifi_state = WIFI_STATE_DISCONNECTED;
static uint32_t              g_local_ip   = 0;

/* ------------------------------------------------------------------ */
/*  Internal: Wi-Fi event handler                                     */
/* ------------------------------------------------------------------ */

static void ICACHE_FLASH_ATTR wifi_event_cb(System_Event_t *event)
{
    if (!event) return;

    switch (event->event) {

        case EVENT_STAMODE_CONNECTED:
            g_wifi_state = WIFI_STATE_CONNECTED;
#if DEBUG_WIFI_EVENTS
            HAL_UART_PutString("[WiFi] connected to AP\n");
#endif
            break;

        case EVENT_STAMODE_DISCONNECTED:
            g_wifi_state = WIFI_STATE_DISCONNECTED;
            g_local_ip   = 0;
#if DEBUG_WIFI_EVENTS
            HAL_UART_PutString("[WiFi] disconnected\n");
#endif
            break;

        case EVENT_STAMODE_GOT_IP:
            g_wifi_state = WIFI_STATE_GOT_IP;
            g_local_ip   = event->event_info.got_ip.ip.addr;
#if DEBUG_WIFI_EVENTS
            {
                char ip_str[16];
                HAL_WiFi_GetIPString(ip_str);
                HAL_UART_PutString("[WiFi] got IP: ");
                HAL_UART_PutString(ip_str);
                HAL_UART_PutString("\n");
            }
#endif
            break;

        case EVENT_STAMODE_AUTHMODE_CHANGE:
            HAL_UART_PutString("[WiFi] auth mode changed\n");
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  HAL_WiFi_Init                                                     */
/* ------------------------------------------------------------------ */

Status ICACHE_FLASH_ATTR HAL_WiFi_Init(void)
{
    g_wifi_state = WIFI_STATE_CONNECTING;
    g_local_ip   = 0;

    /* Register event handler */
    wifi_set_event_handler_cb(wifi_event_cb);

    /* Set station mode */
    wifi_set_opmode_current(STATION_MODE);

    /* Configure SSID + password from user_config.h */
    struct station_config sta_cfg;
    os_memset(&sta_cfg, 0, sizeof(sta_cfg));
    os_strncpy((char *)sta_cfg.ssid,     WIFI_SSID,     32);
    os_strncpy((char *)sta_cfg.password, WIFI_PASSWORD, 64);
    sta_cfg.bssid_set = 0;

    wifi_station_set_config_current(&sta_cfg);

#if WIFI_USE_DHCP
    wifi_station_dhcpc_start();
#else
    /* Static IP configuration */
    wifi_station_dhcpc_stop();
    struct ip_info ip_cfg;
    ip_cfg.ip.addr      = ipaddr_addr(WIFI_STATIC_IP);
    ip_cfg.gw.addr      = ipaddr_addr(WIFI_STATIC_GW);
    ip_cfg.netmask.addr = ipaddr_addr(WIFI_STATIC_MASK);
    wifi_set_ip_info(STATION_IF, &ip_cfg);
#endif

    /* Auto-reconnect on drop */
    wifi_station_set_auto_connect(1);
    wifi_station_set_reconnect_policy(true);

    /* Initiate connection */
    wifi_station_connect();

    HAL_UART_PutString("[WiFi] connecting to: ");
    HAL_UART_PutString(WIFI_SSID);
    HAL_UART_PutString("\n");

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  HAL_WiFi_GetState                                                 */
/* ------------------------------------------------------------------ */

wifi_state_t HAL_WiFi_GetState(void)
{
    return g_wifi_state;
}

/* ------------------------------------------------------------------ */
/*  HAL_WiFi_WaitForIP                                                */
/* ------------------------------------------------------------------ */

Status ICACHE_FLASH_ATTR HAL_WiFi_WaitForIP(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t poll_ms = 100;

    while (g_wifi_state != WIFI_STATE_GOT_IP) {
        HAL_Timer_DelayMs(poll_ms);
        elapsed += poll_ms;

        if (elapsed >= timeout_ms) {
            HAL_UART_PutString("[WiFi] timeout waiting for IP\n");
            return STATUS_ERROR_WIFI_TIMEOUT;
        }

#if DEBUG_WIFI_EVENTS
        if ((elapsed % 1000) == 0) {
            HAL_UART_PutString("[WiFi] waiting... ");
            HAL_UART_PutDec(elapsed / 1000);
            HAL_UART_PutString("s\n");
        }
#endif
    }

    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/*  HAL_WiFi_GetIP                                                    */
/* ------------------------------------------------------------------ */

uint32_t HAL_WiFi_GetIP(void)
{
    if (g_wifi_state != WIFI_STATE_GOT_IP) return 0;
    return g_local_ip;
}

/* ------------------------------------------------------------------ */
/*  HAL_WiFi_GetIPString                                              */
/* ------------------------------------------------------------------ */

void ICACHE_FLASH_ATTR HAL_WiFi_GetIPString(char *buf)
{
    if (g_wifi_state != WIFI_STATE_GOT_IP || g_local_ip == 0) {
        /* "0.0.0.0" */
        buf[0] = '0'; buf[1] = '.'; buf[2] = '0'; buf[3] = '.';
        buf[4] = '0'; buf[5] = '.'; buf[6] = '0'; buf[7] = '\0';
        return;
    }

    /* IP is stored in little-endian byte order */
    uint8_t b0 = (uint8_t)(g_local_ip);
    uint8_t b1 = (uint8_t)(g_local_ip >> 8);
    uint8_t b2 = (uint8_t)(g_local_ip >> 16);
    uint8_t b3 = (uint8_t)(g_local_ip >> 24);

    /* Build "b0.b1.b2.b3" string manually */
    int pos = 0;
    uint8_t octets[4] = { b0, b1, b2, b3 };
    for (int i = 0; i < 4; i++) {
        uint8_t v = octets[i];
        if (v >= 100) { buf[pos++] = (char)('0' + v / 100); v %= 100; }
        if (v >= 10)  { buf[pos++] = (char)('0' + v / 10);  v %= 10;  }
        buf[pos++] = (char)('0' + v);
        if (i < 3) buf[pos++] = '.';
    }
    buf[pos] = '\0';
}

/* ------------------------------------------------------------------ */
/*  HAL_WiFi_GetRSSI                                                  */
/* ------------------------------------------------------------------ */

int8_t HAL_WiFi_GetRSSI(void)
{
    if (g_wifi_state != WIFI_STATE_GOT_IP) return 0;
    return (int8_t)wifi_station_get_rssi();
}

/* ------------------------------------------------------------------ */
/*  HAL_WiFi_Reconnect                                                */
/* ------------------------------------------------------------------ */

Status ICACHE_FLASH_ATTR HAL_WiFi_Reconnect(void)
{
    g_wifi_state = WIFI_STATE_CONNECTING;
    wifi_station_disconnect();
    HAL_Timer_DelayMs(100);
    wifi_station_connect();
    HAL_UART_PutString("[WiFi] reconnecting...\n");
    return STATUS_OK;
}
