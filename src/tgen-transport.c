/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "tgen.h"

typedef enum {
    TGEN_XPORT_CONNECT,
    TGEN_XPORT_PROXY_INIT, TGEN_XPORT_PROXY_CHOICE,
    TGEN_XPORT_PROXY_AUTHREQUEST, TGEN_XPORT_PROXY_AUTHRESPONSE,
    TGEN_XPORT_PROXY_REQUEST, TGEN_XPORT_PROXY_RESPONSE_STATUS,
    TGEN_XPORT_PROXY_RESPONSE_TYPE, TGEN_XPORT_PROXY_RESPONSE_TYPE_IPV4,
    TGEN_XPORT_PROXY_RESPONSE_TYPE_NAME_LEN, TGEN_XPORT_PROXY_RESPONSE_TYPE_NAME,
    TGEN_XPORT_SUCCESSEOF, TGEN_XPORT_SUCCESSOPEN, TGEN_XPORT_ERROR
} TGenTransportState;

typedef enum {
    TGEN_XPORT_ERR_NONE, TGEN_XPORT_ERR_CONNECT,
    TGEN_XPORT_ERR_PROXY_CHOICE, TGEN_XPORT_ERR_PROXY_AUTH,
    TGEN_XPORT_ERR_PROXY_RECONN, TGEN_XPORT_ERR_PROXY_ADDR,
    TGEN_XPORT_ERR_PROXY_VERSION, TGEN_XPORT_ERR_PROXY_STATUS,
    TGEN_XPORT_ERR_WRITE, TGEN_XPORT_ERR_READ, TGEN_XPORT_ERR_MISC,
    TGEN_XPORT_ERR_STALLOUT, TGEN_XPORT_ERR_TIMEOUT
} TGenTransportError;

struct _TGenTransport {
    TGenTransportState state;
    TGenTransportError error;
    gchar* string;

    gint socketD;

    /* function/args to use when we send/recv bytes */
    NotifyBytesCallback bytesCB;

    /* our local socket, our side of the transport */
    TGenPeer* local;
    /* non-null if we need to connect through a proxy */
    TGenPeer* proxy;
    gchar* username;
    gchar* password;
    /* the remote side of the transport */
    TGenPeer* remote;

    /* track timings for time reporting, using g_get_monotonic_time in usec granularity */
    struct {
        gint64 start;
        gint64 socketCreate;
        gint64 socketConnect;
        gint64 proxyInit;
        gint64 proxyChoice;
        gint64 proxyRequest;
        gint64 proxyResponse;
        gint64 lastProgress;
    } time;

    /* a buffer used during the socks handshake */
    GString* socksBuffer;

    gint refcount;
    guint magic;
};

static const gchar* _tgentransport_stateToString(TGenTransportState state) {
    switch(state) {
        case TGEN_XPORT_CONNECT: {
            return "CONNECT";
        }
        case TGEN_XPORT_PROXY_INIT: {
            return "INIT";
        }
        case TGEN_XPORT_PROXY_CHOICE: {
            return "CHOICE";
        }
        case TGEN_XPORT_PROXY_REQUEST: {
            return "REQUEST";
        }
        case TGEN_XPORT_PROXY_AUTHREQUEST: {
            return "AUTH_REQUEST";
        }
        case TGEN_XPORT_PROXY_AUTHRESPONSE: {
            return "AUTH_RESPONSE";
        }
        case TGEN_XPORT_PROXY_RESPONSE_STATUS: {
            return "RESPONSE_STATUS";
        }
        case TGEN_XPORT_PROXY_RESPONSE_TYPE: {
            return "RESPONSE_TYPE";
        }
        case TGEN_XPORT_PROXY_RESPONSE_TYPE_IPV4: {
            return "RESPONSE_IPV4";
        }
        case TGEN_XPORT_PROXY_RESPONSE_TYPE_NAME_LEN: {
            return "RESPONSE_NAMELEN";
        }
        case TGEN_XPORT_PROXY_RESPONSE_TYPE_NAME: {
            return "RESPONSE_NAME";
        }
        case TGEN_XPORT_SUCCESSOPEN: {
            return "SUCCESS_OPEN";
        }
        case TGEN_XPORT_SUCCESSEOF: {
            return "SUCCESS_EOF";
        }
        case TGEN_XPORT_ERROR:
        default: {
            return "ERROR";
        }
    }
}

static const gchar* _tgentransport_errorToString(TGenTransportError error) {
    switch(error) {
        case TGEN_XPORT_ERR_NONE: {
            return "NONE";
        }
        case TGEN_XPORT_ERR_CONNECT: {
            return "CONNECT";
        }
        case TGEN_XPORT_ERR_PROXY_CHOICE: {
            return "CHOICE";
        }
        case TGEN_XPORT_ERR_PROXY_AUTH: {
            return "AUTH";
        }
        case TGEN_XPORT_ERR_PROXY_RECONN: {
            return "RECONN";
        }
        case TGEN_XPORT_ERR_PROXY_ADDR: {
            return "ADDR";
        }
        case TGEN_XPORT_ERR_PROXY_VERSION: {
            return "VERSION";
        }
        case TGEN_XPORT_ERR_PROXY_STATUS: {
            return "STATUS";
        }
        case TGEN_XPORT_ERR_WRITE: {
            return "WRITE";
        }
        case TGEN_XPORT_ERR_READ: {
            return "READ";
        }
        case TGEN_XPORT_ERR_STALLOUT: {
            return "STALLOUT";
        }
        case TGEN_XPORT_ERR_TIMEOUT: {
            return "TIMEOUT";
        }
        case TGEN_XPORT_ERR_MISC:
        default: {
            return "MISC";
        }
    }
}

const gchar* tgentransport_toString(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    if(!transport->string) {
        const gchar* stateStr = _tgentransport_stateToString(transport->state);
        const gchar* errorStr = _tgentransport_errorToString(transport->error);

        /* the following may be NULL, and these toString methods will handle it */
        const gchar* localStr = tgenpeer_toString(transport->local);
        const gchar* proxyStr = tgenpeer_toString(transport->proxy);
        const gchar* remoteStr = tgenpeer_toString(transport->remote);

        GString* buffer = g_string_new(NULL);

        g_string_printf(buffer, "[fd=%i,local=%s,proxy=%s,remote=%s,state=%s,error=%s]",
                transport->socketD, localStr, proxyStr, remoteStr, stateStr, errorStr);

        transport->string = g_string_free(buffer, FALSE);
    }

    return transport->string;
}

static void _tgentransport_resetString(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    if(transport->string) {
        g_free(transport->string);
        transport->string = NULL;
    }
}

static void _tgentransport_changeState(TGenTransport* transport, TGenTransportState state) {
    TGEN_ASSERT(transport);
    tgen_info("transport %s moving from state %s to state %s", tgentransport_toString(transport),
            _tgentransport_stateToString(transport->state), _tgentransport_stateToString(state));
    transport->state = state;
    _tgentransport_resetString(transport);
}

static void _tgentransport_changeError(TGenTransport* transport, TGenTransportError error) {
    TGEN_ASSERT(transport);
    tgen_info("transport %s moving from error %s to error %s", tgentransport_toString(transport),
            _tgentransport_errorToString(transport->error), _tgentransport_errorToString(error));
    transport->error = error;
    _tgentransport_resetString(transport);
}

static TGenTransport* _tgentransport_newHelper(gint socketD, gint64 startedTime, gint64 createdTime,
        TGenPeer* proxy, const gchar* username, const gchar* password, TGenPeer* peer,
        NotifyBytesCallback bytesCB) {
    TGenTransport* transport = g_new0(TGenTransport, 1);
    transport->magic = TGEN_MAGIC;
    transport->refcount = 1;

    transport->socketD = socketD;

    if(peer) {
        transport->remote = peer;
        tgenpeer_ref(peer);
    }
    if(proxy) {
        transport->proxy = proxy;
        tgenpeer_ref(proxy);

        tgen_info("Initiated transport to socks proxy at %s", tgenpeer_toString(transport->proxy));

        if(username) {
            transport->username = g_strdup(username);
        }
        if(password) {
            transport->password = g_strdup(password);
        }

        if(username != NULL || password != NULL) {
            tgen_info("Configured to use proxy authentication with username='%s' and password='%s'",
                    username ? username : "", password ? password : "");
        }
    }

    struct sockaddr_in addrBuf = {0};
    socklen_t addrBufLen = (socklen_t)sizeof(struct sockaddr_in);
    if(getsockname(socketD, (struct sockaddr*) &addrBuf, &addrBufLen) == 0) {
        transport->local = tgenpeer_newFromIP(addrBuf.sin_addr.s_addr, addrBuf.sin_port);
    }

    transport->time.start = startedTime;
    transport->time.socketCreate = createdTime;
    transport->time.socketConnect = -1;
    transport->time.proxyInit = -1;
    transport->time.proxyChoice = -1;
    transport->time.proxyRequest = -1;
    transport->time.proxyResponse = -1;

    transport->time.lastProgress = createdTime;

    transport->bytesCB = bytesCB;
    if(transport->bytesCB.arg && transport->bytesCB.argRef) {
        transport->bytesCB.argRef(transport->bytesCB.arg);
    }

    return transport;
}

static TGenPeer* _tgentransport_getProxyFromEnvHelper(){
    gchar* socksProxyStr = tgenconfig_getSOCKS();

    if(!socksProxyStr) {
        return NULL;
    }

    TGenOptionPeer peerOption = {0};

    GError* error = tgenoptionparser_parsePeer("TGENSOCKS", socksProxyStr, &peerOption);
    if(error) {
        tgen_warning("Error while parsing TGENSOCKS string: error %i: %s", error->code, error->message);
        g_error_free(error);
        return NULL;
    }

    if(peerOption.isSet) {
        /* this peer needs to be freed by the caller */
        return peerOption.value;
    } else {
        return NULL;
    }
}

TGenTransport* tgentransport_newActive(TGenStreamOptions* options, NotifyBytesCallback bytesCB,
        TGenPeer* socksProxy, const gchar* socksUsername, const gchar* socksPassword) {
    /* get the ultimate destination */
    TGenPeer* peer = options->peers.isSet ? tgenpool_getRandom(options->peers.value) : NULL;
    if(!peer) {
        tgen_error("Transport was created with no viable peers");
        return NULL;
    }

    gint64 started = g_get_monotonic_time();

    /* create the socket and get a socket descriptor */
    gint socketD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    gint64 created = g_get_monotonic_time();

    if (socketD < 0) {
        tgen_critical("socket(): returned %i error %i: %s",
                socketD, errno, g_strerror(errno));
        return NULL;
    }

    gchar* tgenip = tgenconfig_getIP();
    /* bind()'ing here is only neccessary if we specify our IP */
    if (tgenip != NULL) {
        struct sockaddr_in localaddr = {0};
        localaddr.sin_family = AF_INET;
        localaddr.sin_addr.s_addr = inet_addr(tgenip);
        localaddr.sin_port = 0;
        gint result = bind(socketD, (struct sockaddr *) &localaddr, sizeof(localaddr));

        if (result < 0) {
            tgen_critical("bind(): socket %i returned %i error %i: %s",
                    socketD, result, errno, g_strerror(errno));
            close(socketD);
            return NULL;
        }
    }

    /* connect to another host */
    struct sockaddr_in master = {0};
    master.sin_family = AF_INET;

    /* if there is a proxy, we connect there; otherwise connect to the peer */
    TGenPeer* envProxy = _tgentransport_getProxyFromEnvHelper();
    TGenPeer* proxy = envProxy ? envProxy : socksProxy;
    TGenPeer* connectee = proxy ? proxy : peer;

    /* its safe to do lookups on whoever we are directly connecting to. */
    tgenpeer_performLookups(connectee);

    master.sin_addr.s_addr = tgenpeer_getNetworkIP(connectee);
    master.sin_port = tgenpeer_getNetworkPort(connectee);

    gint result = connect(socketD, (struct sockaddr *) &master, sizeof(master));

    /* nonblocking sockets means inprogress is ok */
    if (result < 0 && errno != EINPROGRESS) {
        tgen_critical("connect(): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }

    const gchar* username = (proxy && socksUsername) ? socksUsername : NULL;
    const gchar* password = (proxy && socksPassword) ? socksPassword : NULL;

    TGenTransport* transport = _tgentransport_newHelper(socketD, started, created,
            proxy, username, password, peer, bytesCB);

    if(envProxy) {
        tgenpeer_unref(envProxy);
    }

    return transport;
}

TGenTransport* tgentransport_newPassive(gint socketD, gint64 started, gint64 created, TGenPeer* peer,
        NotifyBytesCallback bytesCB) {
    return _tgentransport_newHelper(socketD, started, created, NULL, NULL, NULL, peer, bytesCB);
}

static void _tgentransport_free(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    if(transport->socketD > 0) {
        if(transport->error == TGEN_XPORT_ERR_NONE) {
            tgen_debug("Calling shutdown() on transport %s", tgentransport_toString(transport));
            shutdown(transport->socketD, SHUT_RDWR);
        }
        tgen_debug("Calling close() on transport %s", tgentransport_toString(transport));
        close(transport->socketD);
    }

    if(transport->string) {
        g_free(transport->string);
    }

    if(transport->remote) {
        tgenpeer_unref(transport->remote);
    }

    if(transport->proxy) {
        tgenpeer_unref(transport->proxy);
    }

    if(transport->local) {
        tgenpeer_unref(transport->local);
    }

    if(transport->socksBuffer) {
        g_string_free(transport->socksBuffer, TRUE);
    }

    if(transport->bytesCB.arg && transport->bytesCB.argUnref) {
        transport->bytesCB.argUnref(transport->bytesCB.arg);
    }

    if(transport->username) {
        g_free(transport->username);
    }

    if(transport->password) {
        g_free(transport->password);
    }

    transport->magic = 0;
    g_free(transport);
}

void tgentransport_ref(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    transport->refcount++;
}

void tgentransport_unref(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    transport->refcount--;
    if(transport->refcount == 0) {
        _tgentransport_free(transport);
    }
}

void tgentransport_shutdownWrites(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    if(transport->socketD > 0) {
//        Tor will close the circuit if we call shutdown
//        Once they implement RELAY_FIN cells, we can uncomment this
//        shutdown(transport->socketD, SHUT_WR);
    }
}

gssize tgentransport_write(TGenTransport* transport, gpointer buffer, gsize length) {
    TGEN_ASSERT(transport);

    gssize bytes = write(transport->socketD, buffer, length);

    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        tgen_info("%s write(): write to socket %i returned %"G_GSSIZE_FORMAT" error %i: %s",
                tgentransport_toString(transport),
                transport->socketD, bytes, errno, g_strerror(errno));
        _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
        _tgentransport_changeError(transport, TGEN_XPORT_ERR_WRITE);
    } else if(bytes == 0) {
        tgen_info("%s write(): socket %i closed unexpectedly",
                tgentransport_toString(transport), transport->socketD);
        _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
        _tgentransport_changeError(transport, TGEN_XPORT_ERR_WRITE);
    }

    if(bytes > 0 && transport->bytesCB.func) {
        transport->bytesCB.func(transport->bytesCB.arg, 0, (gsize)bytes);
    }

    return bytes;
}

gssize tgentransport_read(TGenTransport* transport, gpointer buffer, gsize length) {
    TGEN_ASSERT(transport);

    gssize bytes = read(transport->socketD, buffer, length);

    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        tgen_info("%s read(): read from socket %i returned %"G_GSSIZE_FORMAT" error %i: %s",
                tgentransport_toString(transport),
                transport->socketD, bytes, errno, g_strerror(errno));
        _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
        _tgentransport_changeError(transport, TGEN_XPORT_ERR_READ);
    } else if(bytes == 0) {
        tgen_info("%s read(): read eof on socket %i",
                tgentransport_toString(transport), transport->socketD);
        if(transport->state == TGEN_XPORT_SUCCESSOPEN) {
            _tgentransport_changeState(transport, TGEN_XPORT_SUCCESSEOF);
        } else {
            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_READ);
        }
    }

    if(bytes > 0 && transport->bytesCB.func) {
        transport->bytesCB.func(transport->bytesCB.arg, (gsize)bytes, 0);
    }

    return bytes;
}

gint tgentransport_getDescriptor(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    return transport->socketD;
}

gchar* tgentransport_getTimeStatusReport(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    gint64 create = (transport->time.socketCreate >= 0 && transport->time.start >= 0) ?
            (transport->time.socketCreate - transport->time.start) : -1;
    gint64 connect = (transport->time.socketConnect >= 0 && transport->time.start >= 0) ?
            (transport->time.socketConnect - transport->time.start) : -1;
    gint64 init = (transport->time.proxyInit >= 0 && transport->time.start >= 0) ?
            (transport->time.proxyInit - transport->time.start) : -1;
    gint64 choice = (transport->time.proxyChoice >= 0 && transport->time.start >= 0) ?
            (transport->time.proxyChoice - transport->time.start) : -1;
    gint64 request = (transport->time.proxyRequest >= 0 && transport->time.start >= 0) ?
            (transport->time.proxyRequest - transport->time.start) : -1;
    gint64 response = (transport->time.proxyResponse >= 0 && transport->time.start >= 0) ?
            (transport->time.proxyResponse - transport->time.start) : -1;

    GString* buffer = g_string_new(NULL);

    /* print the times in milliseconds */
    g_string_printf(buffer,
            "usecs-to-socket-create=%"G_GINT64_FORMAT",usecs-to-socket-connect=%"G_GINT64_FORMAT","
            "usecs-to-proxy-init=%"G_GINT64_FORMAT",usecs-to-proxy-choice=%"G_GINT64_FORMAT","
            "usecs-to-proxy-request=%"G_GINT64_FORMAT",usecs-to-proxy-response=%"G_GINT64_FORMAT,
            create, connect, init, choice, request, response);

    return g_string_free(buffer, FALSE);
}

gboolean tgentransport_wantsEvents(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    if(transport->state != TGEN_XPORT_SUCCESSOPEN &&
            transport->state != TGEN_XPORT_SUCCESSEOF && transport->state != TGEN_XPORT_ERROR) {
        return TRUE;
    }
    return FALSE;
}

static TGenEvent _tgentransport_sendSocksInit(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    /*
    1 socks init client --> server
    \x05 (version 5)
    \x01 (1 supported auth method)
    \x?? method is \x00 "no auth" or \x02 user/pass if configured
    */

    if(!transport->socksBuffer) {
        /* use g_string_new_len to make sure the NULL gets written */
        if(transport->username || transport->password) {
            transport->socksBuffer = g_string_new_len("\x05\x01\x02", 3);
        } else {
            transport->socksBuffer = g_string_new_len("\x05\x01\x00", 3);
        }
        g_assert(transport->socksBuffer->len == 3);
    }

    gssize bytesSent = tgentransport_write(transport, transport->socksBuffer->str, transport->socksBuffer->len);

    if(bytesSent <= 0 || bytesSent > transport->socksBuffer->len) {
        /* there was an error of some kind */
        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;
        return TGEN_EVENT_NONE;
    } else {
        /* we sent some bytes */
        transport->socksBuffer = g_string_erase(transport->socksBuffer, 0, bytesSent);

        /* after writing, we may not have written it all and may have some remaining */
        if(transport->socksBuffer->len > 0) {
            /* we still have more to send later */
            tgen_debug("sent partial socks init to proxy %s", tgenpeer_toString(transport->proxy));
            return TGEN_EVENT_WRITE;
        } else {
            /* we wrote it all, we can move on */
            transport->time.proxyInit = g_get_monotonic_time();
            transport->time.lastProgress = transport->time.proxyInit;
            tgen_debug("sent socks init to proxy %s", tgenpeer_toString(transport->proxy));

            g_string_free(transport->socksBuffer, TRUE);
            transport->socksBuffer = NULL;

            /* the next step is to read the choice from the server */
            _tgentransport_changeState(transport, TGEN_XPORT_PROXY_CHOICE);
            return TGEN_EVENT_READ;
        }
    }
}

static void _tgentransport_socksReceiveHelper(TGenTransport* transport, gsize requestedAmount) {
    if(!transport->socksBuffer) {
        transport->socksBuffer = g_string_new(NULL);
    }
    g_assert(transport->socksBuffer->len <= requestedAmount);

    gsize readAmount = (gsize)(requestedAmount - transport->socksBuffer->len);

    gchar buffer[readAmount];
    memset(buffer, 0, readAmount);
    gssize bytesReceived = tgentransport_read(transport, buffer, readAmount);

    if(bytesReceived <= 0 || bytesReceived > readAmount) {
        /* there was an error of some kind */
        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;
    } else {
        /* we read some bytes */
        transport->socksBuffer = g_string_append_len(transport->socksBuffer, buffer, bytesReceived);
    }
}

static TGenEvent _tgentransport_receiveSocksChoice(TGenTransport* transport) {
    /*
    2 socks choice client <-- server
    \x05 (version 5)
    \x00 (auth method choice - \xFF means none supported)
    */

    _tgentransport_socksReceiveHelper(transport, 2);

    if(!transport->socksBuffer) {
        /* there was an error of some kind */
        return TGEN_EVENT_NONE;
    } else if(transport->socksBuffer->len < 2) {
        /* we did not get all of the data yet */
        tgen_debug("received partial socks choice from proxy %s", tgenpeer_toString(transport->proxy));
        return TGEN_EVENT_READ;
    } else {
        /* we read it all, we can move on */
        transport->time.proxyChoice = g_get_monotonic_time();
        transport->time.lastProgress = transport->time.proxyChoice;

        gboolean versionSupported = (transport->socksBuffer->str[0] == 0x05) ? TRUE : FALSE;
        gboolean authSupported = FALSE;
        if(transport->username || transport->password) {
            authSupported = (transport->socksBuffer->str[1] == 0x02) ? TRUE : FALSE;
            if(authSupported) {
                tgen_debug("Proxy supports username/password authenticated");
            }
        } else {
            authSupported = (transport->socksBuffer->str[1] == 0x00) ? TRUE : FALSE;
            if(authSupported) {
                tgen_debug("Proxy supports no authenticated");
            }
        }

        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;

        if(versionSupported && authSupported) {
            tgen_debug("socks choice supported by proxy %s", tgenpeer_toString(transport->proxy));

            if(transport->username || transport->password) {
                /* try to authenticate */
                _tgentransport_changeState(transport, TGEN_XPORT_PROXY_AUTHREQUEST);
            } else {
                /* go straight to request */
                _tgentransport_changeState(transport, TGEN_XPORT_PROXY_REQUEST);
            }

            return TGEN_EVENT_WRITE;
        } else {
            tgen_info("socks choice unsupported by proxy %s", tgenpeer_toString(transport->proxy));

            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_CHOICE);
            return TGEN_EVENT_NONE;
        }
    }
}

static guint8 _tgentransport_getTruncatedStrLen(gchar* str) {
    guint8 guint8max = -1;
    glong len = g_utf8_strlen(str, -1);

    if(len > guint8max) {
        tgen_warning("truncated string '%s' in proxy handshake from %li to %u bytes",
                str, len, (guint)guint8max);
        return guint8max;
    } else {
        return (guint8)len;
    }
}

static TGenEvent _tgentransport_sendSocksAuth(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    /*
    2.5a socks auth request client --> server
    \x01 (user/pass auth version)
    uint8 username length (1 byte)
    (username) (1-255 bytes)
    uint8 password length (1 byte)
    (password) (1-255 bytes)
    */

    if(!transport->socksBuffer) {
        guint8 userlen = transport->username ? _tgentransport_getTruncatedStrLen(transport->username) : 1;
        gchar* user = transport->username ? transport->username : "\x00";
        guint8 passlen = transport->password ? _tgentransport_getTruncatedStrLen(transport->password) : 1;
        gchar* pass = transport->password ? transport->password : "\x00";

        gchar buffer[255+255+3] = {0};

        memmove(&buffer[0], "\x01", 1);
        memmove(&buffer[1], &userlen, 1);
        memmove(&buffer[2], user, userlen);
        memmove(&buffer[2+userlen], &passlen, 1);
        memmove(&buffer[3+userlen], pass, passlen);

        /* use g_string_new_len to make sure the NULL gets written */
        transport->socksBuffer = g_string_new_len(&buffer[0], (gssize)3+userlen+passlen);
        g_assert(transport->socksBuffer->len == (gssize)3+userlen+passlen);
    }

    gssize bytesSent = tgentransport_write(transport, transport->socksBuffer->str, transport->socksBuffer->len);

    if(bytesSent <= 0 || bytesSent > transport->socksBuffer->len) {
        /* there was an error of some kind */
        tgen_debug("there was an error when trying to send socks auth request");
        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;
        return TGEN_EVENT_NONE;
    } else {
        /* we sent some bytes */
        transport->socksBuffer = g_string_erase(transport->socksBuffer, 0, bytesSent);

        /* after writing, we may not have written it all and may have some remaining */
        if(transport->socksBuffer->len > 0) {
            /* we still have more to send later */
            tgen_debug("sent partial socks authentication request to proxy %s",
                    tgenpeer_toString(transport->proxy));
            return TGEN_EVENT_WRITE;
        } else {
            /* we wrote it all, we can move on */
            tgen_debug("sent socks authentication request to proxy %s",
                    tgenpeer_toString(transport->proxy));

            g_string_free(transport->socksBuffer, TRUE);
            transport->socksBuffer = NULL;

            /* the next step is to read the auth response from the server */
            _tgentransport_changeState(transport, TGEN_XPORT_PROXY_AUTHRESPONSE);
            return TGEN_EVENT_READ;
        }
    }
}

static TGenEvent _tgentransport_receiveSocksAuth(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    /*
    2.5b socks auth response client <-- server
    \x01 (user/pass auth version)
    \x00 (1 byte status, 00 for success otherwise fail)
    */

    _tgentransport_socksReceiveHelper(transport, 2);

    if(!transport->socksBuffer) {
        /* there was an error of some kind */
        return TGEN_EVENT_NONE;
    } else if(transport->socksBuffer->len < 2) {
        /* we did not get all of the data yet */
        tgen_debug("received partial socks auth response from proxy %s", tgenpeer_toString(transport->proxy));
        return TGEN_EVENT_READ;
    } else {
        /* we read it all, we can move on */
        int version = transport->socksBuffer->str[0];
        gboolean versionMatch = (version == 0x01) ? TRUE : FALSE;
        gboolean authSuccess = (transport->socksBuffer->str[1] == 0x00) ? TRUE : FALSE;

        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;

        if (!versionMatch) {
            tgen_warning("socks server %s returned unexpected version %d", 
                    tgenpeer_toString(transport->proxy), version);
            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_VERSION);
            return TGEN_EVENT_NONE;
        }

        if (!authSuccess){
            tgen_warning("socks server %s authentication failed with username='%s' and password='%s'",
                    tgenpeer_toString(transport->proxy),
                    transport->username ? transport->username : "",
                    transport->password ? transport->password : "");

            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_AUTH);
            return TGEN_EVENT_NONE;
        }

        tgen_info("socks server %s authentication succeeded with username='%s' and password='%s'",
                tgenpeer_toString(transport->proxy),
                transport->username ? transport->username : "",
                transport->password ? transport->password : "");

        /* now we can move on to the request */
        _tgentransport_changeState(transport, TGEN_XPORT_PROXY_REQUEST);
        return TGEN_EVENT_WRITE;
    }
}

static TGenEvent _tgentransport_sendSocksRequest(TGenTransport* transport) {
    /*
    3 socks request client --> server
    \x05 (version 5)
    \x01 (tcp stream)
    \x00 (reserved)

    the client asks the server to connect to a remote

    3a ip address client --> server
    \x01 (ipv4)
    in_addr_t (4 bytes)
    in_port_t (2 bytes)

    3b hostname client --> server
    \x03 (domain name)
    \x__ (1 byte name len)
    (name)
    in_port_t (2 bytes)
    */

    /* set up the request buffer */
    if(!transport->socksBuffer) {
        /* prefer name mode if we have it, and let the proxy lookup IP as needed */
        const gchar* name = tgenpeer_getName(transport->remote);
        if(name && g_str_has_suffix(name, ".onion")) { // FIXME remove suffix matching to have proxy do lookup for us
            /* case 3b - domain name */
            glong nameLength = g_utf8_strlen(name, -1);
            guint8 guint8max = -1;
            if(nameLength > guint8max) {
                nameLength = (glong)guint8max;
                tgen_warning("truncated name '%s' in socks request from %ld to %u bytes",
                        name, nameLength, (guint)guint8max);
            }

            in_addr_t port = tgenpeer_getNetworkPort(transport->remote);

            gchar buffer[nameLength+8];
            memset(buffer, 0, nameLength+8);

            memmove(&buffer[0], "\x05\x01\x00\x03", 4);
            memmove(&buffer[4], &nameLength, 1);
            memmove(&buffer[5], name, nameLength);
            memmove(&buffer[5+nameLength], &port, 2);

            /* use g_string_new_len to make sure the NULL gets written */
            transport->socksBuffer = g_string_new_len(&buffer[0], nameLength+7);
            g_assert(transport->socksBuffer->len == nameLength+7);
        } else {
            tgenpeer_performLookups(transport->remote); // FIXME remove this to have proxy do lookup for us
            /* case 3a - IPv4 */
            in_addr_t ip = tgenpeer_getNetworkIP(transport->remote);
            in_addr_t port = tgenpeer_getNetworkPort(transport->remote);

            gchar buffer[16] = {0};

            memmove(&buffer[0], "\x05\x01\x00\x01", 4);
            memmove(&buffer[4], &ip, 4);
            memmove(&buffer[8], &port, 2);

            transport->socksBuffer = g_string_new_len(&buffer[0], 10);
            g_assert(transport->socksBuffer->len == 10);
        }
    }

    gssize bytesSent = tgentransport_write(transport, transport->socksBuffer->str, transport->socksBuffer->len);

    if(bytesSent <= 0 || bytesSent > transport->socksBuffer->len) {
        /* there was an error of some kind */
        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;
        return TGEN_EVENT_NONE;
    } else {
        /* we sent some bytes */
        transport->socksBuffer = g_string_erase(transport->socksBuffer, 0, bytesSent);

        /* after writing, we may not have written it all and may have some remaining */
        if(transport->socksBuffer->len > 0) {
            /* we still have more to send later */
            tgen_debug("sent partial socks request to proxy %s", tgenpeer_toString(transport->proxy));
            return TGEN_EVENT_WRITE;
        } else {
            /* we wrote it all, we can move on */
            transport->time.proxyRequest = g_get_monotonic_time();
            transport->time.lastProgress = transport->time.proxyRequest;
            tgen_debug("requested connection from %s through socks proxy %s to remote %s",
                    tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote));

            g_string_free(transport->socksBuffer, TRUE);
            transport->socksBuffer = NULL;

            /* the next step is to read the response from the server */
            _tgentransport_changeState(transport, TGEN_XPORT_PROXY_RESPONSE_STATUS);
            return TGEN_EVENT_READ;
        }
    }
}

static TGenEvent _tgentransport_receiveSocksResponseTypeName(TGenTransport* transport) {
    /* case 4b - domain name mode */
    guint8 nameLength = 0;
    memmove(&nameLength, &transport->socksBuffer->str[0], 1);

    /* len is left over from prev read - now we want to read the name+port so
     * that we have the full len+name+port */
    _tgentransport_socksReceiveHelper(transport, nameLength+3);

    if(!transport->socksBuffer) {
        /* there was an error of some kind */
        return TGEN_EVENT_NONE;
    } else if(transport->socksBuffer->len < nameLength+3) {
        /* we did not get all of the data yet */
        tgen_debug("received partial socks response from proxy %s", tgenpeer_toString(transport->proxy));
        return TGEN_EVENT_READ;
    } else {
        gchar namebuf[nameLength+1];
        memset(namebuf, 0, nameLength+1);
        in_port_t socksBindPort = 0;

        memmove(namebuf, &transport->socksBuffer->str[1], nameLength);
        memmove(&socksBindPort, &transport->socksBuffer->str[1+nameLength], 2);

        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;

        if(!g_ascii_strncasecmp(namebuf, "\0", (gsize) 1) && socksBindPort == 0) {
            tgen_info("connection from %s through socks proxy %s to %s successful",
                    tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote));

            transport->time.proxyResponse = g_get_monotonic_time();
            transport->time.lastProgress = transport->time.proxyResponse;
            _tgentransport_changeState(transport, TGEN_XPORT_SUCCESSOPEN);
            return TGEN_EVENT_DONE;
        } else {
            tgen_warning("connection from %s through socks proxy %s to %s failed: "
                    "proxy requested unsupported reconnection to %s:%u",
                    tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote),
                    namebuf, (guint)ntohs(socksBindPort));

            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_RECONN);
            return TGEN_EVENT_NONE;
        }
    }
}

static TGenEvent _tgentransport_receiveSocksResponseTypeNameLen(TGenTransport* transport) {
    /* case 4b - domain name mode */
    _tgentransport_socksReceiveHelper(transport, 1);

    if(!transport->socksBuffer) {
        /* there was an error of some kind */
        return TGEN_EVENT_NONE;
    } else if(transport->socksBuffer->len < 1) {
        /* we did not get all of the data yet */
        tgen_debug("received partial socks response from proxy %s", tgenpeer_toString(transport->proxy));
        return TGEN_EVENT_READ;
    } else {
        _tgentransport_changeState(transport, TGEN_XPORT_PROXY_RESPONSE_TYPE_NAME);
        return _tgentransport_receiveSocksResponseTypeName(transport);
    }
}

static TGenEvent _tgentransport_receiveSocksResponseTypeIPv4(TGenTransport* transport) {
    /* case 4a - IPV4 mode - get address server told us */
    _tgentransport_socksReceiveHelper(transport, 6);

    if(!transport->socksBuffer) {
        /* there was an error of some kind */
        return TGEN_EVENT_NONE;
    } else if(transport->socksBuffer->len < 6) {
        /* we did not get all of the data yet */
        tgen_debug("received partial socks response from proxy %s", tgenpeer_toString(transport->proxy));
        return TGEN_EVENT_READ;
    } else {
        /* check if they want us to connect elsewhere */
        in_addr_t socksBindAddress = 0;
        in_port_t socksBindPort = 0;
        memmove(&socksBindAddress, &transport->socksBuffer->str[0], 4);
        memmove(&socksBindPort, &transport->socksBuffer->str[4], 2);

        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;

        /* reconnect not supported */
        if(socksBindAddress == 0 && socksBindPort == 0) {
            tgen_info("connection from %s through socks proxy %s to %s successful",
                    tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote));

            transport->time.proxyResponse = g_get_monotonic_time();
            transport->time.lastProgress = transport->time.proxyResponse;
            _tgentransport_changeState(transport, TGEN_XPORT_SUCCESSOPEN);
            return TGEN_EVENT_DONE;
        } else {
            tgen_warning("connection from %s through socks proxy %s to %s failed: "
                    "proxy requested unsupported reconnection to %i:%u",
                    tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote),
                    (gint)socksBindAddress, (guint)ntohs(socksBindPort));

            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_RECONN);
            return TGEN_EVENT_NONE;
        }
    }
}

static TGenEvent _tgentransport_receiveSocksResponseType(TGenTransport* transport) {
    _tgentransport_socksReceiveHelper(transport, 2);

    if(!transport->socksBuffer) {
        /* there was an error of some kind */
        return TGEN_EVENT_NONE;
    } else if(transport->socksBuffer->len < 2) {
        /* we did not get all of the data yet */
        tgen_debug("received partial socks response from proxy %s", tgenpeer_toString(transport->proxy));
        return TGEN_EVENT_READ;
    } else {
        gchar reserved = transport->socksBuffer->str[0];
        gchar addressType = transport->socksBuffer->str[1];

        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;

        if(addressType == 0x01) {
            _tgentransport_changeState(transport, TGEN_XPORT_PROXY_RESPONSE_TYPE_IPV4);
            return _tgentransport_receiveSocksResponseTypeIPv4(transport);
        } else if (addressType == 0x03) {
            _tgentransport_changeState(transport, TGEN_XPORT_PROXY_RESPONSE_TYPE_NAME_LEN);
            return _tgentransport_receiveSocksResponseTypeNameLen(transport);
        } else {
            tgen_warning("connection from %s through socks proxy %s to %s failed: unsupported address type 0x%X",
                    tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote),
                    addressType);

            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_ADDR);
            return TGEN_EVENT_NONE;
        }
    }
}

static TGenEvent _tgentransport_receiveSocksResponseStatus(TGenTransport* transport) {
    /*
    4 socks response client <-- server
    \x05 (version 5)
    \x00 (request granted)
    \x00 (reserved)

    the server can tell us that we need to reconnect elsewhere

    4a ip address client <-- server
    \x01 (ipv4)
    in_addr_t (4 bytes)
    in_port_t (2 bytes)

    4b hostname client <-- server
    \x03 (domain name)
    \x__ (1 byte name len)
    (name)
    in_port_t (2 bytes)
     */

    _tgentransport_socksReceiveHelper(transport, 2);

    if(!transport->socksBuffer) {
        /* there was an error of some kind */
        return TGEN_EVENT_NONE;
    } else if(transport->socksBuffer->len < 2) {
        /* we did not get all of the data yet */
        tgen_debug("received partial socks response from proxy %s", tgenpeer_toString(transport->proxy));
        return TGEN_EVENT_READ;
    } else {
        gchar version = transport->socksBuffer->str[0];
        guchar status = transport->socksBuffer->str[1];

        g_string_free(transport->socksBuffer, TRUE);
        transport->socksBuffer = NULL;

        if(version == 0x05 && status == 0x00) {
            _tgentransport_changeState(transport, TGEN_XPORT_PROXY_RESPONSE_TYPE);
            return _tgentransport_receiveSocksResponseType(transport);
        } else {
            GString* messageBuffer = g_string_new(NULL);

            g_string_append_printf(messageBuffer,
                    "connection from %s through socks proxy %s to %s failed: ",
                    tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy),
                    tgenpeer_toString(transport->remote));

            if(version != 0x05) {
                /* version failure, version must be 5 */
                g_string_append_printf(messageBuffer, "we support version 5 but "
                        "the SOCKS server wants version %X", version);
            } else {
                /* status failure, status must be 0 (request granted) */
                g_string_append_printf(messageBuffer, "our request was not granted by "
                        "the SOCKS server: error status %X: ", status);

                /* error codes: https://en.wikipedia.org/wiki/SOCKS#SOCKS5
                 * we also include non-standard tor error codes (enabled with the 'ExtendedErrors'
                 * torrc flag), which could potentially have different meanings for non-tor
                 * proxies */
                switch(status) {
                    case 0x01: {
                        g_string_append_printf(messageBuffer, "general failure");
                        break;
                    }
                    case 0x02: {
                        g_string_append_printf(messageBuffer, "connection not allowed by ruleset");
                        break;
                    }
                    case 0x03: {
                        g_string_append_printf(messageBuffer, "network unreachable");
                        break;
                    }
                    case 0x04: {
                        g_string_append_printf(messageBuffer, "host unreachable");
                        break;
                    }
                    case 0x05: {
                        g_string_append_printf(messageBuffer, "connection refused by destination host");
                        break;
                    }
                    case 0x06: {
                        g_string_append_printf(messageBuffer, "TTL expired");
                        break;
                    }
                    case 0x07: {
                        g_string_append_printf(messageBuffer, "command not supported / protocol error");
                        break;
                    }
                    case 0x08: {
                        g_string_append_printf(messageBuffer, "address type not supported");
                        break;
                    }
                    case 0xF0: {
                        g_string_append_printf(messageBuffer, "(tor) onion service descriptor can not be found");
                        break;
                    }
                    case 0xF1: {
                        g_string_append_printf(messageBuffer, "(tor) onion service descriptor is invalid");
                        break;
                    }
                    case 0xF2: {
                        g_string_append_printf(messageBuffer, "(tor) onion service introduction failed");
                        break;
                    }
                    case 0xF3: {
                        g_string_append_printf(messageBuffer, "(tor) onion service rendezvous failed");
                        break;
                    }
                    case 0xF4: {
                        g_string_append_printf(messageBuffer, "(tor) onion service missing client authorization");
                        break;
                    }
                    case 0xF5: {
                        g_string_append_printf(messageBuffer, "(tor) onion service wrong client authorization");
                        break;
                    }
                    case 0xF6: {
                        g_string_append_printf(messageBuffer, "(tor) onion service invalid address");
                        break;
                    }
                    case 0xF7: {
                        g_string_append_printf(messageBuffer, "(tor) onion service introduction timed out");
                        break;
                    }
                    default: {
                        g_string_append_printf(messageBuffer, "unknown error");
                        break;
                    }
                }
            }

            tgen_warning("%s", messageBuffer->str);
            g_string_free(messageBuffer, TRUE);

            TGenTransportError error = (version != 0x05) ? TGEN_XPORT_ERR_PROXY_VERSION : TGEN_XPORT_ERR_PROXY_STATUS;
            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, error);
            return TGEN_EVENT_NONE;
        }
    }
}

TGenEvent tgentransport_onEvent(TGenTransport* transport, TGenEvent events) {
    TGEN_ASSERT(transport);

    /* When we first connected, we got EINPROGRESS. If that connected then failed,
     * epoll will tell us by setting TGEN_EVENT_DONE */
    if(events & TGEN_EVENT_DONE) {
        if(transport->state == TGEN_XPORT_CONNECT) {
            _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_CONNECT);
        }
    }

    /* return TGEN_EVENT_NONE to indicate an error, or TGEN_EVENT_DONE to indicate that the socket
     * is ready for a transfer to start (SOCKS is connected, if necessary) */

    switch(transport->state) {
    case TGEN_XPORT_CONNECT: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            /* we are now connected and can send the socks init */
            transport->time.socketConnect = g_get_monotonic_time();
            transport->time.lastProgress = transport->time.socketConnect;
            if(transport->proxy) {
                /* continue with SOCKS handshake next */
                _tgentransport_changeState(transport, TGEN_XPORT_PROXY_INIT);
                /* process the next step */
                return tgentransport_onEvent(transport, events);
            } else {
                /* no proxy, this is a direct connection, we are all done */
                _tgentransport_changeState(transport, TGEN_XPORT_SUCCESSOPEN);
                return TGEN_EVENT_DONE;
            }
        }
    }

    case TGEN_XPORT_PROXY_INIT: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            return _tgentransport_sendSocksInit(transport);
        }
    }

    case TGEN_XPORT_PROXY_CHOICE: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksChoice(transport);
        }
    }

    case TGEN_XPORT_PROXY_AUTHREQUEST: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            return _tgentransport_sendSocksAuth(transport);
        }
    }

    case TGEN_XPORT_PROXY_AUTHRESPONSE: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksAuth(transport);
        }
    }

    case TGEN_XPORT_PROXY_REQUEST: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            return _tgentransport_sendSocksRequest(transport);
        }
    }

    case TGEN_XPORT_PROXY_RESPONSE_STATUS: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksResponseStatus(transport);
        }
    }

    case TGEN_XPORT_PROXY_RESPONSE_TYPE: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksResponseType(transport);
        }
    }

    case TGEN_XPORT_PROXY_RESPONSE_TYPE_IPV4: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksResponseTypeIPv4(transport);
        }
    }

    case TGEN_XPORT_PROXY_RESPONSE_TYPE_NAME_LEN: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksResponseTypeNameLen(transport);
        }
    }

    case TGEN_XPORT_PROXY_RESPONSE_TYPE_NAME: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksResponseTypeName(transport);
        }
    }

    case TGEN_XPORT_SUCCESSEOF:
    case TGEN_XPORT_SUCCESSOPEN: {
        return TGEN_EVENT_DONE;
    }

    case TGEN_XPORT_ERROR:
    default: {
        return TGEN_EVENT_NONE;
    }
    }
}

gint64 tgentransport_getStartTimestamp(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    return transport->time.start;
}

gboolean tgentransport_checkTimeout(TGenTransport* transport, gint64 stalloutUSecs, gint64 timeoutUSecs) {
    TGEN_ASSERT(transport);

    if(!tgentransport_wantsEvents(transport)) {
        /* we already reached a terminal state correctly */
        return FALSE;
    }

    gint64 now = g_get_monotonic_time();

    gint64 stalloutCutoff = transport->time.lastProgress + stalloutUSecs;
    gint64 timeoutCutoff = transport->time.start + timeoutUSecs;

    gboolean stalled = (now >= stalloutCutoff) ? TRUE : FALSE;
    gboolean tookTooLong = (now >= timeoutCutoff) ? TRUE : FALSE;

    if((stalloutUSecs > 0 && stalled) || (timeoutUSecs > 0 && tookTooLong)) {
        /* it's either a stallout or timeout error */
        if(stalloutUSecs > 0 && stalled) {
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_STALLOUT);
        } else {
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_TIMEOUT);
        }

        /* yes we had a stallout/timeout */
        return TRUE;
    } else {
        /* we did not have a stallout/timeout yet */
        return FALSE;
    }
}
