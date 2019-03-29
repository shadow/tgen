/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef TGEN_SERVER_H_
#define TGEN_SERVER_H_

#include <glib.h>

typedef struct _TGenServer TGenServer;

typedef void (*TGenServer_notifyNewPeerFunc)(gpointer data, gint socketD, gint64 started, gint64 created, TGenPeer* peer);

TGenServer* tgenserver_new(in_port_t serverPort, TGenServer_notifyNewPeerFunc notify,
        gpointer data, GDestroyNotify destructData);
void tgenserver_ref(TGenServer* server);
void tgenserver_unref(TGenServer* server);

TGenIOResponse tgenserver_onEvent(TGenServer* server, gint descriptor, TGenEvent events);
gint tgenserver_getDescriptor(TGenServer* server);

#endif /* TGEN_SERVER_H_ */
