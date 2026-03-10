/**
 * @file status_stub.c
 * @brief Provides STATUS_ToString for host-side tests.
 *
 * On the host we cannot compile main.c (it includes hal/arch.h with ARM64 asm).
 * This stub provides just the STATUS_ToString function that test_status.c needs.
 */

#include <stdint.h>
#include "status.h"

const char *STATUS_ToString(Status s)
{
    switch (s) {
        case STATUS_OK:                          return "OK";
        case STATUS_ERROR_INVALID_ARGUMENT:      return "INVALID_ARGUMENT";
        case STATUS_ERROR_NOT_SUPPORTED:         return "NOT_SUPPORTED";
        case STATUS_ERROR_NOT_INITIALIZED:       return "NOT_INITIALIZED";
        case STATUS_ERROR_OUT_OF_MEMORY:         return "OUT_OF_MEMORY";
        case STATUS_ERROR_MEMORY_ALIGNMENT:      return "MEMORY_ALIGNMENT";
        case STATUS_ERROR_MEMORY_PROTECTION:     return "MEMORY_PROTECTION";
        case STATUS_ERROR_HARDWARE_FAULT:        return "HARDWARE_FAULT";
        case STATUS_ERROR_TIMEOUT:               return "TIMEOUT";
        case STATUS_ERROR_EXECUTION_FAILED:      return "EXECUTION_FAILED";
        case STATUS_ERROR_EXECUTION_TIMEOUT:     return "EXECUTION_TIMEOUT";
        case STATUS_ERROR_INVALID_GRAPH:         return "INVALID_GRAPH";
        case STATUS_ERROR_UNSUPPORTED_OPERATOR:  return "UNSUPPORTED_OPERATOR";
        case STATUS_ERROR_SHAPE_MISMATCH:        return "SHAPE_MISMATCH";
        case STATUS_ERROR_COMM_FAILURE:          return "COMM_FAILURE";
        case STATUS_ERROR_CRC_MISMATCH:          return "CRC_MISMATCH";
        case STATUS_ERROR_THREAD_LIMIT:          return "THREAD_LIMIT";
        case STATUS_ERROR_SCHEDULER_ACTIVE:      return "SCHEDULER_ACTIVE";
        case STATUS_ERROR_POOL_EXHAUSTED:        return "POOL_EXHAUSTED";
        default:                                 return "UNKNOWN";
    }
}
