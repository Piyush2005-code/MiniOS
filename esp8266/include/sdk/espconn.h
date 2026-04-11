/**
 * @file espconn.h
 * @brief ESP8266 NonOS SDK espconn (TCP/UDP) stubs.
 *
 * Matches the actual NonOS SDK espconn.h layout so wifi_udp.c compiles
 * without modification:
 *   - espconn.proto.udp  (union of tcp/udp PCB pointers)
 *   - esp_udp.remote_ip  (uint8[4] byte array, little-endian)
 *   - esp_udp.local_port / remote_port
 */

#ifndef _ESPCONN_H_
#define _ESPCONN_H_

#include "c_types.h"

/* ------------------------------------------------------------------ */
/*  espconn connection state enum                                      */
/* ------------------------------------------------------------------ */

typedef enum {
    ESPCONN_NONE       = 0,
    ESPCONN_WAIT       = 1,
    ESPCONN_LISTEN     = 2,
    ESPCONN_CONNECT    = 3,
    ESPCONN_WRITE      = 4,
    ESPCONN_READ       = 5,
    ESPCONN_CLOSE      = 6,
} espconn_state;

/* ------------------------------------------------------------------ */
/*  espconn protocol type                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    ESPCONN_TCP = 0,
    ESPCONN_UDP = 1,
} espconn_type;

/* ------------------------------------------------------------------ */
/*  espconn error codes                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    ESPCONN_OK         =  0,
    ESPCONN_MEM        = -1,
    ESPCONN_TIMEOUT    = -3,
    ESPCONN_RTE        = -4,
    ESPCONN_INPROGRESS = -5,
    ESPCONN_ABRT       = -8,
    ESPCONN_RST        = -9,
    ESPCONN_CLSD       = -10,
    ESPCONN_CONN       = -11,
    ESPCONN_ARG        = -12,
    ESPCONN_ISCONN     = -15,
} espconn_err;

/* ------------------------------------------------------------------ */
/*  UDP PCB — remote_ip is a 4-byte array (little-endian)             */
/* ------------------------------------------------------------------ */

typedef struct _esp_udp {
    uint8  remote_ip[4];   /**< Remote IPv4 (byte array, little-endian) */
    uint16 remote_port;    /**< Remote UDP port (host byte order)       */
    uint16 local_port;     /**< Local  UDP port (host byte order)       */
} esp_udp;

/* ------------------------------------------------------------------ */
/*  TCP PCB (opaque stub — we don't use TCP in MiniOS)                */
/* ------------------------------------------------------------------ */

typedef struct _esp_tcp {
    uint8  remote_ip[4];
    uint16 remote_port;
    uint16 local_port;
    uint8  connect_timeout;
} esp_tcp;

/* ------------------------------------------------------------------ */
/*  espconn — the main connection structure used by espconn_*() API   */
/* ------------------------------------------------------------------ */

typedef struct espconn {
    espconn_state  state;    /**< Current connection state              */
    espconn_type   type;     /**< ESPCONN_TCP or ESPCONN_UDP            */

    /** Protocol-specific PCB — use conn->proto.udp or conn->proto.tcp */
    union {
        esp_tcp *tcp;
        esp_udp *udp;
    } proto;

    void *reverse;           /**< User-data / back-pointer              */
} espconn;

/* ------------------------------------------------------------------ */
/*  Callback typedefs                                                  */
/* ------------------------------------------------------------------ */

typedef void (*espconn_recv_callback)(void *arg, char *pdata, uint16 len);
typedef void (*espconn_sent_callback)(void *arg);

/* ------------------------------------------------------------------ */
/*  API functions used by wifi_udp.c                                  */
/* ------------------------------------------------------------------ */

sint8  espconn_create      (struct espconn *pespconn);
sint8  espconn_delete      (struct espconn *pespconn);
sint8  espconn_sendto      (struct espconn *pespconn,
                            uint8 *psent, uint16 length);
sint8  espconn_regist_recvcb(struct espconn *pespconn,
                             espconn_recv_callback recv_cb);
sint8  espconn_regist_sentcb(struct espconn *pespconn,
                             espconn_sent_callback sent_cb);

#endif /* _ESPCONN_H_ */
