/**
 * @file string.h
 * @brief Freestanding string and memory utilities
 *
 * Replaces the libc string functions that are not available in a
 * -ffreestanding -nostdlib build environment.
 */

#ifndef MINIOS_STRING_H
#define MINIOS_STRING_H

#include "types.h"

/**
 * @brief Fill memory with a constant byte.
 * @param dst   Destination buffer.
 * @param value Byte value to fill.
 * @param count Number of bytes.
 * @return dst
 */
void *memset(void *dst, int value, size_t count);

/**
 * @brief Copy non-overlapping memory.
 * @param dst   Destination buffer.
 * @param src   Source buffer.
 * @param count Number of bytes.
 * @return dst
 */
void *memcpy(void *dst, const void *src, size_t count);

/**
 * @brief Return the length of a null-terminated string.
 * @param s Input string.
 * @return Number of characters before the null terminator.
 */
size_t strlen(const char *s);

#endif /* MINIOS_STRING_H */
