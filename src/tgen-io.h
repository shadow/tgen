/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef TGEN_IO_H_
#define TGEN_IO_H_

#include "tgen.h"

typedef enum _TGenEvent {
    TGEN_EVENT_NONE = 0,
    TGEN_EVENT_READ = 1 << 0,
    TGEN_EVENT_WRITE = 1 << 1,
    TGEN_EVENT_WRITE_DEFERRED = 1 << 2,
    TGEN_EVENT_DONE = 1 << 3,
} TGenEvent;

typedef struct _TGenIOResponse {
    /* The events that the IO module should listen for using epoll.
     * If TGEN_EVENT_WRITE_DEFERRED is specified, then we ignore the
     * TGEN_EVENT_WRITE flag, and we stop listening for write events now
     * and start listening again at deferUntilUSec absolute time.
     * If we are still deferring writes from a previous response, that
     * request will be cancelled and the new time will be used instead. */
    TGenEvent events;
    gint64 deferUntilUSec;
} TGenIOResponse;

typedef TGenIOResponse (*TGenIO_notifyEventFunc)(gpointer data, gint descriptor, TGenEvent events);
typedef gboolean (*TGenIO_notifyCheckTimeoutFunc)(gpointer data, gint descriptor);

typedef struct _TGenIO TGenIO;

TGenIO* tgenio_new();
void tgenio_ref(TGenIO* io);
void tgenio_unref(TGenIO* io);

gboolean tgenio_register(TGenIO* io, gint descriptor, TGenIO_notifyEventFunc notify,
        TGenIO_notifyCheckTimeoutFunc checkTimeout, gpointer data, GDestroyNotify destructData);

gint tgenio_loopOnce(TGenIO* io, gint maxEvents);
void tgenio_checkTimeouts(TGenIO* io);
gint tgenio_getEpollDescriptor(TGenIO* io);

#endif /* TGEN_IO_H_ */
