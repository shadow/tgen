/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "tgen.h"

/* disable default timeout */
#define DEFAULT_STREAM_TIMEOUT_NSEC (0*((guint64)1000*1000*1000))
/* 60 second default stallout */
#define DEFAULT_STREAM_STALLOUT_NSEC (30*((guint64)1000*1000*1000))

/* default lengths for buffers used during i/o.
 * the read buffer is temporary and stack-allocated.
 * the write buffer is persistent and heap-allocated, and thus
 * it will consume more memory so we keep it relatively smaller. */
#define DEFAULT_STREAM_READ_BUFLEN 65536
#define DEFAULT_STREAM_WRITE_BUFLEN 32768

/* an auth password so we know both sides understand tgen */
#define TGEN_AUTH_PW "T8nNx9L95LATtckJkR5n"
#define TGEN_PROTO_VERS_MAJ 1
#define TGEN_PROTO_VERS_MIN 0

/* the various states the read side of the connection can take */
typedef enum _TGenStreamRecvState {
    TGEN_STREAM_RECV_NONE,
    TGEN_STREAM_RECV_AUTHENTICATE,
    TGEN_STREAM_RECV_HEADER,
    TGEN_STREAM_RECV_MODEL,
    TGEN_STREAM_RECV_PAYLOAD,
    TGEN_STREAM_RECV_CHECKSUM,
    TGEN_STREAM_RECV_SUCCESS,
    TGEN_STREAM_RECV_ERROR,
} TGenStreamRecvState;

/* the various states the write side of the connection can take */
typedef enum _TGenStreamSendState {
    TGEN_STREAM_SEND_NONE,
    TGEN_STREAM_SEND_COMMAND,
    TGEN_STREAM_SEND_RESPONSE,
    TGEN_STREAM_SEND_PAYLOAD,
    TGEN_STREAM_SEND_CHECKSUM,
    TGEN_STREAM_SEND_FLUSH,
    TGEN_STREAM_SEND_SUCCESS,
    TGEN_STREAM_SEND_ERROR,
} TGenStreamSendState;

/* the various error states the connection can take */
typedef enum _TGenStreamErrorType {
    TGEN_STREAM_ERR_NONE,
    TGEN_STREAM_ERR_AUTHENTICATE,
    TGEN_STREAM_ERR_HEADER,
    TGEN_STREAM_ERR_HEADER_INCOMPLETE,
    TGEN_STREAM_ERR_HEADER_VERSION,
    TGEN_STREAM_ERR_HEADER_MODELMODE,
    TGEN_STREAM_ERR_HEADER_MODELPATH,
    TGEN_STREAM_ERR_HEADER_MODELSIZE,
    TGEN_STREAM_ERR_MODEL,
    TGEN_STREAM_ERR_CHECKSUM,
    TGEN_STREAM_ERR_READ,
    TGEN_STREAM_ERR_WRITE,
    TGEN_STREAM_ERR_READEOF,
    TGEN_STREAM_ERR_WRITEEOF,
    TGEN_STREAM_ERR_TIMEOUT,
    TGEN_STREAM_ERR_STALLOUT,
    TGEN_STREAM_ERR_PROXY,
    TGEN_STREAM_ERR_MISC,
} TGenStreamErrorType;

typedef enum _TGenStreamHeaderFlags {
    TGEN_HEADER_FLAG_NONE = 0,
    TGEN_HEADER_FLAG_PROTOCOL = 1 << 0,
    TGEN_HEADER_FLAG_HOSTNAME = 1 << 1,
    TGEN_HEADER_FLAG_CODE = 1 << 2,
    TGEN_HEADER_FLAG_ID = 1 << 3,
    TGEN_HEADER_FLAG_SENDSIZE = 1 << 4,
    TGEN_HEADER_FLAG_RECVSIZE = 1 << 5,
    TGEN_HEADER_FLAG_MODELNAME = 1 << 6,
    TGEN_HEADER_FLAG_MODELSEED = 1 << 7,
    TGEN_HEADER_FLAG_MODELMODE = 1 << 8, /* either 'path' or 'graphml' */
    TGEN_HEADER_FLAG_MODELPATH = 1 << 9, /* only if mode is 'path' */
    TGEN_HEADER_FLAG_MODELSIZE = 1 << 10, /* only if mode is 'graphml' */
} TGenStreamHeaderFlags;

struct _TGenStream {
    /* info to help describe this stream object */
    gchar* id; /* the unique vertex id from the graph */
    gchar* hostname; /* our hostname */
    GString* stringBuffer; /* a human-readable string for logging */

    /* describes the type of error if we are in an error state */
    TGenStreamErrorType error;

    /* true if we initiated the stream (i.e., the client) */
    gboolean isCommander;

    /* the configured timeout values */
    gint64 timeoutUSecs;
    gint64 stalloutUSecs;

    /* socket communication layer and buffers */
    TGenTransport* transport;

    /* describes how this stream generates packets */
    TGenMarkovModel* mmodel;
    gboolean mmodelSendPath;

    /* information about the reading side of the connection */
    struct {
        /* current read state */
        TGenStreamRecvState state;

        /* bytes configured or requested by the peer, 0 is a special case (see below) */
        gsize requestedBytes;
        /* if TRUE and requestedBytes is 0, we should not recv anything;
         * if FALSE and requestedBytes is 0, we stop when the model ends */
        gboolean requestedZero;
        /* the number of payload bytes we have read */
        gsize payloadBytes;
        /* the total number of bytes we have read */
        gsize totalBytes;

        /* for buffering reads before processing */
        GString* buffer;
        /* used during authentication */
        guint authIndex;
        /* checksum over payload bytes for integrity */
        GChecksum* checksum;
    } recv;

    /* information about the writing side of the connection */
    struct {
        /* current write state */
        TGenStreamSendState state;

        /* bytes configured or requested by the peer, 0 is a special case (see below) */
        gsize requestedBytes;
        /* if TRUE and requestedBytes is 0, we should not send anything;
         * if FALSE and requestedBytes is 0, we stop when the model ends */
        gboolean requestedZero;
        /* the number of payload bytes we have written */
        gsize payloadBytes;
        /* the total number of bytes we have written */
        gsize totalBytes;

        /* for buffering writes to the transport */
        GString* buffer;
        /* tracks which buffer bytes were already written */
        guint offset;
        /* checksum over payload bytes for integrity */
        GChecksum* checksum;

        /* if non-zero, then our sending model told us to pause sending until
         * g_get_monotonic_time() returns a result >= this time. */
        gint64 deferBarrierMicros;
    } send;

    /* information about the other end of the connection */
    struct {
        gchar* hostname;
        GString* buffer;
        gchar* modelName;
        guint32 modelSeed;
        gsize modelSize;
    } peer;

    /* track timings for time reporting, using g_get_monotonic_time in usec granularity */
    struct {
        gint64 start;
        gint64 command;
        gint64 response;
        gint64 firstPayloadByte;
        gint64 lastPayloadByte;
        gint64 checksum;
        gint64 lastBytesStatusReport;
        gint64 lastTimeStatusReport;
        gint64 lastTimeErrorReport;
        gint64 lastProgress;
    } time;

    /* notification and parameters for when this stream finishes */
    TGenStream_notifyCompleteFunc notify;
    gpointer data1;
    gpointer data2;
    GDestroyNotify destructData1;
    GDestroyNotify destructData2;

    /* memory housekeeping */
    gint refcount;
    guint magic;
};

static const gchar* _tgenstream_recvStateToString(TGenStreamRecvState state) {
    switch(state) {
        /* valid states throughout the life of the recv side of the conn */
        case TGEN_STREAM_RECV_NONE: {
            return "RECV_NONE";
        }
        case TGEN_STREAM_RECV_AUTHENTICATE: {
            return "RECV_AUTHENTICATE";
        }
        case TGEN_STREAM_RECV_HEADER: {
            return "RECV_HEADER";
        }
        case TGEN_STREAM_RECV_MODEL: {
            return "RECV_MODEL";
        }
        case TGEN_STREAM_RECV_PAYLOAD: {
            return "RECV_PAYLOAD";
        }
        case TGEN_STREAM_RECV_CHECKSUM: {
            return "RECV_CHECKSUM";
        }
        /* success and error are terminal states */
        case TGEN_STREAM_RECV_SUCCESS: {
            return "RECV_SUCCESS";
        }
        case TGEN_STREAM_RECV_ERROR:
        default: {
            return "RECV_ERROR";
        }
    }
}

static const gchar* _tgenstream_sendStateToString(TGenStreamSendState state) {
    switch(state) {
        /* valid states throughout the life of the send side of the conn */
        case TGEN_STREAM_SEND_NONE: {
            return "SEND_NONE";
        }
        case TGEN_STREAM_SEND_COMMAND: {
            return "SEND_COMMAND";
        }
        case TGEN_STREAM_SEND_RESPONSE: {
            return "SEND_RESPONSE";
        }
        case TGEN_STREAM_SEND_PAYLOAD: {
            return "SEND_PAYLOAD";
        }
        case TGEN_STREAM_SEND_CHECKSUM: {
            return "SEND_CHECKSUM";
        }
        case TGEN_STREAM_SEND_FLUSH: {
            return "SEND_FLUSH";
        }
        /* success and error are terminal states */
        case TGEN_STREAM_SEND_SUCCESS: {
            return "SEND_SUCCESS";
        }
        case TGEN_STREAM_SEND_ERROR:
        default: {
            return "SEND_ERROR";
        }
    }
}

static const gchar* _tgenstream_errorToString(TGenStreamErrorType error) {
    switch(error) {
        case TGEN_STREAM_ERR_NONE: {
            return "NONE";
        }
        case TGEN_STREAM_ERR_AUTHENTICATE: {
            return "AUTH";
        }
        case TGEN_STREAM_ERR_HEADER: {
            return "HEADER";
        }
        case TGEN_STREAM_ERR_HEADER_INCOMPLETE: {
            return "HEADER_INCOMPLETE";
        }
        case TGEN_STREAM_ERR_HEADER_VERSION: {
            return "HEADER_VERSION";
        }
        case TGEN_STREAM_ERR_HEADER_MODELMODE: {
            return "HEADER_MODELMODE";
        }
        case TGEN_STREAM_ERR_HEADER_MODELPATH: {
            return "HEADER_MODELPATH";
        }
        case TGEN_STREAM_ERR_HEADER_MODELSIZE: {
            return "HEADER_MODELSIZE";
        }
        case TGEN_STREAM_ERR_MODEL: {
            return "MODEL";
        }
        case TGEN_STREAM_ERR_CHECKSUM: {
            return "CHECKSUM";
        }
        case TGEN_STREAM_ERR_READ: {
            return "READ";
        }
        case TGEN_STREAM_ERR_WRITE: {
            return "WRITE";
        }
        case TGEN_STREAM_ERR_READEOF: {
            return "READEOF";
        }
        case TGEN_STREAM_ERR_WRITEEOF: {
            return "WRITEEOF";
        }
        case TGEN_STREAM_ERR_TIMEOUT: {
            return "TIMEOUT";
        }
        case TGEN_STREAM_ERR_STALLOUT: {
            return "STALLOUT";
        }
        case TGEN_STREAM_ERR_PROXY: {
            return "PROXY";
        }
        case TGEN_STREAM_ERR_MISC:
        default: {
            return "MISC";
        }
    }
}

static const gchar* _tgenstream_toString(TGenStream* stream) {
    TGEN_ASSERT(stream);

    if(stream->stringBuffer) {
        return stream->stringBuffer->str;
    }

    stream->stringBuffer = g_string_new(NULL);

    g_string_printf(stream->stringBuffer, "id=%s", stream->id);

    g_string_append_printf(stream->stringBuffer,
            ",name=%s", stream->hostname);
    g_string_append_printf(stream->stringBuffer,
            ",peername=%s", stream->peer.hostname);

    g_string_append_printf(stream->stringBuffer,
            ",sendsize=%"G_GSIZE_FORMAT, stream->send.requestedBytes);
    g_string_append_printf(stream->stringBuffer,
            ",recvsize=%"G_GSIZE_FORMAT, stream->recv.requestedBytes);

    g_string_append_printf(stream->stringBuffer,
            ",sendstate=%s", _tgenstream_sendStateToString(stream->send.state));
    g_string_append_printf(stream->stringBuffer,
            ",recvstate=%s", _tgenstream_recvStateToString(stream->recv.state));

    g_string_append_printf(stream->stringBuffer,
            ",error=%s", _tgenstream_errorToString(stream->error));

    return stream->stringBuffer->str;
}

static void _tgenstream_resetString(TGenStream* stream) {
    TGEN_ASSERT(stream);
    if(stream->stringBuffer) {
        g_string_free(stream->stringBuffer, TRUE);
        stream->stringBuffer = NULL;
    }
}

static void _tgenstream_changeRecvState(TGenStream* stream, TGenStreamRecvState state) {
    TGEN_ASSERT(stream);
    tgen_info("stream %s moving from recv state %s to recv state %s",
            _tgenstream_toString(stream),
            _tgenstream_recvStateToString(stream->recv.state),
            _tgenstream_recvStateToString(state));
    stream->recv.state = state;
    _tgenstream_resetString(stream);
}

static void _tgenstream_changeSendState(TGenStream* stream, TGenStreamSendState state) {
    TGEN_ASSERT(stream);
    tgen_info("stream %s moving from send state %s to send state %s",
            _tgenstream_toString(stream),
            _tgenstream_sendStateToString(stream->send.state),
            _tgenstream_sendStateToString(state));
    stream->send.state = state;
    _tgenstream_resetString(stream);
}

static void _tgenstream_changeError(TGenStream* stream, TGenStreamErrorType error) {
    TGEN_ASSERT(stream);
    tgen_info("stream %s moving from error %s to error %s",
            _tgenstream_toString(stream),
            _tgenstream_errorToString(stream->error),
            _tgenstream_errorToString(error));
    stream->error = error;
    _tgenstream_resetString(stream);
}

static gssize _tgenstream_readBuffered(TGenStream* stream, guchar* buffer, gsize limit) {
    TGEN_ASSERT(stream);
    g_assert(stream->recv.buffer);
    g_assert(stream->recv.buffer->len > 0);

    tgen_debug("Trying to read %"G_GSIZE_FORMAT" bytes, we already have %"G_GSIZE_FORMAT
            " in the read buffer", limit, stream->recv.buffer->len);

    /* we need to drain the recv buffer first */
    if(stream->recv.buffer->len <= limit) {
        /* take all of the recv buffer */
        gsize amount = stream->recv.buffer->len;
        g_memmove(buffer, stream->recv.buffer->str, amount);

        /* don't need the recv buffer any more */
        g_string_free(stream->recv.buffer, TRUE);
        stream->recv.buffer = NULL;

        /* their buffer might be larger than what they need, so return what we have */
        return (gssize) amount;
    } else {
        /* we already have more buffered than the caller wants */
        g_memmove(buffer, stream->recv.buffer->str, limit);

        /* we want to keep the remaining bytes that we did not return */
        GString* newBuffer = g_string_new(&stream->recv.buffer->str[limit]);

        /* replace the read buffer */
        g_string_free(stream->recv.buffer, TRUE);
        stream->recv.buffer = newBuffer;

        /* we read everything they wanted, don't try to read more */
        return (gssize) limit;
    }
}

static gssize _tgenstream_read(TGenStream* stream, guchar* buffer, gsize limit) {
    TGEN_ASSERT(stream);
    g_assert(buffer);
    g_assert(limit > 0);

    /* if there is anything left over in the recv buffer, use that first */
    if(stream->recv.buffer) {
        return _tgenstream_readBuffered(stream, buffer, limit);
    }

    /* by now the recv buffer should be empty */
    g_assert(stream->recv.buffer == NULL);

    /* get more bytes from the transport */
    gssize bytes = tgentransport_read(stream->transport, &(buffer[0]), limit);

    /* check for errors and EOF */
    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_ERROR);
        _tgenstream_changeError(stream, TGEN_STREAM_ERR_READ);

        tgen_critical("read(): transport %s stream %s error %i: %s",
                tgentransport_toString(stream->transport),
                _tgenstream_toString(stream),
                errno, g_strerror(errno));
    } else if(bytes == 0) {
        /* reading an EOF is only an error if we were expecting a certain recvsize */
        if(stream->recv.state != TGEN_STREAM_RECV_PAYLOAD ||
                (stream->recv.requestedBytes > 0 &&
                        stream->recv.payloadBytes < stream->recv.requestedBytes)) {
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_ERROR);
            _tgenstream_changeError(stream, TGEN_STREAM_ERR_READEOF);

            tgen_critical("read(): transport %s stream %s closed unexpectedly",
                    tgentransport_toString(stream->transport),
                    _tgenstream_toString(stream));
        }
    } else {
        stream->recv.totalBytes += bytes;
    }

    return bytes;
}

static GString* _tgenstream_getLine(TGenStream* stream) {
    TGEN_ASSERT(stream);

    /* our read buffer */
    guchar buffer[DEFAULT_STREAM_READ_BUFLEN];

    /* get some data */
    gssize bytes = _tgenstream_read(stream, buffer, DEFAULT_STREAM_READ_BUFLEN);

    /* if there was an error, just return */
    if(bytes <= 0) {
        tgen_debug("Read returned %"G_GSSIZE_FORMAT" when reading a line: error %i: %s",
                bytes, errno, g_strerror(errno));
        return NULL;
    }

    /* we are looking for a full line */
    GString* line = NULL;

    /* keep track if we need to keep any bytes */
    gssize remaining = 0;
    gssize offset = 0;
    gboolean foundNewline = FALSE;

    /* scan the buffer for the newline character */
    for(gssize i = 0; i < bytes; i++) {
        if(buffer[i] == '\n') {
            /* found the end of the line */
            foundNewline = TRUE;

            /* don't include the newline in the returned buffer */
            line = g_string_new_len(buffer, i);

            /* are there more bytes left, make sure not to count the newline character */
            remaining = bytes-i-1;
            offset = i+1;

            break;
        }
    }

    tgen_debug("%s newline in %"G_GSSIZE_FORMAT" bytes",
            foundNewline ? "Found" : "Did not find", bytes);

    /* if we didn't find the newline yet, then we need to keep everything */
    if(!foundNewline) {
        remaining = bytes;
        offset = 0;
    }

    if(remaining > 0) {
        /* store the rest of the bytes in the read buffer for later */
        if(stream->recv.buffer == NULL) {
            stream->recv.buffer = g_string_new_len(&buffer[offset], remaining);
        } else {
            g_string_append_len(stream->recv.buffer, &buffer[offset], remaining);
        }
    }

    /* return the line, which may be NULL if we didn't find it yet */
    return line;
}

static gboolean _tgenstream_readAuthenticate(TGenStream* stream) {
    TGEN_ASSERT(stream);

    guchar authbuf[24];
    gsize amt = 21 - stream->recv.authIndex;
    gssize bytes = _tgenstream_read(stream, &(authbuf[0]), amt);

    if(bytes <= 0 || stream->recv.state != TGEN_STREAM_RECV_AUTHENTICATE) {
        /* we didn't get anything or some error when reading */
        return FALSE;
    }

    /* check the bytes we got */
    for (gssize loc = 0; loc < bytes; loc++) {
        gchar c = authbuf[loc];

        if(stream->recv.authIndex == 20) {
            /* we just read the space following the password, so we are now done */
            tgen_info("stream authentication successful!");
            return TRUE;
        }

        g_assert(stream->recv.authIndex < 20);

        if(c == TGEN_AUTH_PW[stream->recv.authIndex]) {
            /* this character matched */
            stream->recv.authIndex++;
        } else {
            /* password doesn't match */
            tgen_info("stream authentication error: incorrect authentication token");
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_ERROR);
            _tgenstream_changeError(stream, TGEN_STREAM_ERR_AUTHENTICATE);
            return FALSE;
        }
    }

    /* all the bytes that we got matched, but we didn't get everything yet */
    return FALSE;
}

static gboolean _tgenstream_readHeader(TGenStream* stream) {
    TGEN_ASSERT(stream);

    GString* line = _tgenstream_getLine(stream);
    if(!line) {
        /* unable to receive an entire line, wait for the rest */
        return FALSE;
    }

    TGenStreamHeaderFlags parsedKeys = TGEN_HEADER_FLAG_NONE;

    /* we have read the entire command header from the other end */
    TGenStreamErrorType theError = TGEN_STREAM_ERR_NONE;
    gboolean modeIsPath = FALSE;
    gchar* modelPath = NULL;
    gchar* errorCode = NULL;

    tgen_debug("Parsing header string now: %s", line->str);

    /* lets parse the string */
    gchar** parts = g_strsplit(line->str, " ", 0);

    /* parse all of the key=value pairs */
    for(gint i = 0; (theError == TGEN_STREAM_ERR_NONE) && parts != NULL && parts[i] != NULL; i++) {
        gchar** pair = g_strsplit(parts[i], "=", 2);
        gchar* key = pair[0];
        gchar* value = pair[1];

        if(key != NULL && value != NULL) {
            /* we have both key and value in key=value entry */
            if(!g_ascii_strcasecmp(key, "PROTOCOL_VERSION")) {
                gchar** versions = g_strsplit(value, ".", 2);

                /* validate the version */
                if(versions && versions[0] && versions[1]) {
                    gint major = atoi(versions[0]);
                    gint minor = atoi(versions[1]);

                    if(major == TGEN_PROTO_VERS_MAJ) {
                        /* version is OK */
                        parsedKeys |= TGEN_HEADER_FLAG_PROTOCOL;
                    } else {
                        tgen_info("Client running protocol version %s is unsupported", value);
                        theError = TGEN_STREAM_ERR_HEADER_VERSION;
                    }
                }

                if(versions != NULL) {
                    g_strfreev(versions);
                }
            } else if(!g_ascii_strcasecmp(key, "HOSTNAME")) {
                stream->peer.hostname = g_strdup(value);
                parsedKeys |= TGEN_HEADER_FLAG_HOSTNAME;
            } else if(!g_ascii_strcasecmp(key, "TRANSFER_ID")) {
                stream->id = g_strdup(value);
                parsedKeys |= TGEN_HEADER_FLAG_ID;
            } else if(!g_ascii_strcasecmp(key, "CODE")) {
                errorCode = g_strdup(value);
                parsedKeys |= TGEN_HEADER_FLAG_CODE;
            } else if(!g_ascii_strcasecmp(key, "SEND_SIZE")) {
                /* the other side's send size is our recv size */
                if(value[0] == '~') {
                    stream->recv.requestedBytes = 0;
                    stream->recv.requestedZero = TRUE;
                    tgen_info("Peer requested 0 recv bytes on stream %s",
                            _tgenstream_toString(stream));
                } else {
                    stream->recv.requestedBytes = (gsize)g_ascii_strtoull(value, NULL, 10);
                }
                parsedKeys |= TGEN_HEADER_FLAG_SENDSIZE;
            } else if(!g_ascii_strcasecmp(key, "RECV_SIZE")) {
                /* the other side's recv size is our send size */
                if(value[0] == '~') {
                    stream->send.requestedBytes = 0;
                    stream->send.requestedZero = TRUE;
                    tgen_info("Peer requested 0 send bytes on stream %s",
                            _tgenstream_toString(stream));
                } else {
                    stream->send.requestedBytes = (gsize)g_ascii_strtoull(value, NULL, 10);
                }
                parsedKeys |= TGEN_HEADER_FLAG_RECVSIZE;
            } else if(!g_ascii_strcasecmp(key, "MODEL_NAME")) {
                stream->peer.modelName = g_strdup(value);
                parsedKeys |= TGEN_HEADER_FLAG_MODELNAME;
            } else if(!g_ascii_strcasecmp(key, "MODEL_SEED")) {
                stream->peer.modelSeed = (guint32)atol(value);
                parsedKeys |= TGEN_HEADER_FLAG_MODELSEED;
            } else if(!g_ascii_strcasecmp(key, "MODEL_MODE")) {
                if(!g_ascii_strncasecmp(value, "path", 4)) {
                    modeIsPath = TRUE;
                } else if(!g_ascii_strncasecmp(value, "graphml", 4)) {
                    modeIsPath = FALSE;
                } else {
                    theError = TGEN_STREAM_ERR_HEADER_MODELMODE;
                }
                parsedKeys |= TGEN_HEADER_FLAG_MODELMODE;
            } else if(!g_ascii_strcasecmp(key, "MODEL_PATH")) {
                modelPath = g_strdup(value);
                parsedKeys |= TGEN_HEADER_FLAG_MODELPATH;
            } else if(!g_ascii_strcasecmp(key, "MODEL_SIZE")) {
                long long int modelSize = atoll(value);

                /* we allocate memory of this size, so check bounds for safety */
#define TEN_MIB 1024*1024*10
                if(modelSize > 0 && modelSize <= TEN_MIB) {
                    /* the model size is OK */
                    stream->peer.modelSize = (gsize)modelSize;
                    parsedKeys |= TGEN_HEADER_FLAG_MODELSIZE;
                } else {
                    tgen_warning("Client requested model size %lli, "
                            "but we only allow: 0 < size <= 10 MiB", modelSize);
                    theError = TGEN_STREAM_ERR_HEADER_MODELSIZE;
                }
            } else {
                tgen_info("Client sent unrecognized key '%s', ignoring", key);
            }

            if(theError == TGEN_STREAM_ERR_NONE) {
                tgen_debug("successfully parsed key='%s' value='%s'", key, value);
            }
        } else {
            /* we are missing either the key or the value */
            tgen_info("Key value pair '%s' is malformed, ignoring", parts[i]);
        }

        if(pair != NULL) {
            g_strfreev(pair);
        }
    }

    /* free the line buffer */
    if(line != NULL) {
        g_string_free(line, TRUE);
    }
    if(parts != NULL) {
        g_strfreev(parts);
    }

    if(theError == TGEN_STREAM_ERR_NONE) {
        if(stream->isCommander) {
            TGenStreamHeaderFlags required = (TGEN_HEADER_FLAG_PROTOCOL |
                    TGEN_HEADER_FLAG_HOSTNAME | TGEN_HEADER_FLAG_CODE);
            if((parsedKeys & required) != required) {
                tgen_info("Finished parsing header flags, we did not receive all required flags.");
                theError = TGEN_STREAM_ERR_HEADER_INCOMPLETE;
            }

            if(errorCode && g_ascii_strcasecmp(errorCode,
                    _tgenstream_errorToString(TGEN_STREAM_ERR_NONE))) {
                tgen_info("Server returned error code %s", errorCode);
                theError = TGEN_STREAM_ERR_HEADER;
            }
        } else {
            TGenStreamHeaderFlags required = (TGEN_HEADER_FLAG_PROTOCOL |
                    TGEN_HEADER_FLAG_HOSTNAME | TGEN_HEADER_FLAG_ID |
                    TGEN_HEADER_FLAG_SENDSIZE | TGEN_HEADER_FLAG_RECVSIZE |
                    TGEN_HEADER_FLAG_MODELNAME | TGEN_HEADER_FLAG_MODELSEED |
                    TGEN_HEADER_FLAG_MODELMODE);

            if(modeIsPath) {
                required |= TGEN_HEADER_FLAG_MODELPATH;
            } else {
                required |= TGEN_HEADER_FLAG_MODELSIZE;
            }

            if((parsedKeys & required) != required) {
                tgen_info("Finished parsing header flags, we did not receive all required flags.");
                theError = TGEN_STREAM_ERR_HEADER_INCOMPLETE;
            }

            if(modeIsPath) {
                tgen_info("Loading Markov model from the peer-provided path %s", modelPath);

                stream->mmodel = tgenmarkovmodel_newFromPath(stream->peer.modelName,
                        stream->peer.modelSeed, modelPath);

                if(stream->mmodel) {
                    tgen_info("Success loading Markov model from path %s", modelPath);
                } else {
                    tgen_warning("Failure loading Markov model from path %s", modelPath);
                    theError = TGEN_STREAM_ERR_HEADER_MODELPATH;
                }
            } else {
                if(stream->peer.modelSize <= 0) {
                    tgen_warning("We need a graphml model, but the peer sent us model size 0");
                    theError = TGEN_STREAM_ERR_HEADER_MODELSIZE;
                }
            }
        }
    }

    if(theError == TGEN_STREAM_ERR_NONE) {
        /* we need to update our string with the new command info */
        _tgenstream_resetString(stream);
        if(stream->isCommander) {
            /* we are done receive the response */
            stream->time.response = g_get_monotonic_time();
        }
        return TRUE;
    } else {
        /* problem with the header params */
        _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_ERROR);
        _tgenstream_changeError(stream, theError);
        if(stream->isCommander) {
            /* we can't send any more, so we are done */
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_SUCCESS);
        } else {
            /* send an error code as response */
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_RESPONSE);
        }
        return FALSE;
    }
}

static gboolean _tgenstream_readModel(TGenStream* stream) {
    TGEN_ASSERT(stream);

    g_assert(stream->peer.modelSize > 0);

    if(!stream->peer.buffer) {
        stream->peer.buffer = g_string_sized_new(stream->peer.modelSize);
    }

    guchar buffer[DEFAULT_STREAM_READ_BUFLEN];

    gsize remaining = stream->peer.modelSize - stream->peer.buffer->len;
    gsize requested = MIN(DEFAULT_STREAM_READ_BUFLEN, remaining);
    g_assert(requested > 0);

    gssize bytes = _tgenstream_read(stream, &buffer[0], requested);

    if(bytes <= 0 || stream->recv.state != TGEN_STREAM_RECV_MODEL) {
        /* we didn't get anything or some error when reading */
        return FALSE;
    }

    g_string_append_len(stream->peer.buffer, (gchar*)buffer, bytes);

    /* we should not have read more than the size of the model */
    g_assert(stream->peer.buffer->len <= stream->peer.modelSize);

    if(stream->peer.buffer->len == stream->peer.modelSize) {
        tgen_info("Parsing Markov model of size %"G_GSIZE_FORMAT, stream->peer.buffer->len);

        /* done with the model, lets instantiate and parse it */
        stream->mmodel = tgenmarkovmodel_newFromString(stream->peer.modelName,
                stream->peer.modelSeed, stream->peer.buffer);

        /* clean up the read buffer */
        g_string_free(stream->peer.buffer, TRUE);
        stream->peer.buffer = NULL;

        if(stream->mmodel) {
            tgen_info("We received a valid Markov model");
            if(!stream->isCommander) {
                /* we are done receive the command */
                stream->time.command = g_get_monotonic_time();
            }
            return TRUE;
        } else {
            /* some problem with the model */
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_ERROR);
            _tgenstream_changeError(stream, TGEN_STREAM_ERR_MODEL);
            /* send an error code as response */
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_RESPONSE);

            tgen_critical("We received model '%s', but could not instantiate it",
                    stream->peer.modelName);
        }
    }

    return FALSE;
}

static gboolean _tgenstream_readPayload(TGenStream* stream) {
    TGEN_ASSERT(stream);

    if(stream->recv.requestedBytes == 0 && stream->recv.requestedZero) {
        /* we should not have any payload, just move on */
        tgen_debug("Ignoring payload on stream requesting 0 bytes");
        return TRUE;
    }

    guchar buffer[DEFAULT_STREAM_READ_BUFLEN];
    gsize limit = DEFAULT_STREAM_READ_BUFLEN;

    /* if the requested bytes is non-zero, then we have a specific total amount to read */
    if(stream->recv.requestedBytes > 0) {
        g_assert(stream->recv.payloadBytes <= stream->recv.requestedBytes);
        gsize remaining = stream->recv.requestedBytes - stream->recv.payloadBytes;
        limit = MIN(limit, remaining);
    }

    /* we only run through the read loop once in order to give other sockets a chance for i/o */
    gssize bytes = _tgenstream_read(stream, &buffer[0], limit);

    /* EOF is a valid end state for streams where we don't know the payload size */
    if(bytes == 0 && stream->recv.requestedBytes == 0) {
        /* its possible they didnt send us anything */
        if(stream->recv.payloadBytes > 0) {
            stream->time.lastPayloadByte = g_get_monotonic_time();
        }

        return TRUE;
    }

    if(bytes <= 0 || stream->recv.state != TGEN_STREAM_RECV_PAYLOAD) {
        /* we didn't get anything or some error when reading */
        return FALSE;
    }

    if(stream->recv.payloadBytes == 0) {
        stream->time.firstPayloadByte = g_get_monotonic_time();
    }

    stream->recv.payloadBytes += bytes;

    /* only track the checksum if we know the final size.
     * TODO this needs to be updated if we support checksums on general streams. */
    if(stream->recv.requestedBytes > 0) {
        g_checksum_update(stream->recv.checksum, buffer, bytes);
    }

    /* valid end state for streams where we know the payload size */
    if(stream->recv.requestedBytes > 0 &&
            stream->recv.payloadBytes >= stream->recv.requestedBytes) {
        tgen_debug("Finished reading %"G_GSIZE_FORMAT
                " requested payload bytes", stream->recv.payloadBytes);
        stream->time.lastPayloadByte = g_get_monotonic_time();
        return TRUE;
    }

    /* still want more */
    return FALSE;
}

static gboolean _tgenstream_readChecksum(TGenStream* stream) {
    TGEN_ASSERT(stream);

    if(stream->recv.requestedBytes == 0) {
        /* we don't handle checksums if we don't know the total size, so just move on.
         * TODO this needs to be updated if we support checksums on general streams. */
        tgen_debug("Ignoring checksum on stream with no requested bytes");
        return TRUE;
    }

    GString* line = _tgenstream_getLine(stream);
    if(!line) {
        return FALSE;
    }

    /* we have read the entire checksum from the other end */
    stream->time.checksum = g_get_monotonic_time();

    gchar** parts = g_strsplit(line->str, " ", 0);
    const gchar* receivedSum = parts[1];

    gchar* computedSum = g_strdup(g_checksum_get_string(stream->recv.checksum));
    g_assert(computedSum);

    gssize sumLength = g_checksum_type_get_length(G_CHECKSUM_MD5);
    g_assert(sumLength >= 0);

    gboolean matched = (g_ascii_strncasecmp(computedSum, receivedSum, (gsize)sumLength) == 0);
    gboolean isSuccess = FALSE;

    /* check that the sums match */
    if(receivedSum && matched) {
        tgen_info("transport %s stream %s MD5 checksums passed: computed=%s received=%s",
                tgentransport_toString(stream->transport), _tgenstream_toString(stream),
                computedSum, receivedSum);
        isSuccess = TRUE;
    } else {
        if(receivedSum) {
            tgen_message("MD5 checksums failed: computed=%s received=%s", computedSum, receivedSum);
        } else {
            tgen_message("MD5 checksums failed: received sum is NULL");
        }
        _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_ERROR);
        _tgenstream_changeError(stream, TGEN_STREAM_ERR_CHECKSUM);
        isSuccess = FALSE;
    }

    g_free(computedSum);
    g_strfreev(parts);
    g_string_free(line, TRUE);

    return isSuccess;
}

static void _tgenstream_onReadable(TGenStream* stream) {
    TGEN_ASSERT(stream);

    tgen_debug("active stream %s is readable", _tgenstream_toString(stream));
    gsize startBytes = stream->recv.totalBytes;

    if(stream->recv.state == TGEN_STREAM_RECV_AUTHENTICATE) {
        if(_tgenstream_readAuthenticate(stream)) {
            /* want to receive the header next */
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_HEADER);
        }
    }

    if(stream->recv.state == TGEN_STREAM_RECV_HEADER) {
        if(_tgenstream_readHeader(stream)) {
            if(stream->isCommander) {
                /* now we can move to the payload stage */
                _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_PAYLOAD);
            } else {
                /* need to receive the model next */
                _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_MODEL);
            }
        }
    }

    /* only the non-commander */
    if(stream->recv.state == TGEN_STREAM_RECV_MODEL) {
        g_assert(!stream->isCommander);
        /* if we already have the mmodel (loaded from a path), then don't read any graphml */
        if(stream->mmodel || _tgenstream_readModel(stream)) {
            /* now we send the response */
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_RESPONSE);

            /* and we start receiving the payload */
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_PAYLOAD);
        }
    }

    if(stream->recv.state == TGEN_STREAM_RECV_PAYLOAD) {
        if(_tgenstream_readPayload(stream)) {
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_CHECKSUM);
        }
    }

    if(stream->recv.state == TGEN_STREAM_RECV_CHECKSUM) {
        if(_tgenstream_readChecksum(stream)) {
            /* yay, now we are done! */
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_SUCCESS);
        }
    }

    gsize endBytes = stream->recv.totalBytes;
    gsize totalBytes = endBytes - startBytes;

    tgen_debug("active stream %s read %"G_GSIZE_FORMAT" more bytes",
            _tgenstream_toString(stream), totalBytes);

    if(totalBytes > 0) {
        stream->time.lastProgress = g_get_monotonic_time();
    }
}

static GString* _tgenstream_getRandomString(gsize size) {
    /* call rand() once to limit overhead */
    gint r = rand() % 26;
    gchar c = (gchar)('a' + r);
    /* fill the buffer. this was more efficient than malloc/memset and then g_string_new  */
    GString* buffer = g_string_new_len(NULL, (gssize)size);
    for(gsize i = 0; i < size; i++) {
        /* this is an inline function so it's fast */
        g_string_append_c(buffer, c);
    }
    return buffer;
}

static gsize _tgenstream_flushOut(TGenStream* stream) {
    TGEN_ASSERT(stream);

    if(!stream->send.buffer) {
        return 0;
    }

    gchar* position = &(stream->send.buffer->str[stream->send.offset]);
    gsize limit = stream->send.buffer->len - stream->send.offset;
    gssize bytes = tgentransport_write(stream->transport, position, limit);

    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_ERROR);
        _tgenstream_changeError(stream, TGEN_STREAM_ERR_WRITE);

        tgen_critical("write(): transport %s stream %s error %i: %s",
                tgentransport_toString(stream->transport),
                _tgenstream_toString(stream),
                errno, g_strerror(errno));
    } else if(bytes == 0) {
        _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_ERROR);
        _tgenstream_changeError(stream, TGEN_STREAM_ERR_WRITEEOF);

        tgen_critical("write(): transport %s stream %s closed unexpectedly",
                tgentransport_toString(stream->transport),
                _tgenstream_toString(stream));
    } else if(bytes > 0) {
        stream->send.offset += bytes;

        /* if we wrote everything, free the buffer */
        if(stream->send.offset >= stream->send.buffer->len) {
            stream->send.offset = 0;
            g_string_free(stream->send.buffer, TRUE);
            stream->send.buffer = NULL;
        }

        stream->send.totalBytes += bytes;
        return (gsize) bytes;
    }

    return 0;
}

static gboolean _tgenstream_writeCommand(TGenStream* stream) {
    TGEN_ASSERT(stream);

    /* buffer the command if we have not done that yet */
    if(!stream->send.buffer) {
        stream->send.buffer = g_string_new(NULL);

        /* we may send the model as a string */
        GString* modelGraphml = NULL;

        /* Send useful information about the stream. All but the PW are tagged
         * to make it easier to extend in the future. */
        g_string_printf(stream->send.buffer, "%s", TGEN_AUTH_PW);

        g_string_append_printf(stream->send.buffer,
                " PROTOCOL_VERSION=%i.%i", TGEN_PROTO_VERS_MAJ, TGEN_PROTO_VERS_MIN);
        g_string_append_printf(stream->send.buffer,
                " HOSTNAME=%s", stream->hostname);
        g_string_append_printf(stream->send.buffer,
                " TRANSFER_ID=%s", stream->id);
        if(stream->send.requestedZero) {
            g_string_append_printf(stream->send.buffer,
                    " SEND_SIZE=~");
        } else {
            g_string_append_printf(stream->send.buffer,
                    " SEND_SIZE=%"G_GSIZE_FORMAT, stream->send.requestedBytes);
        }
        if(stream->recv.requestedZero) {
            g_string_append_printf(stream->send.buffer,
                    " RECV_SIZE=~");
        } else {
            g_string_append_printf(stream->send.buffer,
                    " RECV_SIZE=%"G_GSIZE_FORMAT, stream->recv.requestedBytes);
        }
        g_string_append_printf(stream->send.buffer,
                " MODEL_NAME=%s", tgenmarkovmodel_getName(stream->mmodel));
        g_string_append_printf(stream->send.buffer,
                " MODEL_SEED=%"G_GUINT32_FORMAT, tgenmarkovmodel_getSeed(stream->mmodel));

        if(stream->mmodelSendPath) {
            const gchar* path = tgenmarkovmodel_getPath(stream->mmodel);
            g_assert(path);

            g_string_append_printf(stream->send.buffer,
                    " MODEL_MODE=path");
            g_string_append_printf(stream->send.buffer,
                    " MODEL_PATH=%s", path);
        } else {
            modelGraphml = tgenmarkovmodel_toGraphmlString(stream->mmodel);
            g_assert(modelGraphml);

            g_string_append_printf(stream->send.buffer,
                    " MODEL_MODE=graphml");
            g_string_append_printf(stream->send.buffer,
                    " MODEL_SIZE=%"G_GSIZE_FORMAT, modelGraphml->len);
        }

        /* close off the tagged data with a newline */
        g_string_append_c(stream->send.buffer, '\n');

        if(modelGraphml) {
            /* then we write the graphml string of the specified size */
            g_string_append_printf(stream->send.buffer, "%s", modelGraphml->str);

            /* clean up */
            g_string_free(modelGraphml, TRUE);
        }
    }

    _tgenstream_flushOut(stream);

    if(!stream->send.buffer) {
        /* entire command was sent, move to payload phase */
        stream->time.command = g_get_monotonic_time();
        return TRUE;
    } else {
        /* still need to write/flush more */
        return FALSE;
    }
}

static gboolean _tgenstream_writeResponse(TGenStream* stream) {
    TGEN_ASSERT(stream);

    /* buffer the command if we have not done that yet */
    if(!stream->send.buffer) {
        stream->send.buffer = g_string_new(NULL);
        g_string_printf(stream->send.buffer, "%s", TGEN_AUTH_PW);

        g_string_append_printf(stream->send.buffer,
                " PROTOCOL_VERSION=%i.%i", TGEN_PROTO_VERS_MAJ, TGEN_PROTO_VERS_MIN);
        g_string_append_printf(stream->send.buffer,
                " HOSTNAME=%s", stream->hostname);
        g_string_append_printf(stream->send.buffer,
                " CODE=%s", _tgenstream_errorToString(stream->error));

        /* close off the tagged data with a newline */
        g_string_append_c(stream->send.buffer, '\n');
    }

    _tgenstream_flushOut(stream);

    if(!stream->send.buffer) {
        /* entire response was sent */
        stream->time.response = g_get_monotonic_time();
        return TRUE;
    } else {
        /* unable to send entire command, wait for next chance to write */
        return FALSE;
    }
}

static gboolean _tgenstream_writePayload(TGenStream* stream) {
    TGEN_ASSERT(stream);

    if(!stream->mmodel) {
        tgen_info("Trying to write payload but we have no Markov model");
        _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_ERROR);
        _tgenstream_changeError(stream, TGEN_STREAM_ERR_MODEL);
        return FALSE;
    }

    /* try to flush any leftover bytes */
    stream->send.payloadBytes += _tgenstream_flushOut(stream);

    if (stream->send.buffer) {
        /* still need to write it next time */
        return FALSE;
    }

    /* we are done if we sent the total requested bytes or have no requested
     * bytes but reached the end of the model */
    if(stream->send.requestedBytes > 0) {
        if(stream->send.payloadBytes >= stream->send.requestedBytes) {
            return TRUE;
        }
    } else {
        if(stream->send.requestedZero) {
            /* we should not send anything */
            return TRUE;
        } else if(tgenmarkovmodel_isInEndState(stream->mmodel)) {
            /* they didn't request 0, so we end when the model ends */
            return TRUE;
        }
    }

    /* We limit our write buffer size in order to give other sockets a chance for i/o.
     * This allows us to return to the epoll loop and service other active streams.
     * If we don't do this, it's possible that this single stream will block the others. */
    gsize limit = DEFAULT_STREAM_WRITE_BUFLEN;
    if(stream->send.requestedBytes > 0) {
        g_assert(stream->send.requestedBytes >= stream->send.payloadBytes);
        gsize remaining = stream->send.requestedBytes - stream->send.payloadBytes;
        limit = MIN(remaining, DEFAULT_STREAM_WRITE_BUFLEN);
    }

    gsize cumulativeSize = 0;
    guint64 cumulativeDelay = 0;
    guint64 interPacketDelay = 0;

    while(cumulativeSize < limit) {
        guint64 obsDelay = 0;
        Observation obs = tgenmarkovmodel_getNextObservation(stream->mmodel, &obsDelay);

        if((stream->isCommander && obs == OBSERVATION_TO_ORIGIN)
                || (!stream->isCommander && obs == OBSERVATION_TO_SERVER)) {
            /* the other end is sending us a packet, we have nothing to do.
             * but this delay should be included in the delay for our next outgoing packet. */
            interPacketDelay += obsDelay;
            cumulativeDelay += obsDelay;
        } else if((stream->isCommander && obs == OBSERVATION_TO_SERVER)
                || (!stream->isCommander && obs == OBSERVATION_TO_ORIGIN)) {
            /* this means we should send a packet */
            cumulativeSize += TGEN_MMODEL_PACKET_DATA_SIZE;
            /* since we sent a packet, now we reset the delay */
            interPacketDelay = obsDelay;
            cumulativeDelay += obsDelay;
        } else if(obs == OBSERVATION_END) {
            /* if we have a specific requested send size, we need to reset and keep sending.
             * we never reset when requestedBytes is 0 (it either means no bytes, or end
             * when the model ends) */
            if(stream->send.requestedBytes > 0) {
                tgenmarkovmodel_reset(stream->mmodel);
            } else {
                /* the model reached the end and we should stop sending */
                break;
            }
        } else {
            /* we should not be getting other observation types in a stream model */
            tgen_info("Got a non-packet model observation from the Markov model");
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_ERROR);
            _tgenstream_changeError(stream, TGEN_STREAM_ERR_MODEL);
            return FALSE;
        }

        if(interPacketDelay > TGEN_MMODEL_MICROS_AT_ONCE) {
            /* pause before we continue sending more */
            stream->send.deferBarrierMicros = g_get_monotonic_time() + (gint64)interPacketDelay;
            break;
        }
    }

    gsize newBufLen = MIN(limit, cumulativeSize);
    if(newBufLen > 0) {
        stream->send.buffer = _tgenstream_getRandomString(newBufLen);

        /* only send a checksum when we know the final size.
         * TODO this needs to be updated if we support checksums on general streams. */
        if(stream->send.requestedBytes > 0) {
            g_checksum_update(stream->send.checksum, (guchar*)stream->send.buffer->str,
                    (gssize)stream->send.buffer->len);
        }

        stream->send.payloadBytes += _tgenstream_flushOut(stream);
    }

    /* return false so we stay in the payload state so we can flush and
     * double check the end conditions again. */
    return FALSE;
}

static gboolean _tgenstream_writeChecksum(TGenStream* stream) {
    TGEN_ASSERT(stream);

    /* buffer the checksum if we have not done that yet */
    if(!stream->send.buffer) {
        stream->send.buffer = g_string_new(NULL);
        g_string_printf(stream->send.buffer, "MD5 %s",
                g_checksum_get_string(stream->send.checksum));
        g_string_append_c(stream->send.buffer, '\n');
        tgen_debug("Sending checksum '%s'", stream->send.buffer->str);
    }

    _tgenstream_flushOut(stream);

    if(!stream->send.buffer) {
        /* we were able to send all of the checksum */
        stream->time.checksum = g_get_monotonic_time();
        return TRUE;
    } else {
        /* unable to send entire checksum, wait for next chance to write */
        return FALSE;
    }
}

static void _tgenstream_onWritable(TGenStream* stream) {
    TGEN_ASSERT(stream);

    tgen_debug("active stream %s is writable", _tgenstream_toString(stream));
    gsize startBytes = stream->send.totalBytes;

    /* if we previously wanted to defer writes, reset the cache */
    if(stream->send.deferBarrierMicros > 0) {
        g_assert(g_get_monotonic_time() >= stream->send.deferBarrierMicros);
        stream->send.deferBarrierMicros = 0;
    }

    if(stream->send.state == TGEN_STREAM_SEND_COMMAND) {
        /* only the commander sends the command */
        g_assert(stream->isCommander);

        if(_tgenstream_writeCommand(stream)) {
            /* now we start waiting for the response */
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_AUTHENTICATE);

            /* and we start sending the payload */
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_PAYLOAD);
        }
    }

    if(stream->send.state == TGEN_STREAM_SEND_RESPONSE) {
        /* only the non-commander sends the response */
        g_assert(!stream->isCommander);

        if(_tgenstream_writeResponse(stream)) {
            if(stream->error == TGEN_STREAM_ERR_NONE) {
                /* start sending the payload */
                _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_PAYLOAD);
            } else {
                /* we just wrote a response with an error code, sending is done now */
                _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_SUCCESS);
            }
        }
    }

    if(stream->send.state == TGEN_STREAM_SEND_PAYLOAD) {
        if(_tgenstream_writePayload(stream)) {
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_CHECKSUM);
        }
    }

    if(stream->send.state == TGEN_STREAM_SEND_CHECKSUM) {
        if(_tgenstream_writeChecksum(stream)) {
            /* now we just need to make sure we finished flushing */
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_FLUSH);
        }
    }

    if(stream->send.state == TGEN_STREAM_SEND_FLUSH) {
        /* make sure we flush the rest of the send buffer */
        _tgenstream_flushOut(stream);
        if(stream->send.buffer == NULL) {
            /* yay, now we are done sending everything! */
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_SUCCESS);

            tgen_debug("Stream finished writing, shutting down transport writes now");
            tgentransport_shutdownWrites(stream->transport);
        }
    }

    gsize endBytes = stream->send.totalBytes;
    gsize totalBytes = endBytes - startBytes;
    tgen_debug("active stream %s wrote %"G_GSIZE_FORMAT" more bytes",
                _tgenstream_toString(stream), totalBytes);

    if(totalBytes > 0) {
        stream->time.lastProgress = g_get_monotonic_time();
    }
}

static gchar* _tgenstream_getBytesStatusReport(TGenStream* stream) {
    TGEN_ASSERT(stream);

    GString* buffer = g_string_new(NULL);

    g_string_printf(buffer,
            "total-bytes-recv=%"G_GSIZE_FORMAT, stream->recv.totalBytes);
    g_string_append_printf(buffer,
            " total-bytes-send=%"G_GSIZE_FORMAT, stream->send.totalBytes);
    g_string_append_printf(buffer,
            " payload-bytes-recv=%"G_GSIZE_FORMAT, stream->recv.payloadBytes);
    g_string_append_printf(buffer,
            " payload-bytes-send=%"G_GSIZE_FORMAT, stream->send.payloadBytes);

    if(stream->recv.requestedBytes > 0) {
        gdouble progress = (gdouble)stream->recv.payloadBytes /
                (gdouble)stream->recv.requestedBytes * 100.0f;
        g_string_append_printf(buffer, " payload-progress-recv=%.2f%%", progress);
    } else if(stream->recv.state == TGEN_STREAM_RECV_SUCCESS ||
            (stream->recv.requestedBytes == 0 && stream->recv.requestedZero)) {
        g_string_append_printf(buffer, " payload-progress-recv=%.2f%%", 100.0f);
    } else {
        g_string_append_printf(buffer, " payload-progress-recv=?");
    }

    if(stream->send.requestedBytes > 0) {
        gdouble progress = (gdouble)stream->send.payloadBytes /
                (gdouble)stream->send.requestedBytes * 100.0f;
        g_string_append_printf(buffer, " payload-progress-send=%.2f%%", progress);
    } else if(stream->send.state == TGEN_STREAM_SEND_SUCCESS ||
            (stream->send.requestedBytes == 0 && stream->send.requestedZero)) {
        g_string_append_printf(buffer, " payload-progress-send=%.2f%%", 100.0f);
    } else {
        g_string_append_printf(buffer, " payload-progress-send=?");
    }

    return g_string_free(buffer, FALSE);
}

static gchar* _tgenstream_getTimeStatusReport(TGenStream* stream) {
    TGEN_ASSERT(stream);

    gchar* proxyTimeStr = tgentransport_getTimeStatusReport(stream->transport);

    gint64 command = (stream->time.command > 0 && stream->time.start > 0) ?
            (stream->time.command - stream->time.start) : -1;
    gint64 response = (stream->time.response > 0 && stream->time.start > 0) ?
            (stream->time.response - stream->time.start) : -1;
    gint64 firstPayloadByte = (stream->time.firstPayloadByte > 0 && stream->time.start > 0) ?
            (stream->time.firstPayloadByte - stream->time.start) : -1;
    gint64 lastPayloadByte = (stream->time.lastPayloadByte > 0 && stream->time.start > 0) ?
            (stream->time.lastPayloadByte - stream->time.start) : -1;
    gint64 checksum = (stream->time.checksum > 0 && stream->time.start > 0) ?
            (stream->time.checksum - stream->time.start) : -1;

    GString* buffer = g_string_new(proxyTimeStr);

    /* print the times in milliseconds */
    g_string_append_printf(buffer,
            " usecs-to-command=%"G_GINT64_FORMAT" usecs-to-response=%"G_GINT64_FORMAT" "
            "usecs-to-first-byte=%"G_GINT64_FORMAT" usecs-to-last-byte=%"G_GINT64_FORMAT" "
            "usecs-to-checksum=%"G_GINT64_FORMAT,
            command, response, firstPayloadByte, lastPayloadByte, checksum);

    g_free(proxyTimeStr);
    return g_string_free(buffer, FALSE);
}

static void _tgenstream_log(TGenStream* stream, gboolean wasActive) {
    TGEN_ASSERT(stream);

    if(stream->recv.state == TGEN_STREAM_RECV_ERROR ||
            stream->send.state == TGEN_STREAM_SEND_ERROR) {
        /* we had an error at some point and will unlikely be able to complete.
         * only log an error once. */
        if(stream->time.lastTimeErrorReport == 0) {
            gchar* bytesMessage = _tgenstream_getBytesStatusReport(stream);
            gchar* timeMessage = _tgenstream_getTimeStatusReport(stream);

            tgen_message("[stream-error] transport %s stream %s %s %s",
                    tgentransport_toString(stream->transport),
                    _tgenstream_toString(stream), bytesMessage, timeMessage);

            gint64 now = g_get_monotonic_time();
            stream->time.lastBytesStatusReport = now;
            stream->time.lastTimeErrorReport = now;
            g_free(bytesMessage);
            g_free(timeMessage);
        }
    } else if(stream->recv.state == TGEN_STREAM_RECV_SUCCESS &&
            stream->send.state == TGEN_STREAM_SEND_SUCCESS) {
        /* we completed the stream. yay. only log once. */
        if(stream->time.lastTimeStatusReport == 0) {
            gchar* bytesMessage = _tgenstream_getBytesStatusReport(stream);
            gchar* timeMessage = _tgenstream_getTimeStatusReport(stream);

            tgen_message("[stream-complete] transport %s stream %s %s %s",
                    tgentransport_toString(stream->transport),
                    _tgenstream_toString(stream), bytesMessage, timeMessage);

            gint64 now = g_get_monotonic_time();
            stream->time.lastBytesStatusReport = now;
            stream->time.lastTimeStatusReport = now;
            g_free(bytesMessage);
            g_free(timeMessage);
        }
    } else {
        /* the stream is still working. only log on new activity */
        if(wasActive) {
            gchar* bytesMessage = _tgenstream_getBytesStatusReport(stream);

            /* TODO should this be logged at debug? */
            tgen_info("[stream-status] transport %s stream %s %s",
                    tgentransport_toString(stream->transport),
                    _tgenstream_toString(stream), bytesMessage);

            stream->time.lastBytesStatusReport = g_get_monotonic_time();;
            g_free(bytesMessage);
        }
    }
}

static TGenIOResponse _tgenstream_runTransportEventLoop(TGenStream* stream, TGenEvent events) {
    TGenIOResponse response;
    memset(&response, 0, sizeof(TGenIOResponse));

    TGenEvent retEvents = tgentransport_onEvent(stream->transport, events);
    if(retEvents == TGEN_EVENT_NONE) {
        /* proxy failed */
        tgen_critical("proxy connection failed, stream cannot begin");
        _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_ERROR);
        _tgenstream_changeError(stream, TGEN_STREAM_ERR_PROXY);
        _tgenstream_log(stream, FALSE);

        /* return DONE to the io module so it does deregistration */
        response.events = TGEN_EVENT_DONE;
    } else {
        stream->time.lastProgress = g_get_monotonic_time();
        if(retEvents & TGEN_EVENT_DONE) {
            /* proxy is connected and ready, now its our turn */
            response.events = TGEN_EVENT_READ|TGEN_EVENT_WRITE;
        } else {
            /* proxy in progress */
            response.events = retEvents;
        }
    }

    return response;
}

static TGenEvent _tgenstream_computeWantedEvents(TGenStream* stream) {
    TGEN_ASSERT(stream);

    /* the events we need in order to make progress */
    TGenEvent wantedEvents = TGEN_EVENT_NONE;

    /* each part of the conn is done if we have reached success or error */
    gboolean recvDone = (stream->recv.state == TGEN_STREAM_RECV_SUCCESS ||
            stream->recv.state == TGEN_STREAM_RECV_ERROR) ? TRUE : FALSE;
    /* for sending, we also need to make sure we flush the outbuf after success */
    gboolean sendDone = (stream->send.state == TGEN_STREAM_SEND_SUCCESS ||
            stream->send.state == TGEN_STREAM_SEND_ERROR) ? TRUE : FALSE;

    /* the stream is done if both sending and receiving states are done,
     * and we have flushed our entire send buffer */
    if(recvDone && sendDone) {
        wantedEvents |= TGEN_EVENT_DONE;
    } else {
        if(!recvDone && stream->recv.state != TGEN_STREAM_RECV_NONE) {
            wantedEvents |= TGEN_EVENT_READ;
        }

        if(!sendDone && stream->send.state != TGEN_STREAM_SEND_NONE) {
            /* check if we should defer writes */
            if(stream->send.deferBarrierMicros > 0) {
                wantedEvents |= TGEN_EVENT_WRITE_DEFERRED;
            } else {
                wantedEvents |= TGEN_EVENT_WRITE;
            }
        }
    }

    return wantedEvents;
}

static TGenIOResponse _tgenstream_runStreamEventLoop(TGenStream* stream, TGenEvent events) {
    TGEN_ASSERT(stream);

    gsize recvBefore = stream->recv.payloadBytes;
    gsize sendBefore = stream->send.payloadBytes;

    /* process the events */
    if(events & TGEN_EVENT_READ) {
        _tgenstream_onReadable(stream);
    }
    if(events & TGEN_EVENT_WRITE) {
        /* process the events */
        _tgenstream_onWritable(stream);
    }

    /* check if we want to log any progress information */
    gboolean recvActive = (stream->recv.payloadBytes > recvBefore) ? TRUE : FALSE;
    gboolean sendActive = (stream->send.payloadBytes > sendBefore) ? TRUE : FALSE;
    gboolean wasActive = (recvActive || sendActive) ? TRUE : FALSE;
    _tgenstream_log(stream, wasActive);

    /* figure out which events we need to advance */
    TGenIOResponse response;
    memset(&response, 0, sizeof(TGenIOResponse));
    response.events = _tgenstream_computeWantedEvents(stream);

    /* if the stream is done, notify the driver */
    if(response.events & TGEN_EVENT_DONE) {
        if(stream->notify) {
            /* execute the callback to notify that we are complete */
            gboolean wasSuccess = stream->error == TGEN_STREAM_ERR_NONE ? TRUE : FALSE;
            stream->notify(stream->data1, stream->data2, wasSuccess);
            /* make sure we only do the notification once */
            stream->notify = NULL;
        }
    } else if(response.events & TGEN_EVENT_WRITE_DEFERRED) {
        g_assert(stream->send.deferBarrierMicros > 0);
        response.deferUntilUSec = stream->send.deferBarrierMicros;
    }

    return response;
}

TGenIOResponse tgenstream_onEvent(TGenStream* stream, gint descriptor, TGenEvent events) {
    TGEN_ASSERT(stream);

    if(stream->transport && tgentransport_wantsEvents(stream->transport)) {
        /* transport layer wants to do some IO, redirect as needed */
        return _tgenstream_runTransportEventLoop(stream, events);
    } else {
        /* transport layer is happy, our turn to start the stream */
        return _tgenstream_runStreamEventLoop(stream, events);
    }
}

gboolean tgenstream_onCheckTimeout(TGenStream* stream, gint descriptor) {
    TGEN_ASSERT(stream);

    /* the io module is checking to see if we are in a timeout state. if we are, then
     * the stream will be cancelled, de-registered and destroyed. */
    gint64 now = g_get_monotonic_time();

    gint64 stalloutCutoff = stream->time.lastProgress + stream->stalloutUSecs;
    gint64 timeoutCutoff = stream->time.start + stream->timeoutUSecs;

    gboolean madeSomeProgress = (stream->time.lastProgress > 0) ? TRUE : FALSE;
    gboolean stalled = (madeSomeProgress && (now >= stalloutCutoff)) ? TRUE : FALSE;
    gboolean tookTooLong = (now >= timeoutCutoff) ? TRUE : FALSE;

    if((stream->stalloutUSecs > 0 && stalled) ||
            (stream->timeoutUSecs > 0 && tookTooLong)) {
        /* if the recv side is in a non-terminal state, change it to error */
        if(stream->recv.state != TGEN_STREAM_RECV_SUCCESS &&
                stream->recv.state != TGEN_STREAM_RECV_ERROR) {
            _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_ERROR);
        }

        /* if the send side is in a non-terminal state, change it to error */
        if(stream->send.state != TGEN_STREAM_SEND_SUCCESS &&
                stream->send.state != TGEN_STREAM_SEND_ERROR) {
            _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_ERROR);
        }

        /* it's either a stallout or timeout error */
        if(stalled) {
            _tgenstream_changeError(stream, TGEN_STREAM_ERR_STALLOUT);
        } else {
            _tgenstream_changeError(stream, TGEN_STREAM_ERR_TIMEOUT);
        }

        /* log the error and notify driver before the stream is destroyed */
        _tgenstream_runStreamEventLoop(stream, TGEN_EVENT_NONE);

        /* this stream will be destroyed by the io module */
        return TRUE;
    } else {
        /* this stream is still in progress */
        return FALSE;
    }
}

TGenStream* tgenstream_new(const gchar* idStr, TGenStreamOptions* options,
        TGenMarkovModel* mmodel, TGenTransport* transport,
        TGenStream_notifyCompleteFunc notify,
        gpointer data1, GDestroyNotify destructData1,
        gpointer data2, GDestroyNotify destructData2) {
    TGenStream* stream = g_new0(TGenStream, 1);
    stream->magic = TGEN_MAGIC;
    stream->refcount = 1;

    stream->time.start = g_get_monotonic_time();

    stream->notify = notify;
    stream->data1 = data1;
    stream->destructData1 = destructData1;
    stream->data2 = data2;
    stream->destructData2 = destructData2;

    /* get the hostname */
    gchar nameBuffer[256];
    memset(nameBuffer, 0, 256);
    stream->hostname = (0 == tgenconfig_gethostname(nameBuffer, 255)) ? g_strdup(nameBuffer) : NULL;

    /* save the stream context values */
    if(idStr) {
        stream->id = g_strdup(idStr);
    }

    /* the timeout after which we abandon this stream */
    guint64 timeoutNanos = (options && options->timeoutNanos.isSet) ?
            options->timeoutNanos.value : DEFAULT_STREAM_TIMEOUT_NSEC;
    stream->timeoutUSecs = (gint64)(timeoutNanos / 1000);

    /* the stallout after which we abandon this stream */
    guint64 stalloutNanos = (options && options->stalloutNanos.isSet) ?
            options->stalloutNanos.value : DEFAULT_STREAM_STALLOUT_NSEC;
    stream->stalloutUSecs = (gint64)(stalloutNanos / 1000);

    if(options && options->sendSize.isSet) {
        stream->send.requestedBytes = options->sendSize.value;
        if(options->sendSize.value == 0) {
            /* they explicitly requested 0, so they want 0 send bytes */
            stream->send.requestedZero = TRUE;
        }
    } else {
        /* they didn't set a size, so we end when the model ends */
        stream->send.requestedBytes = 0;
        stream->send.requestedZero = FALSE;
    }

    /* if they explicitly requested 0, they mean they want 0 recv bytes */
    if(options && options->recvSize.isSet) {
        stream->recv.requestedBytes = options->recvSize.value;
        if(options->recvSize.value == 0) {
            /* they explicitly requested 0, so they want 0 recv bytes */
            stream->recv.requestedZero = TRUE;
        }
    } else {
        /* they didn't set a size, so we end when the model ends */
        stream->recv.requestedBytes = 0;
        stream->send.requestedZero = FALSE;
    }

    stream->send.checksum = g_checksum_new(G_CHECKSUM_MD5);
    stream->recv.checksum = g_checksum_new(G_CHECKSUM_MD5);

    if(transport) {
        tgentransport_ref(transport);
        stream->transport = transport;
    }

    if(mmodel) {
        tgenmarkovmodel_ref(mmodel);
        stream->mmodel = mmodel;

        /* send the path instead of the full graphml to the server if configured,
         * but only if the mmodel was initiated from a path rather than a string */
        if(options->packetModelMode.isSet &&
                !g_ascii_strcasecmp(options->packetModelMode.value, "path") &&
                tgenmarkovmodel_getPath(mmodel) != NULL) {
            stream->mmodelSendPath = TRUE;
        }

        /* the commander first sends the command */
        stream->isCommander = TRUE;
        _tgenstream_changeSendState(stream, TGEN_STREAM_SEND_COMMAND);
    } else {
        /* the non-commander waits for the command, the first part is authentication */
        stream->isCommander = FALSE;
        _tgenstream_changeRecvState(stream, TGEN_STREAM_RECV_AUTHENTICATE);
    }

    return stream;
}

static void _tgenstream_free(TGenStream* stream) {
    TGEN_ASSERT(stream);

    if(stream->hostname) {
        g_free(stream->hostname);
    }

    if(stream->id) {
        g_free(stream->id);
    }

    if(stream->stringBuffer) {
        g_string_free(stream->stringBuffer, TRUE);
    }

    if(stream->peer.hostname) {
        g_free(stream->peer.hostname);
    }

    if(stream->peer.modelName) {
        g_free(stream->peer.modelName);
    }

    if(stream->peer.buffer) {
        g_string_free(stream->peer.buffer, TRUE);
    }

    if(stream->send.buffer) {
        g_string_free(stream->send.buffer, TRUE);
    }

    if(stream->recv.buffer) {
        g_string_free(stream->recv.buffer, TRUE);
    }

    if(stream->send.checksum) {
        g_checksum_free(stream->send.checksum);
    }

    if(stream->recv.checksum) {
        g_checksum_free(stream->recv.checksum);
    }

    if(stream->destructData1 && stream->data1) {
        stream->destructData1(stream->data1);
    }

    if(stream->destructData2 && stream->data2) {
        stream->destructData2(stream->data2);
    }

    if(stream->transport) {
        tgentransport_unref(stream->transport);
    }

    if(stream->mmodel) {
        tgenmarkovmodel_unref(stream->mmodel);
    }

    stream->magic = 0;
    g_free(stream);
}

void tgenstream_ref(TGenStream* stream) {
    TGEN_ASSERT(stream);
    stream->refcount++;
}

void tgenstream_unref(TGenStream* stream) {
    TGEN_ASSERT(stream);
    stream->refcount--;
    if(stream->refcount <= 0) {
        _tgenstream_free(stream);
    }
}
