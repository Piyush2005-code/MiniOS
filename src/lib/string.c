/**
 * @file string.c
 * @brief Minimal string/memory utility implementations for MiniOS
 *
 * Freestanding implementations required by GCC even with -nostdlib.
 *
 * @complexity All functions: Time O(n), Space O(1)
 */

#include "lib/string.h"

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    uint8_t val = (uint8_t)c;

    /* Word-fill for large regions (8 bytes at a time) */
    if (n >= 8 && val == 0) {
        /* Align to 8-byte boundary first */
        while ((uintptr_t)p & 7 && n > 0) {
            *p++ = 0;
            n--;
        }
        uint64_t *wp = (uint64_t *)p;
        while (n >= 8) {
            *wp++ = 0;
            n -= 8;
        }
        p = (uint8_t *)wp;
    }

    while (n--) {
        *p++ = val;
    }

    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    /* Word-copy for aligned, large regions */
    if (n >= 8 &&
        ((uintptr_t)d & 7) == 0 &&
        ((uintptr_t)s & 7) == 0) {
        uint64_t *dw = (uint64_t *)d;
        const uint64_t *sw = (const uint64_t *)s;
        while (n >= 8) {
            *dw++ = *sw++;
            n -= 8;
        }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }

    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;

    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}
