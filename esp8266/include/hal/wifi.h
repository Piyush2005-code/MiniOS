/**
 * @file wifi.h
 * @brief Wi-Fi HAL API for MiniOS-ESP8266
 *
 * Wraps the ESP8266 NonOS SDK Wi-Fi station API behind a clean MiniOS
 * interface.  All higher-level code (shell, SFU) uses only these functions.
 *
 * State machine:
 *   DISCONNECTED → (HAL_WiFi_Init) → CONNECTING → (DHCP OK) → GOT_IP
 *   GOT_IP → (AP drop) → DISCONNECTED (auto-reconnect retries)
 */

#ifndef MINIOS_ESP8266_HAL_WIFI_H
#define MINIOS_ESP8266_HAL_WIFI_H

#include "types.h"
#include "status.h"

/* ------------------------------------------------------------------ */
/*  Wi-Fi connection state                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,  /**< Not associated with any AP        */
    WIFI_STATE_CONNECTING   = 1,  /**< Association / DHCP in progress    */
    WIFI_STATE_CONNECTED    = 2,  /**< L2 associated, waiting for DHCP   */
    WIFI_STATE_GOT_IP       = 3,  /**< DHCP complete — ready to use UDP  */
} wifi_state_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Start Wi-Fi in STATION mode and begin associating.
 *
 * Reads SSID/password from user_config.h.  Association is asynchronous —
 * call HAL_WiFi_WaitForIP() to block until DHCP completes.
 *
 * @return STATUS_OK always (errors are reported via UART log).
 */
Status HAL_WiFi_Init(void);

/**
 * @brief Return the current Wi-Fi connection state.
 */
wifi_state_t HAL_WiFi_GetState(void);

/**
 * @brief Poll until the Wi-Fi stack obtains a DHCP lease or times out.
 *
 * Internally calls HAL_Timer_DelayMs(100) in a loop, feeding the SDK
 * watchdog on each iteration.  Safe to call from user_init().
 *
 * @param timeout_ms Maximum wait time in milliseconds.
 * @return STATUS_OK if an IP was obtained, STATUS_ERROR_WIFI_TIMEOUT otherwise.
 */
Status HAL_WiFi_WaitForIP(uint32_t timeout_ms);

/**
 * @brief Get the current local IP address as a uint32 (network byte order).
 * @return IP address, or 0 if not connected.
 */
uint32_t HAL_WiFi_GetIP(void);

/**
 * @brief Write the current IP address as a dotted-decimal C string.
 * @param buf  Caller-allocated buffer of at least 16 bytes.
 *             Set to "0.0.0.0" if not connected.
 */
void HAL_WiFi_GetIPString(char *buf);

/**
 * @brief Get the RSSI of the associated AP.
 * @return RSSI in dBm (negative), or 0 if not connected.
 */
int8_t HAL_WiFi_GetRSSI(void);

/**
 * @brief Disconnect from current AP and reconnect.
 * @return STATUS_OK always.
 */
Status HAL_WiFi_Reconnect(void);

#endif /* MINIOS_ESP8266_HAL_WIFI_H */
