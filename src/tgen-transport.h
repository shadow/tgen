/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_TRANSPORT_H_
#define TGEN_TRANSPORT_H_

#include "tgen.h"

typedef struct _TGenTransport TGenTransport;

typedef void (*TGenTransport_notifyBytesFunc)(gpointer data, gsize bytesRead, gsize bytesWritten);

typedef struct _NotifyBytesCallback NotifyBytesCallback;
struct _NotifyBytesCallback {
    TGenTransport_notifyBytesFunc func;
    gpointer arg;
    GDestroyNotify argRef;
    GDestroyNotify argUnref;
};

TGenTransport* tgentransport_newActive(TGenStreamOptions* options, NotifyBytesCallback bytesCB,
        TGenPeer* socksProxy, const gchar* socksUsername, const gchar* socksPassword);
TGenTransport* tgentransport_newPassive(gint socketD, gint64 started, gint64 created,
        TGenPeer* peer, NotifyBytesCallback bytesCB);

void tgentransport_ref(TGenTransport* transport);
void tgentransport_unref(TGenTransport* transport);

gssize tgentransport_write(TGenTransport* transport, gpointer buffer, gsize length);
gssize tgentransport_read(TGenTransport* transport, gpointer buffer, gsize length);

void tgentransport_shutdownWrites(TGenTransport* transport);

gint64 tgentransport_getStartTimestamp(TGenTransport* transport);
gint tgentransport_getDescriptor(TGenTransport* transport);
const gchar* tgentransport_toString(TGenTransport* transport);
gchar* tgentransport_getTimeStatusReport(TGenTransport* transport);

gboolean tgentransport_wantsEvents(TGenTransport* transport);
TGenEvent tgentransport_onEvent(TGenTransport* transport, TGenEvent events);

gboolean tgentransport_checkTimeout(TGenTransport* transport, gint64 stalloutUSecs, gint64 timeoutUSecs);

#endif /* TGEN_TRANSPORT_H_ */
