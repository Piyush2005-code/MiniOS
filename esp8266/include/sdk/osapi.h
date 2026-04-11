/**
 * @file osapi.h
 * @brief ESP8266 NonOS SDK osapi stubs — macros match the real SDK exactly.
 *
 * All os_memset/os_timer_arm/etc. are #define macros in the real SDK,
 * NOT functions.  They alias to ets_* ROM functions declared in ets_sys.h.
 * This stub reproduces the real osapi.h macro-for-macro so our code
 * compiles identically whether building against stubs or the real SDK.
 */

#ifndef _OSAPI_H_
#define _OSAPI_H_

#include "c_types.h"
#include "os_type.h"

/* ------------------------------------------------------------------ */
/*  os_timer_t — opaque repeating/one-shot software timer             */
/* ------------------------------------------------------------------ */

typedef struct _os_timer_t {
    void          *timer_next;
    uint32         timer_expire;
    uint32         timer_period;
    void          (*timer_func)(void *);
    void          *timer_arg;
} os_timer_t;

typedef void (*os_timer_func_t)(void *timer_arg);

/* ------------------------------------------------------------------ */
/*  ETS ROM function declarations (resolved by eagle.rom.addr.v6.ld)  */
/* ------------------------------------------------------------------ */

/* Memory */
void *ets_memset (void *s, int c, size_t n);
void *ets_memcpy (void *dst, const void *src, size_t n);
void *ets_memmove(void *dst, const void *src, size_t n);
int   ets_memcmp (const void *s1, const void *s2, size_t n);
void  ets_bzero  (void *s, size_t n);

/* Strings */
char  *ets_strcpy (char *dst, const char *src);
char  *ets_strncpy(char *dst, const char *src, size_t n);
int    ets_strcmp (const char *s1, const char *s2);
int    ets_strncmp(const char *s1, const char *s2, size_t n);
size_t ets_strlen (const char *s);
char  *ets_strstr (const char *haystack, const char *needle);
char  *ets_strchr (const char *s, int c);

/* Time */
void ets_delay_us(uint32 us);

/* Printf */
void ets_install_putc1(void (*p)(char c));
int  ets_sprintf (char *buf, const char *fmt, ...);
int  ets_snprintf(char *buf, size_t size, const char *fmt, ...);

/* Timer (ROM dispatcher) */
void ets_timer_disarm  (os_timer_t *ptimer);
void ets_timer_setfn   (os_timer_t *ptimer, os_timer_func_t fn, void *arg);
void ets_timer_arm_new (os_timer_t *ptimer, uint32 ms, bool repeat, bool is_ms);

/* ------------------------------------------------------------------ */
/*  os_* macros — identical to the real NonOS SDK osapi.h             */
/* ------------------------------------------------------------------ */

/* Memory helpers */
#define os_bzero(s, n)            ets_bzero((s), (n))
#define os_memcmp(s1, s2, n)      ets_memcmp((s1), (s2), (n))
#define os_memcpy(dst, src, n)    ets_memcpy((dst), (src), (n))
#define os_memmove(dst, src, n)   ets_memmove((dst), (src), (n))
#define os_memset(s, c, n)        ets_memset((s), (c), (n))

/* String helpers */
#define os_strcat(d, s)           strcat((d), (s))
#define os_strchr(s, c)           strchr((s), (c))
#define os_strcmp(s1, s2)         ets_strcmp((s1), (s2))
#define os_strcpy(d, s)           ets_strcpy((d), (s))
#define os_strlen(s)              ets_strlen((s))
#define os_strncmp(s1, s2, n)     ets_strncmp((s1), (s2), (n))
#define os_strncpy(d, s, n)       ets_strncpy((d), (s), (n))
#define os_strstr(h, n)           ets_strstr((h), (n))

/* Delay */
#define os_delay_us(us)           ets_delay_us((us))

/* Timers */
#define os_timer_arm(t, ms, r)    ets_timer_arm_new((t), (ms), (r), 1)
#define os_timer_arm_us(t, us, r) ets_timer_arm_new((t), (us), (r), 0)
#define os_timer_disarm(t)        ets_timer_disarm((t))
#define os_timer_setfn(t, fn, a)  ets_timer_setfn((t), (fn), (a))

/* Printf */
#define os_sprintf(buf, fmt, ...) ets_sprintf((buf), (fmt), ##__VA_ARGS__)

#endif /* _OSAPI_H_ */
