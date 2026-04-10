/**
 * @file types.h
 * @brief Fundamental type definitions for MiniOS-ESP8266 (Xtensa LX106)
 *
 * Provides stdint-compatible types for the freestanding ESP8266 environment.
 * Key differences from ARM64 types.h:
 *   - size_t  = uint32_t (4 bytes, not 8)
 *   - uintptr_t = uint32_t (32-bit address space)
 *   - All pointer-sized quantities are 32-bit
 *
 * The ESP8266 NonOS SDK provides <c_types.h> which defines uint8_t etc.,
 * but we define our own for source compatibility with the MiniOS codebase.
 */

#ifndef MINIOS_ESP8266_TYPES_H
#define MINIOS_ESP8266_TYPES_H

/* Use SDK types if building under ESP8266 NonOS SDK */
#ifdef USE_ESP_SDK
#  include <c_types.h>
#  include <osapi.h>
#else

/* ------------------------------------------------------------------ */
/*  Exact-width integer types                                         */
/* ------------------------------------------------------------------ */

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

/* ------------------------------------------------------------------ */
/*  Platform-width types (32-bit on Xtensa LX106)                    */
/* ------------------------------------------------------------------ */

/** Unsigned size type — 4 bytes on ESP8266 (was 8 on ARM64) */
typedef uint32_t            size_t;

/** Unsigned pointer-width integer — 4 bytes on ESP8266 */
typedef uint32_t            uintptr_t;

/** Signed pointer-width integer — 4 bytes on ESP8266 */
typedef int32_t             intptr_t;

/* ------------------------------------------------------------------ */
/*  Boolean type                                                      */
/* ------------------------------------------------------------------ */

typedef uint8_t             bool;
#define true                ((bool)1)
#define false               ((bool)0)

/* ------------------------------------------------------------------ */
/*  NULL                                                              */
/* ------------------------------------------------------------------ */

#ifndef NULL
#  define NULL              ((void*)0)
#endif

#endif /* USE_ESP_SDK */

/* ------------------------------------------------------------------ */
/*  Helper macros                                                     */
/* ------------------------------------------------------------------ */

/** Register access macro for MMIO (ESP8266 peripheral registers) */
#define REG32(addr)         (*(volatile uint32_t *)(addr))
#define REG8(addr)          (*(volatile uint8_t  *)(addr))

/** Alignment attribute */
#define ALIGNED(n)          __attribute__((aligned(n)))

/** Place in IRAM (fast instruction RAM, 32 KB) */
#define ICACHE_FLASH_ATTR   __attribute__((section(".irom0.text")))
#define IRAM_ATTR           __attribute__((section(".iram0.text")))

/** Unused parameter silencer */
#define UNUSED(x)           ((void)(x))

/* ------------------------------------------------------------------ */
/*  Integer limits                                                    */
/* ------------------------------------------------------------------ */

#define UINT8_MAX   0xFFu
#define UINT16_MAX  0xFFFFu
#define UINT32_MAX  0xFFFFFFFFu
#define INT8_MIN    (-128)
#define INT8_MAX    127
#define INT16_MIN   (-32768)
#define INT16_MAX   32767
#define INT32_MIN   (-2147483648)
#define INT32_MAX   2147483647

#endif /* MINIOS_ESP8266_TYPES_H */
