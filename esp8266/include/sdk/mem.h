/**
 * @file mem.h
 * @brief ESP8266 NonOS SDK lwIP memory pool stub.
 *
 * The ESP8266 NonOS SDK bundles a modified lwIP 1.4.x whose memory
 * functions are exposed via this header.  Our code only uses the
 * os_malloc / os_free / os_zalloc wrappers which are thin aliases.
 */

#ifndef _MEM_H_
#define _MEM_H_

#include "c_types.h"

/* ------------------------------------------------------------------ */
/*  Heap allocators (backed by SDK internal heap manager)             */
/* ------------------------------------------------------------------ */

/**
 * Allocate 'size' bytes from the SDK heap.
 * Returns NULL on allocation failure.
 */
void *pvPortMalloc(size_t size);

/**
 * Free a block previously returned by pvPortMalloc / os_malloc.
 * Passing NULL is safe (no-op).
 */
void vPortFree(void *ptr);

/* ------------------------------------------------------------------ */
/*  SDK convenience aliases (used throughout NonOS SDK sources)       */
/* ------------------------------------------------------------------ */

static inline void *os_malloc(size_t size)       { return pvPortMalloc(size); }
static inline void  os_free  (void *ptr)         { vPortFree(ptr); }

/**
 * Allocate 'size' bytes and zero-initialise them.
 * Equivalent to calloc(1, size) but backed by the SDK heap.
 */
void *os_zalloc(size_t size);

/* ------------------------------------------------------------------ */
/*  lwIP memory type enum (subset — only memp types our code touches) */
/* ------------------------------------------------------------------ */

typedef enum {
    MEMP_UDP_PCB   = 0,
    MEMP_TCP_PCB   = 1,
    MEMP_PBUF      = 2,
    MEMP_PBUF_POOL = 3,
} memp_t;

/* ------------------------------------------------------------------ */
/*  pbuf — packet buffer (minimal definition for wifi_udp usage)      */
/* ------------------------------------------------------------------ */

#define PBUF_RAM   0
#define PBUF_ROM   1
#define PBUF_REF   2
#define PBUF_POOL  3

#define PBUF_TRANSPORT  0x10
#define PBUF_IP         0x20
#define PBUF_LINK       0x40
#define PBUF_RAW        0x00

typedef struct pbuf {
    struct pbuf *next;   /**< next pbuf in chain (NULL = end)        */
    void        *payload;/**< pointer to data payload                */
    uint16       tot_len;/**< total length of this buf + all next    */
    uint16       len;    /**< length of this buffer's payload        */
    uint8        type;   /**< PBUF_RAM / PBUF_ROM / PBUF_REF / PBUF_POOL */
    uint8        flags;
    uint16       ref;    /**< reference count                        */
} pbuf;

struct pbuf *pbuf_alloc(uint8 layer, uint16 length, uint8 type);
void         pbuf_free (struct pbuf *p);

#endif /* _MEM_H_ */
