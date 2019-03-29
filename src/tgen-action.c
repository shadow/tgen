/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>
#include <math.h>

#include "tgen.h"

typedef struct _TGenActionStartData {
    /* TODO change all times to use the handleTime func, as store nanos
     * currently only the heartbeat does this. */
    guint64 timeNanos;
    guint64 timeoutNanos;
    guint64 stalloutNanos;
    guint64 heartbeatPeriodNanos;
    GLogLevelFlags loglevel;
    guint16 serverport;
    TGenPeer* socksproxy;
    TGenPool* peers;
} TGenActionStartData;

typedef struct _TGenActionEndData {
    guint64 timeNanos;
    guint64 count;
    guint64 size;
} TGenActionEndData;

typedef struct _TGenActionPauseData {
    TGenPool* pauseTimesNanos;
    glong totalIncomingEdges;
    glong completedIncomingEdges;
} TGenActionPauseData;

typedef struct _TGenActionTransferData {
    guint64 size;
    guint64 ourSize;
    guint64 theirSize;
    guint64 timeoutNanos;
    gboolean timeoutIsSet;
    guint64 stalloutNanos;
    gboolean stalloutIsSet;
    TGenPool* peers;
    gchar* socksUsernameStr;
    gchar* socksPasswordStr;
} TGenActionTransferData;

typedef struct _TGenActionModelData {
    gchar* streamModelPath;
    gchar* packetModelPath;
    gchar* socksUsernameStr;
    gchar* socksPasswordStr;
    TGenPool* peers;
} TGenActionModelData;

struct _TGenAction {
    TGenActionType type;
    gpointer key;
    gboolean hasKey;
    gpointer data;
    gint refcount;
    guint magic;
};

static GError* _tgenaction_handlePeer(const gchar* attributeName,
        const gchar* peerStr, TGenPeer** peerOut) {
    g_assert(attributeName && peerStr);

    GError* error = NULL;

    /* split peer into host and port parts */
    gchar** tokens = g_strsplit(peerStr, (const gchar*) ":", 2);

    if (!tokens[0] || !tokens[1]) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "expected peer syntax 'hostname:port' for attribute '%s'",
                attributeName);
        return error;
    }

    /* dont add my own address to the server pool */
    char myname[128];
    if (!tgenconfig_gethostname(&myname[0], 128)
            && !g_ascii_strcasecmp(myname, tokens[0])) {
        tgen_info("refusing to place my address in server pool for attribute '%s'", attributeName);
        return NULL;
    }

    gchar* name = tokens[0];
    in_port_t port = 0;
    guint64 portNum = g_ascii_strtoull(tokens[1], NULL, 10);

    /* validate values */
    // removed to avoid lookups that could leak the intended destination
//    in_addr_t address = _tgengraph_toAddress(tokens[0]);
//    if (address == htonl(INADDR_ANY) || address == htonl(INADDR_NONE)) {
//        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
//                "invalid peer '%s' for host part of attribute '%s', "
//                "expected 'localhost', '127.0.0.1', or valid node hostname",
//                peerStr, attributeName);
//    }
    if (portNum > UINT16_MAX) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "invalid peer '%s' for port part of attribute '%s', "
                 "expected 16 bit unsigned integer", peerStr,
                attributeName);
    } else {
        port = (in_port_t) htons((guint16) portNum);
    }

    if (!error) {
        TGenPeer* peer = tgenpeer_newFromName(name, port);
        tgen_debug("parsed peer '%s' from string '%s'", tgenpeer_toString(peer), peerStr);

        if (peerOut) {
            *peerOut = peer;
        } else {
            tgenpeer_unref(peer);
        }
    }

    g_strfreev(tokens);

    return error;
}

static GError* _tgenaction_handlePeers(const gchar* attributeName,
        const gchar* peersStr, TGenPool* peerPool) {
    g_assert(attributeName && peersStr);

    GError* error = NULL;

    /* split into peers */
    gchar** tokens = g_strsplit(peersStr, (const gchar*) ",", 0);

    /* handle each peer */
    for (int i = 0; tokens[i] != NULL; i++) {
        TGenPeer* peer = NULL;

        error = _tgenaction_handlePeer(attributeName, tokens[i], &peer);

        if (!error && peerPool && peer) {
            tgenpool_add(peerPool, peer);
        } else {
            if(peer) {
                /* didn't add the peer */
                tgenpeer_unref(peer);
            }
            if (error) {
                /* some validation error */
                break;
            }
        }
    }

    g_strfreev(tokens);

    return error;
}

static GError* _tgenaction_handleBytes(const gchar* attributeName,
        const gchar* byteStr, guint64* bytesOut) {
    g_assert(attributeName && byteStr);

    GError* error = NULL;

    /* split into parts (format example: "10 MiB") */
    gchar** tokens = g_strsplit(byteStr, (const gchar*) " ", 2);
    gchar* bytesToken = tokens[0];
    gchar* suffixToken = tokens[1];

    glong bytesTokenLength = g_utf8_strlen(bytesToken, -1);
    for (glong i = 0; i < bytesTokenLength; i++) {
        gchar c = bytesToken[i];
        if (!g_ascii_isdigit(c)) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "non-digit byte '%c' in byte string '%s' for attribute '%s', "
                    "expected format like '10240' or '10 KiB'",
                    c, byteStr, attributeName);
            break;
        }
    }

    if (!error) {
        guint64 bytes = g_ascii_strtoull(bytesToken, NULL, 10);

        if (suffixToken) {
            gint base = 0, exponent = 0;

            if (!g_ascii_strcasecmp(suffixToken, "kb")) {
                base = 10, exponent = 3;
            } else if (!g_ascii_strcasecmp(suffixToken, "mb")) {
                base = 10, exponent = 6;
            } else if (!g_ascii_strcasecmp(suffixToken, "gb")) {
                base = 10, exponent = 9;
            } else if (!g_ascii_strcasecmp(suffixToken, "tb")) {
                base = 10, exponent = 12;
            } else if (!g_ascii_strcasecmp(suffixToken, "kib")) {
                base = 2, exponent = 10;
            } else if (!g_ascii_strcasecmp(suffixToken, "mib")) {
                base = 2, exponent = 20;
            } else if (!g_ascii_strcasecmp(suffixToken, "gib")) {
                base = 2, exponent = 30;
            } else if (!g_ascii_strcasecmp(suffixToken, "tib")) {
                base = 2, exponent = 40;
            } else {
                error = g_error_new(G_MARKUP_ERROR,
                        G_MARKUP_ERROR_INVALID_CONTENT,
                        "invalid bytes suffix '%s' in byte string '%s' for attribute '%s', "
                        "expected one of: 'kib','mib','gib','tib','kb','mb','gb', or 'tb'",
                        suffixToken, byteStr, attributeName);
            }

            if (!error && base && exponent) {
                long double factor = powl((long double) base, (long double) exponent);
                long double converted = (long double) (bytes * factor);
                bytes = (guint64) converted;

                if (bytes <= 0) {
                    error = g_error_new(G_MARKUP_ERROR,
                        G_MARKUP_ERROR_INVALID_CONTENT,
                        "invalid byte conversion, factor %Lf computed from base %i and exponent %i",
                        factor, base, exponent);
                }
            }
        }
        if (!error) {
            tgen_debug("parsed %lu bytes from string %s", bytes, byteStr);
            if (bytesOut) {
                *bytesOut = bytes;
            }
        }
    }

    g_strfreev(tokens);

    return error;
}

static GError* _tgenaction_handleTime(const gchar* attributeName, const gchar* timeStr, guint64* timeNanosOut) {
    g_assert(attributeName && timeStr);

    GError* error = NULL;

    /* split into parts (format example: "10 seconds") */
    gchar** tokens = g_strsplit(timeStr, (const gchar*) " ", 2);
    gchar* timeToken = tokens[0];
    gchar* suffixToken = tokens[1];

    glong timeTokenLength = g_utf8_strlen(timeToken, -1);
    for (glong i = 0; i < timeTokenLength; i++) {
        gchar c = timeToken[i];
        if (!g_ascii_isdigit(c)) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "non-digit byte '%c' in time string '%s' for attribute '%s', "
                    "expected format like '10', '10 seconds' or '10 s'",
                    c, timeStr, attributeName);
            break;
        }
    }

    if (!error) {
        guint64 timeUnits = g_ascii_strtoull(timeToken, NULL, 10);
        guint64 timeNanos = 0;

        if (!suffixToken) {
            /* assume default time in seconds */
            timeNanos = timeUnits * 1000000000;
        } else {
            if (!g_ascii_strcasecmp(suffixToken, "nanosecond") || !g_ascii_strcasecmp(suffixToken, "nanoseconds") ||
                    !g_ascii_strcasecmp(suffixToken, "nsec") || !g_ascii_strcasecmp(suffixToken, "nsecs") ||
                    !g_ascii_strcasecmp(suffixToken, "ns")) {
                timeNanos = timeUnits;
            } else if (!g_ascii_strcasecmp(suffixToken, "microsecond") || !g_ascii_strcasecmp(suffixToken, "microseconds") ||
                    !g_ascii_strcasecmp(suffixToken, "usec") || !g_ascii_strcasecmp(suffixToken, "usecs") ||
                    !g_ascii_strcasecmp(suffixToken, "us")) {
                timeNanos = timeUnits * 1000;
            } else if (!g_ascii_strcasecmp(suffixToken, "millisecond") || !g_ascii_strcasecmp(suffixToken, "milliseconds") ||
                    !g_ascii_strcasecmp(suffixToken, "msec") || !g_ascii_strcasecmp(suffixToken, "msecs") ||
                    !g_ascii_strcasecmp(suffixToken, "ms")) {
                timeNanos = timeUnits * 1000000;
            } else if (!g_ascii_strcasecmp(suffixToken, "second") || !g_ascii_strcasecmp(suffixToken, "seconds") ||
                    !g_ascii_strcasecmp(suffixToken, "sec") || !g_ascii_strcasecmp(suffixToken, "secs") ||
                    !g_ascii_strcasecmp(suffixToken, "s")) {
                timeNanos = timeUnits * 1000000000;
            } else if (!g_ascii_strcasecmp(suffixToken, "minute") || !g_ascii_strcasecmp(suffixToken, "minutes") ||
                    !g_ascii_strcasecmp(suffixToken, "min") || !g_ascii_strcasecmp(suffixToken, "mins") ||
                    !g_ascii_strcasecmp(suffixToken, "m")) {
                timeNanos = timeUnits * 1000000000 * 60;
            } else if (!g_ascii_strcasecmp(suffixToken, "hour") || !g_ascii_strcasecmp(suffixToken, "hours") ||
                    !g_ascii_strcasecmp(suffixToken, "hr") || !g_ascii_strcasecmp(suffixToken, "hrs") ||
                    !g_ascii_strcasecmp(suffixToken, "h")) {
                timeNanos = timeUnits * 1000000000 * 60 * 60;
            } else {
                error = g_error_new(G_MARKUP_ERROR,
                        G_MARKUP_ERROR_INVALID_CONTENT,
                        "invalid time suffix '%s' in time string '%s' for attribute '%s', "
                        "expected one of: 'nanosecond','nanoseconds','nsec','nsecs','ns', "
                        "'microsecond', 'microseconds', 'usec', 'usecs', 'us', "
                        "'millisecond', 'milliseconds', 'msec', 'msecs', 'ms', "
                        "'second', 'seconds', 'sec', 'secs', 's', "
                        "'minute', 'minutes', 'min', 'mins', 'm', "
                        "'hour', 'hours', 'hr', 'hrs', or 'h'",
                        suffixToken, timeStr, attributeName);
            }
        }

        if(!error) {
            tgen_debug("parsed %lu nanoseconds from string %s", timeNanos, timeStr);
            if(timeNanosOut) {
                *timeNanosOut = timeNanos;
            }
        }
    }

    g_strfreev(tokens);

    return error;
}

static GError* _tgenaction_handleTimeList(const gchar* attributeName, const gchar* timeStr,
        TGenPool* pauseTimesOut) {
    g_assert(attributeName && timeStr && pauseTimesOut);

    GError* error = NULL;

    /* split into peers */
    gchar** tokens = g_strsplit(timeStr, (const gchar*) ",", 0);

    /* handle each peer */
    for (gint i = 0; tokens[i] != NULL; i++) {
        if (!g_ascii_strncasecmp(tokens[i], "\0", (gsize) 1)) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "invalid content in string %s for attribute '%s', "
                    "expected list of integers", timeStr, attributeName);
            break;
        }

        guint64* pauseTimeNanos = g_new0(guint64, 1);
        error = _tgenaction_handleTime(attributeName, tokens[i], pauseTimeNanos);
        if(error) {
            break;
        } else {
            tgenpool_add(pauseTimesOut, pauseTimeNanos);
        }

    }

    g_strfreev(tokens);

    return error;
}

static GError* _tgenaction_handleBoolean(const gchar* attributeName,
        const gchar* booleanStr, gboolean* booleanOut, gboolean* isFoundOut) {
    g_assert(attributeName && booleanStr);

    GError* error = NULL;
    gboolean boolean = FALSE;
    gboolean isFound = FALSE;

    if (!g_ascii_strcasecmp(booleanStr, "true")
            || !g_ascii_strcasecmp(booleanStr, "1")) {
        isFound = TRUE;
        boolean = TRUE;
    } else if (!g_ascii_strcasecmp(booleanStr, "false")
            || !g_ascii_strcasecmp(booleanStr, "0")) {
        isFound = TRUE;
        boolean = FALSE;
    } else {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "invalid content for attribute '%s', "
                "expected boolean value 'true' or 'false'",
                attributeName);
    }

    if (isFound) {
        tgen_debug("parsed boolean %i from string %s", *booleanOut, booleanStr);
        if (booleanOut) {
            *booleanOut = boolean;
        }
        if (isFoundOut) {
            *isFoundOut = isFound;
        }
    }

    return error;
}

static GError* _tgenaction_handleLogLevel(const gchar* attributeName, const gchar* loglevelStr, GLogLevelFlags* loglevelOut){
    g_assert(attributeName && loglevelStr);

    GError* error = NULL;
    GLogLevelFlags loglevel = 0;

    if (g_ascii_strcasecmp(loglevelStr, "error") == 0) {
        loglevel = G_LOG_LEVEL_ERROR;
    } else if (g_ascii_strcasecmp(loglevelStr, "critical") == 0) {
        loglevel = G_LOG_LEVEL_CRITICAL;
    } else if (g_ascii_strcasecmp(loglevelStr, "warning") == 0) {
        loglevel = G_LOG_LEVEL_WARNING;
    } else if (g_ascii_strcasecmp(loglevelStr, "message") == 0) {
        loglevel = G_LOG_LEVEL_MESSAGE;
    } else if (g_ascii_strcasecmp(loglevelStr, "info") == 0) {
        loglevel = G_LOG_LEVEL_INFO;
    } else if (g_ascii_strcasecmp(loglevelStr, "debug") == 0) {
        loglevel = G_LOG_LEVEL_DEBUG;
    } else {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                        "invalid content in string '%s' for attribute '%s', "
                        "expected one of: 'error', 'critical', 'warning', 'message', 'info', or 'debug'",
                        loglevelStr, attributeName);
    }

    if(!error && loglevelOut) {
        *loglevelOut = loglevel;
    }

    return error;
}

static void _tgenaction_free(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->refcount <= 0);

    if(action->type == TGEN_ACTION_START) {
        TGenActionStartData* data = (TGenActionStartData*) action->data;
        if(data->peers) {
            tgenpool_unref(data->peers);
        }
    } else if(action->type == TGEN_ACTION_TRANSFER) {
        TGenActionTransferData* data = (TGenActionTransferData*) action->data;
        if(data->peers) {
            tgenpool_unref(data->peers);
        }
        if(data->localSchedule) {
            g_free(data->localSchedule);
            data->localSchedule = NULL;
        }
        if(data->remoteSchedule) {
            g_free(data->remoteSchedule);
            data->remoteSchedule = NULL;
        }
    } else if(action->type == TGEN_ACTION_MODEL) {
        TGenActionModelData* data = (TGenActionModelData*) action->data;
        if(data->streamModelPath) {
            g_free(data->streamModelPath);
            data->streamModelPath = NULL;
        }
        if(data->packetModelPath) {
            g_free(data->packetModelPath);
            data->packetModelPath = NULL;
        }
    }

    if(action->data) {
        g_free(action->data);
    }

    action->magic = 0;
    g_free(action);
}

void tgenaction_ref(TGenAction* action) {
    TGEN_ASSERT(action);
    action->refcount++;
}

void tgenaction_unref(TGenAction* action) {
    TGEN_ASSERT(action);
    if(--action->refcount <= 0) {
        _tgenaction_free(action);
    }
}

TGenAction* tgenaction_newStartAction(const gchar* timeStr, const gchar* timeoutStr,
        const gchar* stalloutStr, const gchar* heartbeatStr,
        const gchar* loglevelStr, const gchar* serverPortStr,
        const gchar* peersStr, const gchar* socksProxyStr, GError** error) {
    g_assert(error);

    adsfasdf make the server port optional
    /* a serverport is required */
    if (!serverPortStr || !g_ascii_strncasecmp(serverPortStr, "\0", (gsize) 1)) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "start action missing required attribute 'serverport'");
        return NULL;
    }

    /* a time delay from application startup is optional */
    guint64 timedelayNanos = 0;
    if (timeStr && g_ascii_strncasecmp(timeStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleTime("time", timeStr, &timedelayNanos);
        if (*error) {
            return NULL;
        }
    }

    /* a global default transfer timeout is optional */
    guint64 defaultTimeoutNanos = 0;
    if (timeoutStr && g_ascii_strncasecmp(timeoutStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleTime("timeout", timeoutStr, &defaultTimeoutNanos);
        if (*error) {
            return NULL;
        }
    }

    guint64 defaultStalloutNanos = 0;
    if (stalloutStr && g_ascii_strncasecmp(stalloutStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleTime("stallout", stalloutStr, &defaultStalloutNanos);
        if (*error) {
            return NULL;
        }
    }

    guint64 heartbeatPeriodNanos = 0;
    if (heartbeatStr && g_ascii_strncasecmp(heartbeatStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleTime("heartbeat", heartbeatStr, &heartbeatPeriodNanos);
        if (*error) {
            return NULL;
        }
    }

    /* specifying a log level is optional, default is message level */
    GLogLevelFlags loglevel = G_LOG_LEVEL_MESSAGE;
    if(loglevelStr && g_ascii_strncasecmp(loglevelStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleLogLevel("loglevel", loglevelStr, &loglevel);
        if (*error) {
            return NULL;
        }
    }

    /* a socks proxy address is optional */
    TGenPeer* socksproxy = NULL;
    if (socksProxyStr && g_ascii_strncasecmp(socksProxyStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handlePeer("socksproxy", socksProxyStr, &socksproxy);
        if (*error) {
            return NULL;
        }
    }

    /* a list of peers is optional */
    TGenPool* peerPool = NULL;
    if (peersStr && g_ascii_strncasecmp(peersStr, "\0", (gsize) 1)) {
        /* validate the peer pool */
        peerPool = tgenpool_new((GDestroyNotify)tgenpeer_unref);
        *error = _tgenaction_handlePeers("peers", peersStr, peerPool);
        if (*error) {
            tgenpool_unref(peerPool);
            return NULL;
        }
    }

    /* if we get here, we have what we need and validated it */
    TGenAction* action = g_new0(TGenAction, 1);
    action->magic = TGEN_MAGIC;
    action->refcount = 1;

    action->type = TGEN_ACTION_START;

    TGenActionStartData* data = g_new0(TGenActionStartData, 1);

    data->timeNanos = timedelayNanos;
    data->timeoutNanos = defaultTimeoutNanos;
    data->stalloutNanos = defaultStalloutNanos;
    data->heartbeatPeriodNanos = heartbeatPeriodNanos;
    data->loglevel = loglevel;
    guint64 longport = g_ascii_strtoull(serverPortStr, NULL, 10);
    data->serverport = htons((guint16)longport);
    data->peers = peerPool;
    data->socksproxy = socksproxy;

    action->data = data;

    return action;
}

TGenAction* tgenaction_newEndAction(const gchar* timeStr, const gchar* countStr,
        const gchar* sizeStr, GError** error) {
    g_assert(error);

    /* time, count, and size are termination conditions, and all are optional */

    guint64 size = 0;
    if (sizeStr && g_ascii_strncasecmp(sizeStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleBytes("size", sizeStr, &size);
        if (*error) {
            return NULL;
        }
    }

    guint64 timeNanos = 0;
    if (timeStr && g_ascii_strncasecmp(timeStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleTime("time", timeStr, &timeNanos);
        if(*error) {
            return NULL;
        }
    }

    TGenAction* action = g_new0(TGenAction, 1);
    action->magic = TGEN_MAGIC;
    action->refcount = 1;

    action->type = TGEN_ACTION_END;

    TGenActionEndData* data = g_new0(TGenActionEndData, 1);
    data->size = size;
    data->timeNanos = timeNanos;
    if (countStr && g_ascii_strncasecmp(countStr, "\0", (gsize) 1)) {
        data->count = g_ascii_strtoull(countStr, NULL, 10);
    }

    action->data = data;

    return action;
}

TGenAction* tgenaction_newPauseAction(const gchar* timeStr, glong totalIncoming, GError** error) {
    g_assert(error);

    TGenPool* pauseTimesNanos = NULL;

    /* the time is optional, if it doesnt exist, we act as a 'synchronizing' pause
     * where we wait until all incoming edges visit us to wake up */
    if (timeStr && g_ascii_strncasecmp(timeStr, "\0", (gsize) 1)) {
        pauseTimesNanos = tgenpool_new(g_free);
        *error = _tgenaction_handleTimeList("time", timeStr, pauseTimesNanos);
        if (*error) {
            tgenpool_unref(pauseTimesNanos);
            return NULL;
        }
    }

    TGenAction* action = g_new0(TGenAction, 1);
    action->magic = TGEN_MAGIC;
    action->refcount = 1;

    action->type = TGEN_ACTION_PAUSE;

    TGenActionPauseData* data = g_new0(TGenActionPauseData, 1);
    data->pauseTimesNanos = pauseTimesNanos;
    data->totalIncomingEdges = totalIncoming;
    data->completedIncomingEdges = 0;

    action->data = data;

    return action;
}

TGenAction* tgenaction_newTransferAction(const gchar* typeStr, const gchar* protocolStr,
        const gchar* sizeStr, const gchar *ourSizeStr, const gchar *theirSizeStr,
        const gchar* peersStr, const gchar* timeoutStr, const gchar* stalloutStr,
        const gchar* localscheduleStr, const gchar* remotescheduleStr,
        const gchar* socksUsernameStr, const gchar* socksPasswordStr,
        GError** error) {
    g_assert(error);

    /* type is required */
    TGenTransferType type = TGEN_TYPE_NONE;
    if (!typeStr || !g_ascii_strncasecmp(typeStr, "\0", (gsize) 1)) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "transfer action missing required attribute 'type'");
        return NULL;
    } else if (!g_ascii_strcasecmp(typeStr, "get")) {
        type = TGEN_TYPE_GET;
    } else if (!g_ascii_strcasecmp(typeStr, "put")) {
        type = TGEN_TYPE_PUT;
    } else if (!g_ascii_strcasecmp(typeStr, "getput")) {
        type = TGEN_TYPE_GETPUT;
    } else if (!g_ascii_strcasecmp(typeStr, "schedule")) {
        type = TGEN_TYPE_SCHEDULE;
    } else {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                "transfer action has unknown value '%s' for 'type' attribute",
                typeStr);
        return NULL;
    }

    /* size is required for certain types */
    gboolean sizeIsValid = sizeStr && g_ascii_strncasecmp(sizeStr, "\0", (gsize)1);
    if ((type == TGEN_TYPE_GET || type == TGEN_TYPE_PUT) && !sizeIsValid) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "transfer action missing required attribute 'size'");
        return NULL;
    }
    guint64 size = 0;
    if (sizeIsValid) {
        *error = _tgenaction_handleBytes("size", sizeStr, &size);
        if (*error) {
            return NULL;
        }
    }

    /* oursize is required for certain types */
    gboolean ourSizeIsValid = ourSizeStr && g_ascii_strncasecmp(ourSizeStr, "\0", (gsize)1);
    if (type == TGEN_TYPE_GETPUT && !ourSizeIsValid) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "transfer action missing required attribute 'oursize'");
        return NULL;
    }
    guint64 ourSize = 0;
    if (ourSizeIsValid) {
        *error = _tgenaction_handleBytes("size", ourSizeStr, &ourSize);
        if (*error) {
            return NULL;
        }
    }

    /* theirsize is required for certain types */
    gboolean theirSizeIsValid = theirSizeStr && g_ascii_strncasecmp(theirSizeStr, "\0", (gsize)1);
    if (type == TGEN_TYPE_GETPUT && !theirSizeIsValid) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "transfer action missing required attribute 'theirsize'");
        return NULL;
    }
    guint64 theirSize = 0;
    if (theirSizeIsValid) {
        *error = _tgenaction_handleBytes("size", theirSizeStr, &theirSize);
        if (*error) {
            return NULL;
        }
    }

    /* peers are optional */
    TGenPool* peerPool = NULL;
    if (peersStr && g_ascii_strncasecmp(peersStr, "\0", (gsize) 1)) {
        peerPool = tgenpool_new((GDestroyNotify)tgenpeer_unref);
        *error = _tgenaction_handlePeers("peers", peersStr, peerPool);
        if (*error) {
            tgenpool_unref(peerPool);
            return NULL;
        }
    }

    /* a transfer timeout is optional */
    guint64 timeoutNanos = 0;
    gboolean timeoutIsSet = FALSE;
    if (timeoutStr && g_ascii_strncasecmp(timeoutStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleTime("timeout", timeoutStr, &timeoutNanos);
        if(*error) {
            return NULL;
        }
        timeoutIsSet = TRUE;
    }

    guint64 stalloutNanos = 0;
    gboolean stalloutIsSet = FALSE;
    if (stalloutStr && g_ascii_strncasecmp(stalloutStr, "\0", (gsize) 1)) {
        *error = _tgenaction_handleTime("stallout", stalloutStr, &stalloutNanos);
        if(*error) {
            return NULL;
        }
        stalloutIsSet = TRUE;
    }

    /* schedules are required for sched type */
    if(type == TGEN_TYPE_SCHEDULE) {
        gboolean localSchedIsValid = localscheduleStr && g_ascii_strncasecmp(localscheduleStr, "\0", (gsize)1);
        gboolean remoteSchedIsValid = remotescheduleStr && g_ascii_strncasecmp(remotescheduleStr, "\0", (gsize)1);

        if(!localSchedIsValid) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                    "transfer action missing required attribute 'localschedule'");
            return NULL;
        }
        if(!remoteSchedIsValid) {
            *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                    "transfer action missing required attribute 'remoteschedule'");
            return NULL;
        }
    }

    TGenAction* action = g_new0(TGenAction, 1);
    action->magic = TGEN_MAGIC;
    action->refcount = 1;

    action->type = TGEN_ACTION_TRANSFER;

    TGenActionTransferData* data = g_new0(TGenActionTransferData, 1);
    data->protocol = protocol;
    data->type = type;
    data->size = size;
    data->ourSize = ourSize;
    data->theirSize = theirSize;
    data->peers = peerPool;
    data->timeoutNanos = timeoutNanos;
    data->timeoutIsSet = timeoutIsSet;
    data->stalloutNanos = stalloutNanos;
    data->stalloutIsSet = stalloutIsSet;
    if(type == TGEN_TYPE_SCHEDULE && localscheduleStr) {
        data->localSchedule = g_strdup(localscheduleStr);
    }
    if(type == TGEN_TYPE_SCHEDULE && remotescheduleStr) {
        data->remoteSchedule = g_strdup(remotescheduleStr);
    }
    data->socksUsernameStr = socksUsernameStr ? g_strdup(socksUsernameStr) : NULL;
    data->socksPasswordStr = socksPasswordStr ? g_strdup(socksPasswordStr) : NULL;

    action->data = data;

    return action;

}

TGenAction* tgenaction_newModelAction(const gchar* streamModelPath,
        const gchar* packetModelPath, const gchar* peersStr,
        const gchar* socksUsernameStr, const gchar* socksPasswordStr, GError** error) {
    g_assert(error);

    gboolean streamPathIsValid = streamModelPath && g_ascii_strncasecmp(streamModelPath, "\0", (gsize)1);
    if(!streamPathIsValid) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "model action missing required attribute 'streammodelpath'");
        return NULL;
    }

    if(!g_file_test(streamModelPath, G_FILE_TEST_EXISTS|G_FILE_TEST_IS_REGULAR)) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "model action found invalid path for 'streammodelpath': %s", streamModelPath);
        return NULL;
    }

    gboolean packetPathIsValid = packetModelPath && g_ascii_strncasecmp(packetModelPath, "\0", (gsize)1);
    if(!packetPathIsValid) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                "model action missing required attribute 'packetmodelpath'");
        return NULL;
    }

    if(!g_file_test(packetModelPath, G_FILE_TEST_EXISTS|G_FILE_TEST_IS_REGULAR)) {
        *error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "model action found invalid path for 'packetmodelpath': %s", packetModelPath);
        return NULL;
    }

    /* peers are optional */
    TGenPool* peerPool = NULL;
    if (peersStr && g_ascii_strncasecmp(peersStr, "\0", (gsize) 1)) {
        peerPool = tgenpool_new((GDestroyNotify)tgenpeer_unref);
        *error = _tgenaction_handlePeers("peers", peersStr, peerPool);
        if (*error) {
            tgenpool_unref(peerPool);
            return NULL;
        }
    }

    TGenAction* action = g_new0(TGenAction, 1);
    action->magic = TGEN_MAGIC;
    action->refcount = 1;

    action->type = TGEN_ACTION_MODEL;

    TGenActionModelData* data = g_new0(TGenActionModelData, 1);
    data->streamModelPath = g_strdup(streamModelPath);
    data->packetModelPath = g_strdup(packetModelPath);
    data->socksUsernameStr = socksUsernameStr ? g_strdup(socksUsernameStr) : NULL;
    data->socksPasswordStr = socksPasswordStr ? g_strdup(socksPasswordStr) : NULL;
    data->peers = peerPool;

    action->data = data;

    return action;
}

void tgenaction_setKey(TGenAction* action, gpointer key) {
    TGEN_ASSERT(action);
    action->key = key;
    action->hasKey = TRUE;
}

gpointer tgenaction_getKey(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->hasKey);
    return action->key;
}

TGenActionType tgenaction_getType(TGenAction* action) {
    TGEN_ASSERT(action);
    return action->type;
}

guint16 tgenaction_getServerPort(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_START);
    return ((TGenActionStartData*)action->data)->serverport;
}

TGenPeer* tgenaction_getSocksProxy(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_START);
    return ((TGenActionStartData*)action->data)->socksproxy;
}

guint64 tgenaction_getStartTimeMillis(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_START);
    return (guint64)(((TGenActionStartData*)action->data)->timeNanos / 1000000);
}

guint64 tgenaction_getDefaultTimeoutMillis(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_START);
    return (guint64)(((TGenActionStartData*)action->data)->timeoutNanos / 1000000);
}

guint64 tgenaction_getDefaultStalloutMillis(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_START);
    return (guint64)(((TGenActionStartData*)action->data)->stalloutNanos / 1000000);
}

guint64 tgenaction_getHeartbeatPeriodMillis(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_START);
    return (guint64)(((TGenActionStartData*)action->data)->heartbeatPeriodNanos / 1000000);
}

GLogLevelFlags tgenaction_getLogLevel(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_START);
    return ((TGenActionStartData*)action->data)->loglevel;
}

void tgenaction_getTransferParameters(TGenAction* action, TGenTransferType* typeOut,
        TGenTransportProtocol* protocolOut, guint64* sizeOut, guint64 *ourSizeOut,
        guint64 *theirSizeOut, guint64* timeoutOut, guint64* stalloutOut,
        gchar** localSchedule, gchar** remoteSchedule) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_TRANSFER);

    TGenActionTransferData* data = (TGenActionTransferData*)action->data;

    if(typeOut) {
        *typeOut = data->type;
    }
    if(protocolOut) {
        *protocolOut = data->protocol;
    }
    if(sizeOut) {
        *sizeOut = data->size;
    }
    if (ourSizeOut) {
        *ourSizeOut = data->ourSize;
    }
    if (theirSizeOut) {
        *theirSizeOut = data->theirSize;
    }
    if(timeoutOut) {
        if(data->timeoutIsSet) {
            /* nanoseconds to milliseconds */
            *timeoutOut = (guint64)(data->timeoutNanos / 1000000);
        }
    }
    if(stalloutOut) {
        if(data->stalloutIsSet) {
            /* nanoseconds to milliseconds */
            *stalloutOut = (guint64)(data->stalloutNanos / 1000000);
        }
    }
    if(localSchedule) {
        *localSchedule = data->localSchedule;
    }
    if(remoteSchedule) {
        *remoteSchedule = data->remoteSchedule;
    }
}

void tgenaction_getModelPaths(TGenAction* action,
        gchar** streamModelPathStr, gchar** packetModelPathStr) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_MODEL);

    TGenActionModelData* data = (TGenActionModelData*)action->data;

    if(streamModelPathStr) {
        *streamModelPathStr = data->streamModelPath;
    }
    if(packetModelPathStr) {
        *packetModelPathStr = data->packetModelPath;
    }
}

void tgenaction_getSocksParams(TGenAction* action,
        gchar** socksUsernameStr, gchar** socksPasswordStr) {
    TGEN_ASSERT(action);
    g_assert(action->data);
    g_assert(action->type == TGEN_ACTION_MODEL || action->type == TGEN_ACTION_TRANSFER);

    gchar* userStr = NULL;
    gchar* passStr = NULL;

    if(action->type == TGEN_ACTION_TRANSFER) {
      TGenActionTransferData* data = (TGenActionTransferData*)action->data;
      userStr = data->socksUsernameStr;
      passStr = data->socksPasswordStr;
    } else if(action->type == TGEN_ACTION_MODEL) {
      TGenActionModelData* data = (TGenActionModelData*)action->data;
      userStr = data->socksUsernameStr;
      passStr = data->socksPasswordStr;
    }

    if(socksUsernameStr) {
        *socksUsernameStr = userStr;
    }
    if(socksPasswordStr) {
        *socksPasswordStr = passStr;
    }
}

TGenPool* tgenaction_getPeers(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data);

    if(action->type == TGEN_ACTION_TRANSFER) {
        return ((TGenActionTransferData*)action->data)->peers;
    } else if(action->type == TGEN_ACTION_MODEL) {
        return ((TGenActionModelData*)action->data)->peers;
    } else if(action->type == TGEN_ACTION_START) {
        return ((TGenActionStartData*)action->data)->peers;
    } else {
        return NULL;
    }
}

guint64 tgenaction_getEndTimeMillis(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_END);
    return (guint64)(((TGenActionEndData*)action->data)->timeNanos / 1000000);
}

guint64 tgenaction_getEndCount(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_END);
    return ((TGenActionEndData*)action->data)->count;
}

guint64 tgenaction_getEndSize(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_END);
    return ((TGenActionEndData*)action->data)->size;
}

gboolean tgenaction_hasPauseTime(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_PAUSE);
    return (((TGenActionPauseData*)action->data)->pauseTimesNanos) != NULL ? TRUE : FALSE;
}

guint64 tgenaction_getPauseTimeMillis(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_PAUSE);
    guint64* time = tgenpool_getRandom(((TGenActionPauseData*)action->data)->pauseTimesNanos);
    return (guint64)(*time / 1000000);
}

gboolean tgenaction_incrementPauseVisited(TGenAction* action) {
    TGEN_ASSERT(action);
    g_assert(action->data && action->type == TGEN_ACTION_PAUSE);
    TGenActionPauseData* pauseData = (TGenActionPauseData*)action->data;

    pauseData->completedIncomingEdges++;
    if(pauseData->completedIncomingEdges >= pauseData->totalIncomingEdges) {
        pauseData->completedIncomingEdges = 0;
        return TRUE;
    } else {
        return FALSE;
    }
}
