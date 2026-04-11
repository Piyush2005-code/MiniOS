/**
 * @file status.h
 * @brief Status codes for MiniOS-ESP8266
 *
 * Identical to the ARM64 MiniOS status codes for source compatibility.
 * All functions return Status following the SRS error handling pattern.
 */

#ifndef MINIOS_ESP8266_STATUS_H
#define MINIOS_ESP8266_STATUS_H

#include "types.h"

typedef enum {
    STATUS_OK                        = 0,

    /* General errors */
    STATUS_ERROR_INVALID_ARGUMENT    = 1,
    STATUS_ERROR_NOT_SUPPORTED       = 2,
    STATUS_ERROR_NOT_INITIALIZED     = 3,

    /* Memory errors */
    STATUS_ERROR_OUT_OF_MEMORY       = 4,
    STATUS_ERROR_MEMORY_ALIGNMENT    = 5,
    STATUS_ERROR_MEMORY_PROTECTION   = 6,

    /* Hardware errors */
    STATUS_ERROR_HARDWARE_FAULT      = 7,
    STATUS_ERROR_TIMEOUT             = 8,

    /* Execution errors */
    STATUS_ERROR_EXECUTION_FAILED    = 9,
    STATUS_ERROR_EXECUTION_TIMEOUT   = 10,

    /* ML / Graph errors */
    STATUS_ERROR_INVALID_GRAPH       = 11,
    STATUS_ERROR_UNSUPPORTED_OPERATOR = 12,
    STATUS_ERROR_SHAPE_MISMATCH      = 13,

    /* Communication errors */
    STATUS_ERROR_COMM_FAILURE        = 14,
    STATUS_ERROR_CRC_MISMATCH        = 15,

    /* Threading (stub — single-threaded on ESP8266) */
    STATUS_ERROR_THREAD_LIMIT        = 16,
    STATUS_ERROR_SCHEDULER_ACTIVE    = 17,

    /* Memory pool */
    STATUS_ERROR_POOL_EXHAUSTED      = 18,

    /* Wi-Fi specific */
    STATUS_ERROR_WIFI_NOT_CONNECTED  = 19,
    STATUS_ERROR_WIFI_TIMEOUT        = 20,
} Status;

/**
 * @brief Convert a status code to a human-readable string.
 */
static inline const char* STATUS_ToString(Status s)
{
    switch (s) {
        case STATUS_OK:                         return "OK";
        case STATUS_ERROR_INVALID_ARGUMENT:     return "ERR_INVALID_ARG";
        case STATUS_ERROR_NOT_SUPPORTED:        return "ERR_NOT_SUPPORTED";
        case STATUS_ERROR_NOT_INITIALIZED:      return "ERR_NOT_INIT";
        case STATUS_ERROR_OUT_OF_MEMORY:        return "ERR_OOM";
        case STATUS_ERROR_MEMORY_ALIGNMENT:     return "ERR_ALIGN";
        case STATUS_ERROR_HARDWARE_FAULT:       return "ERR_HW";
        case STATUS_ERROR_TIMEOUT:              return "ERR_TIMEOUT";
        case STATUS_ERROR_EXECUTION_FAILED:     return "ERR_EXEC";
        case STATUS_ERROR_INVALID_GRAPH:        return "ERR_GRAPH";
        case STATUS_ERROR_UNSUPPORTED_OPERATOR: return "ERR_OP";
        case STATUS_ERROR_SHAPE_MISMATCH:       return "ERR_SHAPE";
        case STATUS_ERROR_COMM_FAILURE:         return "ERR_COMM";
        case STATUS_ERROR_CRC_MISMATCH:         return "ERR_CRC";
        case STATUS_ERROR_POOL_EXHAUSTED:       return "ERR_POOL";
        case STATUS_ERROR_WIFI_NOT_CONNECTED:   return "ERR_WIFI";
        case STATUS_ERROR_WIFI_TIMEOUT:         return "ERR_WIFI_TIMEOUT";
        default:                                return "ERR_UNKNOWN";
    }
}

#endif /* MINIOS_ESP8266_STATUS_H */
