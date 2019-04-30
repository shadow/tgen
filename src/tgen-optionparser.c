/*
 * See LICENSE for licensing information
 */

#include <math.h>

#include "tgen.h"

GError* tgenoptionparser_parseUInt64(const gchar* attributeName,
        const gchar* intStr, TGenOptionUInt64* out) {
    g_assert(attributeName);

    if(out) {
        out->isSet = FALSE;
        out->value = 0;
        /* if the string exists and is not set to the empty string */
        if (intStr && g_ascii_strncasecmp(intStr, "\0", (gsize) 1) != 0) {
            out->isSet = TRUE;
            out->value = g_ascii_strtoull(intStr, NULL, 10);
            tgen_debug("parsed %"G_GUINT64_FORMAT" bytes from string '%s' for attribute '%s'",
                    out->value, intStr, attributeName);
        }
    }

    return NULL;
}

GError* tgenoptionparser_parseUInt32(const gchar* attributeName,
        const gchar* intStr, TGenOptionUInt32* out) {
    TGenOptionUInt64 intOpt;
    GError* error = tgenoptionparser_parseUInt64(attributeName, intStr, &intOpt);
    if(!error && out) {
        out->isSet = intOpt.isSet;
        out->value = (guint32) MIN(intOpt.value, G_MAXUINT32);
    }
    return error;
}

GError* tgenoptionparser_parseUInt16(const gchar* attributeName,
        const gchar* intStr, TGenOptionUInt16* out) {
    TGenOptionUInt64 intOpt;
    GError* error = tgenoptionparser_parseUInt64(attributeName, intStr, &intOpt);
    if(!error && out) {
        out->isSet = intOpt.isSet;
        out->value = (guint16) MIN(intOpt.value, G_MAXUINT16);
    }
    return error;
}

GError* tgenoptionparser_parseString(const gchar* attributeName,
        const gchar* stringStr, TGenOptionString* out) {
    g_assert(attributeName);

    if(out) {
        out->isSet = FALSE;
        out->value = NULL;
        /* if the string exists and is not set to the empty string */
        if (stringStr && g_ascii_strncasecmp(stringStr, "\0", (gsize) 1) != 0) {
            out->isSet = TRUE;
            out->value = g_strdup(stringStr);
            tgen_debug("parsed string '%s' from string '%s' for attribute '%s'",
                    out->value, stringStr, attributeName);
        }
    }

    return NULL;
}

GError* tgenoptionparser_parsePeer(const gchar* attributeName,
        const gchar* peerStr, TGenOptionPeer* out) {
    g_assert(attributeName);

    GError* error = NULL;

    if(out) {
        out->isSet = FALSE;
        out->value = NULL;
    }

    /* if the string exists and is not set to the empty string */
    if (peerStr && g_ascii_strncasecmp(peerStr, "\0", (gsize) 1) != 0) {
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
            tgen_debug("parsed peer '%s' from string '%s' for attribute '%s'",
                    tgenpeer_toString(peer), peerStr, attributeName);

            if(out) {
                out->isSet = TRUE;
                out->value = peer;
            } else {
                tgenpeer_unref(peer);
            }
        }

        g_strfreev(tokens);
    }

    return error;
}

GError* tgenoptionparser_parsePeerList(const gchar* attributeName,
        const gchar* peersStr, TGenOptionPool* out) {
    g_assert(attributeName);

    GError* error = NULL;
    gboolean isSet = FALSE;
    TGenPool* peerPool = NULL;

    /* if the string exists and is not set to the empty string */
    if (peersStr && g_ascii_strncasecmp(peersStr, "\0", (gsize) 1) != 0) {
        /* split into peers */
        gchar** tokens = g_strsplit(peersStr, (const gchar*) ",", 0);

        /* handle each peer */
        for (int i = 0; tokens[i] != NULL; i++) {

            TGenOptionPeer peerOpt;
            error = tgenoptionparser_parsePeer(attributeName, tokens[i], &peerOpt);

            if (!error && peerOpt.isSet) {
                if(!peerPool) {
                    peerPool = tgenpool_new((GDestroyNotify)tgenpeer_unref);
                }
                tgenpool_add(peerPool, peerOpt.value);
                isSet = TRUE;
            } else {
                if(peerOpt.value) {
                    /* didn't add the peer */
                    tgenpeer_unref(peerOpt.value);
                }
                if (error) {
                    /* some validation error */
                    break;
                }
            }
        }

        g_strfreev(tokens);
    }

    if(error) {
        isSet = FALSE;
        if(peerPool) {
            tgenpool_unref(peerPool);
        }
        peerPool = NULL;
    }

    if(out) {
        out->isSet = isSet;
        out->value = peerPool;
    }

    return error;
}

GError* tgenoptionparser_parseBytes(const gchar* attributeName,
        const gchar* byteStr, TGenOptionUInt64* out) {
    g_assert(attributeName);

    GError* error = NULL;
    guint64 bytes = 0;
    gboolean isSet = FALSE;

    /* if the string exists and is not set to the empty string */
    if (byteStr && g_ascii_strncasecmp(byteStr, "\0", (gsize) 1) != 0) {
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
            bytes = g_ascii_strtoull(bytesToken, NULL, 10);

            if (suffixToken) {
                gint base = 0, exponent = 0;

                if (!g_ascii_strcasecmp(suffixToken, "b") ||
                        !g_ascii_strcasecmp(suffixToken, "byte") ||
                        !g_ascii_strcasecmp(suffixToken, "bytes")) {
                    base = 10, exponent = 0;
                } else if (!g_ascii_strcasecmp(suffixToken, "kb") ||
                        !g_ascii_strcasecmp(suffixToken, "kilobyte") ||
                        !g_ascii_strcasecmp(suffixToken, "kilobytes")) {
                    base = 10, exponent = 3;
                } else if (!g_ascii_strcasecmp(suffixToken, "mb") ||
                        !g_ascii_strcasecmp(suffixToken, "megabyte") ||
                        !g_ascii_strcasecmp(suffixToken, "megabytes")) {
                    base = 10, exponent = 6;
                } else if (!g_ascii_strcasecmp(suffixToken, "gb") ||
                        !g_ascii_strcasecmp(suffixToken, "gigabyte") ||
                        !g_ascii_strcasecmp(suffixToken, "gigabytes")) {
                    base = 10, exponent = 9;
                } else if (!g_ascii_strcasecmp(suffixToken, "tb") ||
                        !g_ascii_strcasecmp(suffixToken, "terabyte") ||
                        !g_ascii_strcasecmp(suffixToken, "terabytes")) {
                    base = 10, exponent = 12;
                } else if (!g_ascii_strcasecmp(suffixToken, "kib") ||
                        !g_ascii_strcasecmp(suffixToken, "kibibyte") ||
                        !g_ascii_strcasecmp(suffixToken, "kibibytes")) {
                    base = 2, exponent = 10;
                } else if (!g_ascii_strcasecmp(suffixToken, "mib") ||
                        !g_ascii_strcasecmp(suffixToken, "mebibyte") ||
                        !g_ascii_strcasecmp(suffixToken, "mebibytes")) {
                    base = 2, exponent = 20;
                } else if (!g_ascii_strcasecmp(suffixToken, "gib") ||
                        !g_ascii_strcasecmp(suffixToken, "gibibyte") ||
                        !g_ascii_strcasecmp(suffixToken, "gibibytes")) {
                    base = 2, exponent = 30;
                } else if (!g_ascii_strcasecmp(suffixToken, "tib") ||
                        !g_ascii_strcasecmp(suffixToken, "tebibyte") ||
                        !g_ascii_strcasecmp(suffixToken, "tebibytes")) {
                    base = 2, exponent = 40;
                } else {
                    error = g_error_new(G_MARKUP_ERROR,
                            G_MARKUP_ERROR_INVALID_CONTENT,
                            "invalid bytes suffix '%s' in byte string '%s' for attribute '%s', "
                            "expected one of: "
                            "'b', 'byte', 'bytes', "
                            "'kb', 'kilobyte', 'kilobytes', "
                            "'kib', 'kibibyte', 'kibibytes', "
                            "'mb', 'megabyte', 'megabytes', "
                            "'mib', 'mebibyte', 'mebibytes', "
                            "'gb', 'gigabyte', 'gigabytes', "
                            "'gib', 'gibibyte', 'gibibytes', "
                            "'tb', 'terabyte', 'terabytes', "
                            "'tib', 'tebibyte', or 'tebibytes'",
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
                tgen_debug("parsed %lu bytes from string '%s' for attribute '%s'",
                        bytes, byteStr, attributeName);
                isSet = TRUE;
            } else {
                bytes = 0;
            }
        }

        g_strfreev(tokens);
    }

    if(out) {
        out->isSet = isSet;
        out->value = bytes;
    }

    return error;
}

GError* tgenoptionparser_parseTime(const gchar* attributeName,
        const gchar* timeStr, TGenOptionUInt64* out) {
    g_assert(attributeName && timeStr);

    GError* error = NULL;
    guint64 timeNanos = 0;
    gboolean isSet = FALSE;

    /* if the string exists and is not set to the empty string */
    if (timeStr && g_ascii_strncasecmp(timeStr, "\0", (gsize) 1) != 0) {
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
                tgen_debug("parsed %lu nanoseconds from string '%s' for attribute '%s'",
                        timeNanos, timeStr, attributeName);
                isSet = TRUE;
            } else {
                timeNanos = 0;
            }
        }

        g_strfreev(tokens);
    }

    if(out) {
        out->isSet = isSet;
        out->value = timeNanos;
    }

    return error;
}

GError* tgenoptionparser_parseTimeList(const gchar* attributeName,
        const gchar* timeStr, TGenOptionPool* out) {
    g_assert(attributeName);

    GError* error = NULL;
    TGenPool* timelist = tgenpool_new(g_free);
    gboolean isSet = FALSE;

    /* if the string exists and is not set to the empty string */
    if (timeStr && g_ascii_strncasecmp(timeStr, "\0", (gsize) 1) != 0) {
        /* split into time integers */
        gchar** tokens = g_strsplit(timeStr, (const gchar*) ",", 0);

        /* handle each entry */
        for (gint i = 0; tokens[i] != NULL; i++) {
            if (!g_ascii_strncasecmp(tokens[i], "\0", (gsize) 1)) {
                error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                        "invalid content in string %s for attribute '%s', "
                        "expected list of integers", timeStr, attributeName);
                break;
            }

            TGenOptionUInt64 timeOpt;
            error = tgenoptionparser_parseTime(attributeName, tokens[i], &timeOpt);
            if(error) {
                break;
            } else {
                guint64* timeNanos = g_new0(guint64, 1);
                *timeNanos = timeOpt.value;
                tgenpool_add(timelist, timeNanos);
            }

        }

        g_strfreev(tokens);

        if(!error) {
            isSet = TRUE;
        }
    }

    if(error) {
        tgenpool_unref(timelist);
        timelist = NULL;
    }

    if(out) {
        out->isSet = isSet;
        out->value = timelist;
    }

    return error;
}

GError* tgenoptionparser_parseBoolean(const gchar* attributeName,
        const gchar* booleanStr, TGenOptionBoolean* out) {
    g_assert(attributeName);

    GError* error = NULL;
    gboolean boolean = FALSE;
    gboolean isSet = FALSE;

    /* if the string exists and is not set to the empty string */
    if (booleanStr && g_ascii_strncasecmp(booleanStr, "\0", (gsize) 1) != 0) {
        if (!g_ascii_strcasecmp(booleanStr, "true")
                || !g_ascii_strcasecmp(booleanStr, "1")) {
            boolean = TRUE;
        } else if (!g_ascii_strcasecmp(booleanStr, "false")
                || !g_ascii_strcasecmp(booleanStr, "0")) {
            boolean = FALSE;
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "invalid content for attribute '%s', "
                    "expected boolean value 'true' or 'false'",
                    attributeName);
        }

        if (!error) {
            tgen_debug("parsed boolean '%i' from value '%s' for attribute '%s'",
                    boolean, booleanStr, attributeName);
            isSet = TRUE;
        }
    }

    if(out) {
        out->isSet = isSet;
        out->value = boolean;
    }

    return error;
}

GError* tgenoptionparser_parseLogLevel(const gchar* attributeName,
        const gchar* loglevelStr, TGenOptionLogLevel* out) {
    g_assert(attributeName);

    GError* error = NULL;
    GLogLevelFlags loglevel = 0;
    gboolean isSet = FALSE;

    /* if the string exists and is not set to the empty string */
    if (loglevelStr && g_ascii_strncasecmp(loglevelStr, "\0", (gsize) 1) != 0) {
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
                    "invalid content in string '%s' for attribute '%s', expected one of: "
                    "'error', 'critical', 'warning', 'message', 'info', or 'debug'",
                    loglevelStr, attributeName);
        }

        if(!error) {
            tgen_debug("parsed loglevel '%i' from value '%s' for attribute '%s'",
                    (gint)loglevel, loglevelStr, attributeName);
            isSet = TRUE;
        }
    }

    if(out) {
        out->isSet = isSet;
        out->value = loglevel;
    }

    return error;
}
