/**
 * @file status.h
 * @brief Status codes and error handling for MiniOS
 *
 * All public functions in MiniOS return a Status code.
 * The convention mirrors common embedded OS patterns.
 */

#ifndef MINIOS_STATUS_H
#define MINIOS_STATUS_H

typedef enum {
    STATUS_OK = 0,

    /* General errors */
    STATUS_ERROR_INVALID_ARGUMENT,
    STATUS_ERROR_NOT_SUPPORTED,
    STATUS_ERROR_NOT_INITIALIZED,

    /* Memory errors */
    STATUS_ERROR_OUT_OF_MEMORY,
    STATUS_ERROR_MEMORY_ALIGNMENT,
    STATUS_ERROR_MEMORY_PROTECTION,

    /* Hardware errors */
    STATUS_ERROR_HARDWARE_FAULT,
    STATUS_ERROR_TIMEOUT,

    /* Execution errors */
    STATUS_ERROR_EXECUTION_FAILED,
    STATUS_ERROR_EXECUTION_TIMEOUT,

    /* Graph / ML errors (future) */
    STATUS_ERROR_INVALID_GRAPH,
    STATUS_ERROR_UNSUPPORTED_OPERATOR,
    STATUS_ERROR_SHAPE_MISMATCH,

    /* Communication errors */
    STATUS_ERROR_COMM_FAILURE,
    STATUS_ERROR_CRC_MISMATCH,

    /* Allocator errors (pool allocator — Sprint 2) */
    STATUS_ERROR_POOL_EXHAUSTED,

    STATUS_ERROR_COUNT   /* sentinel — keep last */
} Status;

/**
 * @brief Convert a Status code to a human-readable string.
 */
const char* STATUS_ToString(Status status);

#endif /* MINIOS_STATUS_H */
