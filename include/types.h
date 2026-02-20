/**
 * @file types.h
 * @brief Standard fixed-width type definitions for MiniOS
 *
 * Provides stdint-compatible types without libc dependency.
 * All kernel and HAL code should use these types.
 */

#ifndef MINIOS_TYPES_H
#define MINIOS_TYPES_H

/* Unsigned integer types */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

/* Signed integer types */
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

/* Size type */
typedef unsigned long       size_t;
typedef signed long         ssize_t;

/* Pointer-sized integer */
typedef unsigned long       uintptr_t;
typedef signed long         intptr_t;

/* Boolean — only define if not already a keyword (C23+) */
#ifndef __bool_true_false_are_defined
  #if __STDC_VERSION__ >= 202311L
    /* C23: bool, true, false are keywords — nothing to do */
  #else
    typedef _Bool             bool;
    #define true  1
    #define false 0
  #endif
  #define __bool_true_false_are_defined 1
#endif

/* NULL */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Volatile register access helpers */
#define REG32(addr)  (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG64(addr)  (*(volatile uint64_t *)(uintptr_t)(addr))

#endif /* MINIOS_TYPES_H */
