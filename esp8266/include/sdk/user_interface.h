/**
 * @file user_interface.h
 * @brief ESP8266 NonOS SDK system + Wi-Fi interface stubs.
 *
 * Covers every SDK function called by our HAL and scheduler:
 *   - system_get_time / system_get_free_heap_size / system_restart
 *   - system_os_task / system_os_post / system_soft_wdt_feed
 *   - wifi_set_opmode_current / wifi_station_* / wifi_set_event_handler_cb
 *   - Station event types / System_Event_t
 */

#ifndef _USER_INTERFACE_H_
#define _USER_INTERFACE_H_

#include "c_types.h"
#include "os_type.h"

/* ------------------------------------------------------------------ */
/*  System information                                                 */
/* ------------------------------------------------------------------ */

/** Microseconds since power-on (32-bit, wraps after ~71 min). */
uint32 system_get_time(void);

/** Current free heap in bytes (excludes SDK reserved memory). */
uint32 system_get_free_heap_size(void);

/** Software-trigger a chip reset (does not return). */
void system_restart(void);

/** Feed the software watchdog to prevent a 6 s WDT reset. */
void system_soft_wdt_feed(void);

/* ------------------------------------------------------------------ */
/*  Cooperative OS task queue                                          */
/* ------------------------------------------------------------------ */

/**
 * Register a task function at the given priority level.
 * Only ONE task per priority is allowed.
 *
 * @param task      Function pointer (ETSTask = void(*)(os_event_t*))
 * @param prio      ESP_TASK_PRIO_0 / _1 / _2
 * @param queue     Statically allocated os_event_t array
 * @param qlen      Length of the queue array
 * @return TRUE on success
 */
bool system_os_task(ETSTask task, uint8 prio,
                    os_event_t *queue, uint8 qlen);

/**
 * Post a signal to a registered task.
 * Safe to call from timer callbacks and interrupt context.
 *
 * @param prio  Priority of target task
 * @param sig   Signal value (uint32)
 * @param par   Parameter (uint32)
 * @return TRUE if the message was enqueued, FALSE if queue full
 */
bool system_os_post(uint8 prio, os_signal_t sig, os_param_t par);

/* ------------------------------------------------------------------ */
/*  Wi-Fi operating mode                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    NULL_MODE      = 0,
    STATION_MODE   = 1,
    SOFTAP_MODE    = 2,
    STATIONAP_MODE = 3,
} WIFI_MODE;

/** Set the Wi-Fi operating mode (does not save to flash). */
bool wifi_set_opmode_current(WIFI_MODE opmode);

/* ------------------------------------------------------------------ */
/*  Wi-Fi station configuration                                        */
/* ------------------------------------------------------------------ */

/** Maximum SSID length per SDK spec. */
#define ESP_SSID_MAXLEN     32
/** Maximum password length per SDK spec. */
#define ESP_PSK_MAXLEN      64

typedef struct station_config {
    uint8  ssid[ESP_SSID_MAXLEN];
    uint8  password[ESP_PSK_MAXLEN];
    uint8  bssid_set;
    uint8  bssid[6];
} station_config;

/** Apply station config without saving (survives reboot via RAM only). */
bool wifi_station_set_config_current(struct station_config *config);

/** Begin the station association / authentication sequence. */
bool wifi_station_connect(void);

/** Disconnect from the current AP. */
bool wifi_station_disconnect(void);

/** Enable/disable automatic reconnection after a disconnect. */
bool wifi_station_set_auto_connect(uint8 set);

/** Set whether station should reconnect if connection is lost. */
bool wifi_station_set_reconnect_policy(bool enable);

/** Query RSSI of the associated AP (dBm, negative). */
sint8 wifi_station_get_rssi(void);

/* ------------------------------------------------------------------ */
/*  DHCP client                                                        */
/* ------------------------------------------------------------------ */

bool wifi_station_dhcpc_start(void);
bool wifi_station_dhcpc_stop(void);

/* ------------------------------------------------------------------ */
/*  Static IP configuration                                            */
/* ------------------------------------------------------------------ */

/** Interface selector. */
typedef enum { STATION_IF = 0, SOFTAP_IF = 1 } WIFI_INTERFACE;

/** IPv4 address (stored as uint32 in network byte order). */
typedef struct { uint32 addr; } ip_addr_t;

typedef struct ip_info {
    ip_addr_t ip;
    ip_addr_t netmask;
    ip_addr_t gw;
} ip_info;

bool wifi_set_ip_info(WIFI_INTERFACE if_index, ip_info *info);

/** Parse a dotted-decimal string into a uint32 IP address. */
uint32 ipaddr_addr(const char *addr);

/* ------------------------------------------------------------------ */
/*  Wi-Fi event handler                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    EVENT_STAMODE_CONNECTED       = 0,
    EVENT_STAMODE_DISCONNECTED    = 1,
    EVENT_STAMODE_AUTHMODE_CHANGE = 2,
    EVENT_STAMODE_GOT_IP          = 3,
    EVENT_STAMODE_DHCP_TIMEOUT    = 4,
    EVENT_SOFTAPMODE_STACONNECTED = 5,
    EVENT_SOFTAPMODE_STADISCONNECTED = 6,
    EVENT_OPMODE_CHANGED          = 8,
} System_Event_ID;

/** Embedded in System_Event_t.event_info for EVENT_STAMODE_GOT_IP. */
typedef struct {
    ip_addr_t ip;
    ip_addr_t mask;
    ip_addr_t gw;
} Event_StaMode_Got_IP_t;

/** Embedded in System_Event_t.event_info for EVENT_STAMODE_CONNECTED. */
typedef struct {
    uint8  ssid[32];
    uint8  ssid_len;
    uint8  bssid[6];
    uint8  channel;
} Event_StaMode_Connected_t;

/** Embedded in System_Event_t.event_info for EVENT_STAMODE_DISCONNECTED. */
typedef struct {
    uint8  ssid[32];
    uint8  ssid_len;
    uint8  bssid[6];
    uint8  reason;
} Event_StaMode_Disconnected_t;

/** Union of all possible event payloads. */
typedef union {
    Event_StaMode_Connected_t    connected;
    Event_StaMode_Disconnected_t disconnected;
    Event_StaMode_Got_IP_t       got_ip;
} Event_Info_u;

/** Top-level event structure passed to the registered callback. */
typedef struct {
    uint32       event;        /**< System_Event_ID */
    Event_Info_u event_info;
} System_Event_t;

typedef void (*wifi_event_handler_cb_t)(System_Event_t *event);

/** Register a callback for all Wi-Fi/IP events. */
void wifi_set_event_handler_cb(wifi_event_handler_cb_t cb);

#endif /* _USER_INTERFACE_H_ */
