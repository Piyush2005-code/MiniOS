/**
 * @file os_type.h
 * @brief ESP8266 NonOS SDK OS task/event type stubs.
 */

#ifndef _OS_TYPE_H_
#define _OS_TYPE_H_

#include "c_types.h"

/* ------------------------------------------------------------------ */
/*  Signal / parameter types used by system_os_post / system_os_task  */
/* ------------------------------------------------------------------ */

typedef uint32   os_signal_t;
typedef uint32   os_param_t;

/** Event message passed to a system_os_task callback. */
typedef struct {
    os_signal_t sig;
    os_param_t  par;
} os_event_t;

/** Prototype of a function registered with system_os_task(). */
typedef void (*ETSTask)(os_event_t *e);

/* Legacy alias */
typedef ETSTask  ETSEventHandler;

#endif /* _OS_TYPE_H_ */
