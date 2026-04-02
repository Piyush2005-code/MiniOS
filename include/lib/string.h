/**
 * @file string.h
 * @brief Minimal string/memory utility functions for MiniOS
 *
 * Freestanding implementations of essential libc memory and
 * string functions. Required because GCC may emit calls to
 * memset/memcpy even with -ffreestanding -nostdlib.
 */

#ifndef MINIOS_LIB_STRING_H
#define MINIOS_LIB_STRING_H

#include "types.h"

/**
 * @brief Fill memory with a constant byte
 * @param[out] s  Pointer to memory to fill
 * @param[in]  c  Byte value to set (cast to unsigned char)
 * @param[in]  n  Number of bytes to fill
 * @return Pointer to s
 */
void *memset(void *s, int c, size_t n);

/**
 * @brief Copy memory from source to destination
 * @param[out] dest  Destination buffer
 * @param[in]  src   Source buffer
 * @param[in]  n     Number of bytes to copy
 * @return Pointer to dest
 *
 * @warning Behavior is undefined if regions overlap. Use memmove
 *          for overlapping regions (not currently provided).
 */
void *memcpy(void *dest, const void *src, size_t n);

/**
 * @brief Compute the length of a null-terminated string
 * @param[in] s  The string
 * @return Number of characters before the null terminator
 */
size_t strlen(const char *s);

/**
 * @brief Compare memory regions
 * @param[in] s1  First memory region
 * @param[in] s2  Second memory region
 * @param[in] n   Number of bytes to compare
 * @return 0 if equal, <0 if s1 < s2, >0 if s1 > s2
 */
int memcmp(const void *s1, const void *s2, size_t n);

#endif /* MINIOS_LIB_STRING_H */
