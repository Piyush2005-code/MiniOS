/**
 * @file string.c
 * @brief Freestanding string and memory utility implementations
 */

#include "lib/string.h"

void *memset(void *dst, int value, size_t count)
{
    unsigned char *p = (unsigned char *)dst;
    unsigned char  v = (unsigned char)value;
    while (count--) {
        *p++ = v;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t count)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (count--) {
        *d++ = *s++;
    }
    return dst;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0') {
        p++;
    }
    return (size_t)(p - s);
}
