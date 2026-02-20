/**
 * @file status.h
 * @brief Status codes and error handling for MiniOS
 *
 * All functions in MiniOS return Status codes following
 * the convention defined in the SRS error handling pattern.
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

    /* Graph/ML errors (future) */
    STATUS_ERROR_INVALID_GRAPH,
    STATUS_ERROR_UNSUPPORTED_OPERATOR,
    STATUS_ERROR_SHAPE_MISMATCH,

    /* Communication errors */
    STATUS_ERROR_COMM_FAILURE,
    STATUS_ERROR_CRC_MISMATCH,

    STATUS_ERROR_COUNT  /* Sentinel, must be last */
} Status;

/**
 * @brief Convert status code to human-readable string
 * @param[in] status The status code
 * @return String representation of the status
 */
const char* STATUS_ToString(Status status);

#endif /* MINIOS_STATUS_H */
