/**
 * @file c_types.h
 * @brief ESP8266 NonOS SDK basic type stubs for host-side cross-compilation.
 *
 * When the full NonOS SDK is NOT installed (SDK_ROOT absent), this stub
 * provides all type definitions that the ESP8266 SDK normally delivers.
 * Taken verbatim from ESP8266_NONOS_SDK v3.0 include/c_types.h.
 *
 * The actual function implementations live in libmain.a / ROM — this
 * header is needed only for the compiler (type-checking) stage.
 */

#ifndef _C_TYPES_H_
#define _C_TYPES_H_

/* ------------------------------------------------------------------ */
/*  Exact-width integer types (mirror SDK originals)                  */
/* ------------------------------------------------------------------ */

typedef unsigned char           uint8;
typedef signed char             sint8;
typedef unsigned short          uint16;
typedef signed short            sint16;
typedef unsigned int            uint32;
typedef signed int              sint32;
typedef unsigned long long      uint64;
typedef signed long long        sint64;
typedef float                   real32;
typedef double                  real64;

/* stdint-compatible types — only define if not already provided */
#ifndef _UINT32_T_DECLARED
#  define _UINT32_T_DECLARED
typedef unsigned int            uint32_t;
#endif
#ifndef _INT32_T_DECLARED
#  define _INT32_T_DECLARED
typedef signed int              int32_t;
#endif
#ifndef _UINT16_T_DECLARED
#  define _UINT16_T_DECLARED
typedef unsigned short          uint16_t;
#endif
#ifndef _INT16_T_DECLARED
#  define _INT16_T_DECLARED
typedef signed short            int16_t;
#endif
#ifndef _UINT8_T_DECLARED
#  define _UINT8_T_DECLARED
typedef unsigned char           uint8_t;
#endif
#ifndef _INT8_T_DECLARED
#  define _INT8_T_DECLARED
typedef signed char             int8_t;
#endif
#ifndef _UINT64_T_DECLARED
#  define _UINT64_T_DECLARED
typedef unsigned long long      uint64_t;
#endif
#ifndef _INT64_T_DECLARED
#  define _INT64_T_DECLARED
typedef signed long long        int64_t;
#endif
#ifndef _UINTPTR_T_DECLARED
#  define _UINTPTR_T_DECLARED
typedef unsigned int            uintptr_t;
#endif
#ifndef _INTPTR_T_DECLARED
#  define _INTPTR_T_DECLARED
typedef signed int              intptr_t;
#endif
#ifndef _SIZE_T_DECLARED
#  define _SIZE_T_DECLARED
typedef unsigned int            size_t;
#endif

/* Boolean — guard against stdbool.h and C++ built-in */
#ifndef __cplusplus
#  ifndef __bool_true_false_are_defined
#    define __bool_true_false_are_defined 1
typedef unsigned char           bool;
#    define true   ((bool)1)
#    define false  ((bool)0)
#  endif
#endif
#ifndef TRUE
#  define TRUE   1
#  define FALSE  0
#endif

/* NULL */
#ifndef NULL
#  define NULL  ((void *)0)
#endif

/* Volatile helpers */
#define LOCAL   static
#define SVAL(addr)  (*(volatile sint16 *)(addr))
#define UVAL(addr)  (*(volatile uint16 *)(addr))

#endif /* _C_TYPES_H_ */
