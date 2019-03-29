/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <fcntl.h>
#include <unistd.h>

#include "tgen.h"

struct _TGenServer {
    TGenServer_notifyNewPeerFunc notify;
    gpointer data;
    GDestroyNotify destructData;

    gint socketD;

    gint refcount;
    guint magic;
};

static gint _tgenserver_acceptPeer(TGenServer* server) {
    TGEN_ASSERT(server);

    /* we have a peer connecting to our listening socket */
    struct sockaddr_in peerAddress;
    memset(&peerAddress, 0, sizeof(struct sockaddr_in));
    socklen_t addressLength = (socklen_t)sizeof(struct sockaddr_in);

    gint64 started = g_get_monotonic_time();

    gint peerSocketD = accept(server->socketD, (struct sockaddr*)&peerAddress, &addressLength);

    if(peerSocketD >= 0) {
        /* make sure the socket is in nonblocking mode, otherwise a single peer could
         * block a server from processing other peers. */
        gint flags = fcntl(peerSocketD, F_GETFL, 0);
        if(flags < 0) {
            /* log the error, and then assume no flags were present */
            tgen_warning("Error in fcntl(F_GETFL) on socket %i (accepted from socket %i): "
                    "error %i: %s", peerSocketD, server->socketD, errno, g_strerror(errno));
            flags = 0;
        }

        /* make it nonblocking */
        flags = fcntl(peerSocketD, F_SETFL, flags | O_NONBLOCK);
        if(flags < 0) {
            /* log, but proceed anyway with the socket still in blocking mode */
            tgen_warning("Error in fcntl(F_SETFL) on socket %i (accepted from socket %i): "
                    "error %i: %s", peerSocketD, server->socketD, errno, g_strerror(errno));
        }

        gint64 created = g_get_monotonic_time();
        if(server->notify) {
            TGenPeer* peer = tgenpeer_newFromIP(peerAddress.sin_addr.s_addr, peerAddress.sin_port);

            /* someone is connecting to us, its ok to perform network lookups */
            tgenpeer_performLookups(peer);

            tgen_debug("Server listen socket %i accepted new peer %s on socket %i",
                    server->socketD, tgenpeer_toString(peer), peerSocketD)

            server->notify(server->data, peerSocketD, started, created, peer);
            tgenpeer_unref(peer);
        }
    }

    return peerSocketD;
}

TGenIOResponse tgenserver_onEvent(TGenServer* server, gint descriptor, TGenEvent events) {
    TGEN_ASSERT(server);

    g_assert((events & TGEN_EVENT_READ) && descriptor == server->socketD);

    gboolean blocked = FALSE;
    gint acceptedCount = 0;

    /* accept as many connections as we have available, until we get EWOULDBLOCK error */
    while(!blocked) {
        gint result = _tgenserver_acceptPeer(server);
        if(result < 0) {
            blocked = TRUE;

            if(errno != EWOULDBLOCK) {
                tgen_critical("accept(): socket %i returned %i error %i: %s",
                        server->socketD, result, errno, g_strerror(errno));
            }
        } else {
            acceptedCount++;
        }
    }

    tgen_debug("accepted %i peer connection(s), and now the listen port is blocked", acceptedCount);

    /* we will only ever accept and never write */
    TGenIOResponse response;
    memset(&response, 0, sizeof(TGenIOResponse));
    response.events = TGEN_EVENT_READ;
    return response;
}

TGenServer* tgenserver_new(in_port_t serverPort, TGenServer_notifyNewPeerFunc notify,
        gpointer data, GDestroyNotify destructData) {
    /* we run our protocol over a single server socket/port */
    gint socketD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socketD <= 0) {
        tgen_critical("socket(): returned %i error %i: %s", socketD, errno, g_strerror(errno));
        return NULL;
    }

    gint reuse = 1;
    gint result = setsockopt(socketD, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    if (result < 0) {
        tgen_critical("setsockopt(SO_REUSEADDR): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }

#ifdef SO_REUSEPORT
    result = setsockopt(socketD, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse));
    if (result < 0) {
        tgen_critical("setsockopt(SO_REUSEPORT): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }
#endif

    /* setup the listener address information */
    struct sockaddr_in listener;
    memset(&listener, 0, sizeof(struct sockaddr_in));
    listener.sin_family = AF_INET;
    gchar* tgenip = tgenconfig_getIP();
    if (tgenip != NULL) {
        listener.sin_addr.s_addr = inet_addr(tgenip);
    } else {
        listener.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    listener.sin_port = serverPort;

    /* bind the socket to the server port */
    result = bind(socketD, (struct sockaddr *) &listener, sizeof(listener));
    if (result < 0) {
        tgen_critical("bind(): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }

    /* set as server listening socket */
    result = listen(socketD, SOMAXCONN);
    if (result < 0) {
        tgen_critical("listen(): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }

    /* if we got here, everything worked correctly! */
    gchar ipStringBuffer[INET_ADDRSTRLEN + 1];
    memset(ipStringBuffer, 0, INET_ADDRSTRLEN + 1);
    inet_ntop(AF_INET, &listener.sin_addr.s_addr, ipStringBuffer, INET_ADDRSTRLEN);
    tgen_message("server listening at %s:%u", ipStringBuffer, ntohs(listener.sin_port));

    /* allocate the new server object and return it */
    TGenServer* server = g_new0(TGenServer, 1);
    server->magic = TGEN_MAGIC;
    server->refcount = 1;

    server->notify = notify;
    server->data = data;
    server->destructData = destructData;

    server->socketD = socketD;

    return server;
}

static void _tgenserver_free(TGenServer* server) {
    TGEN_ASSERT(server);
    g_assert(server->refcount == 0);

    if(server->socketD > 0) {
        close(server->socketD);
    }

    if(server->destructData && server->data) {
        server->destructData(server->data);
    }

    server->magic = 0;
    g_free(server);
}

void tgenserver_ref(TGenServer* server) {
    TGEN_ASSERT(server);
    server->refcount++;
}

void tgenserver_unref(TGenServer* server) {
    TGEN_ASSERT(server);
    if(--(server->refcount) <= 0) {
        _tgenserver_free(server);
    }
}

gint tgenserver_getDescriptor(TGenServer* server) {
    TGEN_ASSERT(server);
    return server->socketD;
}
