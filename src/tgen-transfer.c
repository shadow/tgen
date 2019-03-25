/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "tgen.h"

/* disable default timeout */
#define DEFAULT_XFER_TIMEOUT_NSEC (0*((guint64)1000*1000*1000))
/* 60 second default stallout */
#define DEFAULT_XFER_STALLOUT_NSEC (30*((guint64)1000*1000*1000))

/* default lengths for buffers used during i/o.
 * the read buffer is temporary and stack-allocated.
 * the write buffer is persistent and heap-allocated, and thus
 * it will consume more memory so we keep it relatively smaller. */
#define DEFAULT_XFER_READ_BUFLEN 65536
#define DEFAULT_XFER_WRITE_BUFLEN 32768

/* an auth password so we know both sides understand tgen */
#define TGEN_AUTH_PW "T8nNx9L95LATtckJkR5n"
#define TGEN_PROTO_VERS_MAJ 1
#define TGEN_PROTO_VERS_MIN 0

/* the various states the read side of the connection can take */
typedef enum _TGenTransferRecvState {
    TGEN_XFER_RECV_NONE,
    TGEN_XFER_RECV_AUTHENTICATE,
    TGEN_XFER_RECV_HEADER,
    TGEN_XFER_RECV_MODEL,
    TGEN_XFER_RECV_PAYLOAD,
    TGEN_XFER_RECV_CHECKSUM,
    TGEN_XFER_RECV_SUCCESS,
    TGEN_XFER_RECV_ERROR,
} TGenTransferRecvState;

/* the various states the write side of the connection can take */
typedef enum _TGenTransferSendState {
    TGEN_XFER_SEND_NONE,
    TGEN_XFER_SEND_COMMAND,
    TGEN_XFER_SEND_RESPONSE,
    TGEN_XFER_SEND_PAYLOAD,
    TGEN_XFER_SEND_CHECKSUM,
    TGEN_XFER_SEND_FLUSH,
    TGEN_XFER_SEND_SUCCESS,
    TGEN_XFER_SEND_ERROR,
} TGenTransferSendState;

/* the various error states the connection can take */
typedef enum _TGenTransferErrorType {
    TGEN_XFER_ERR_NONE,
    TGEN_XFER_ERR_AUTHENTICATE,
    TGEN_XFER_ERR_HEADER,
    TGEN_XFER_ERR_MODEL,
    TGEN_XFER_ERR_CHECKSUM,
    TGEN_XFER_ERR_READ,
    TGEN_XFER_ERR_WRITE,
    TGEN_XFER_ERR_READEOF,
    TGEN_XFER_ERR_WRITEEOF,
    TGEN_XFER_ERR_TIMEOUT,
    TGEN_XFER_ERR_STALLOUT,
    TGEN_XFER_ERR_PROXY,
    TGEN_XFER_ERR_MISC,
} TGenTransferErrorType;

typedef enum _TGenTransferHeaderFlags {
    TGEN_HEADER_FLAG_NONE = 0,
    TGEN_HEADER_FLAG_PROTOCOL = 1 << 0,
    TGEN_HEADER_FLAG_HOSTNAME = 1 << 1,
    TGEN_HEADER_FLAG_ID = 1 << 2,
    TGEN_HEADER_FLAG_COUNT = 1 << 3,
    TGEN_HEADER_FLAG_SENDSIZE = 1 << 4,
    TGEN_HEADER_FLAG_RECVSIZE = 1 << 5,
    TGEN_HEADER_FLAG_MODELNAME = 1 << 6,
    TGEN_HEADER_FLAG_MODELSEED = 1 << 7,
    TGEN_HEADER_FLAG_MODELSIZE = 1 << 8,
} TGenTransferHeaderFlags;

struct _TGenTransfer {
    /* info to help describe this transfer object */
    gchar* id; /* the unique vertex id from the graph */
    gsize count; /* global transfer count */
    gchar* hostname; /* our hostname */
    GString* stringBuffer; /* a human-readable string for logging */

    /* describes the type of error if we are in an error state */
    TGenTransferErrorType error;

    /* true if we initiated the transfer (i.e., the client) */
    gboolean isCommander;

    /* the configured timeout values */
    gint64 timeoutUSecs;
    gint64 stalloutUSecs;

    /* socket communication layer and buffers */
    TGenTransport* transport;

    /* describes how this transfer generates packets */
    TGenMarkovModel* mmodel;

    /* information about the reading side of the connection */
    struct {
        /* current read state */
        TGenTransferRecvState state;

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
        TGenTransferSendState state;

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
        gsize count;
        gchar* hostname;
        GString* buffer;
        gchar* modelName;
        gsize modelSize;
        guint32 modelSeed;
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

    /* notification and parameters for when this transfer finishes */
    TGenTransfer_notifyCompleteFunc notify;
    gpointer data1;
    gpointer data2;
    GDestroyNotify destructData1;
    GDestroyNotify destructData2;

    /* memory housekeeping */
    gint refcount;
    guint magic;
};

static const gchar* _tgentransfer_recvStateToString(TGenTransferRecvState state) {
    switch(state) {
        /* valid states throughout the life of the recv side of the conn */
        case TGEN_XFER_RECV_NONE: {
            return "RECV_NONE";
        }
        case TGEN_XFER_RECV_AUTHENTICATE: {
            return "RECV_AUTHENTICATE";
        }
        case TGEN_XFER_RECV_HEADER: {
            return "RECV_HEADER";
        }
        case TGEN_XFER_RECV_MODEL: {
            return "RECV_MODEL";
        }
        case TGEN_XFER_RECV_PAYLOAD: {
            return "RECV_PAYLOAD";
        }
        case TGEN_XFER_RECV_CHECKSUM: {
            return "RECV_CHECKSUM";
        }
        /* success and error are terminal states */
        case TGEN_XFER_RECV_SUCCESS: {
            return "RECV_SUCCESS";
        }
        case TGEN_XFER_RECV_ERROR:
        default: {
            return "RECV_ERROR";
        }
    }
}

static const gchar* _tgentransfer_sendStateToString(TGenTransferSendState state) {
    switch(state) {
        /* valid states throughout the life of the send side of the conn */
        case TGEN_XFER_SEND_NONE: {
            return "SEND_NONE";
        }
        case TGEN_XFER_SEND_COMMAND: {
            return "SEND_COMMAND";
        }
        case TGEN_XFER_SEND_RESPONSE: {
            return "SEND_RESPONSE";
        }
        case TGEN_XFER_SEND_PAYLOAD: {
            return "SEND_PAYLOAD";
        }
        case TGEN_XFER_SEND_CHECKSUM: {
            return "SEND_CHECKSUM";
        }
        case TGEN_XFER_SEND_FLUSH: {
            return "SEND_FLUSH";
        }
        /* success and error are terminal states */
        case TGEN_XFER_SEND_SUCCESS: {
            return "SEND_SUCCESS";
        }
        case TGEN_XFER_SEND_ERROR:
        default: {
            return "SEND_ERROR";
        }
    }
}

static const gchar* _tgentransfer_errorToString(TGenTransferErrorType error) {
    switch(error) {
        case TGEN_XFER_ERR_NONE: {
            return "NONE";
        }
        case TGEN_XFER_ERR_AUTHENTICATE: {
            return "AUTH";
        }
        case TGEN_XFER_ERR_HEADER: {
            return "HEADER";
        }
        case TGEN_XFER_ERR_MODEL: {
            return "MODEL";
        }
        case TGEN_XFER_ERR_CHECKSUM: {
            return "CHECKSUM";
        }
        case TGEN_XFER_ERR_READ: {
            return "READ";
        }
        case TGEN_XFER_ERR_WRITE: {
            return "WRITE";
        }
        case TGEN_XFER_ERR_READEOF: {
            return "READEOF";
        }
        case TGEN_XFER_ERR_WRITEEOF: {
            return "WRITEEOF";
        }
        case TGEN_XFER_ERR_TIMEOUT: {
            return "TIMEOUT";
        }
        case TGEN_XFER_ERR_STALLOUT: {
            return "STALLOUT";
        }
        case TGEN_XFER_ERR_PROXY: {
            return "PROXY";
        }
        case TGEN_XFER_ERR_MISC:
        default: {
            return "MISC";
        }
    }
}

static const gchar* _tgentransfer_toString(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(transfer->stringBuffer) {
        return transfer->stringBuffer->str;
    }

    transfer->stringBuffer = g_string_new(NULL);

    g_string_printf(transfer->stringBuffer, "id=%s", transfer->id);

    g_string_append_printf(transfer->stringBuffer,
            ",count=%"G_GSIZE_FORMAT, transfer->count);
    g_string_append_printf(transfer->stringBuffer,
            ",peercount=%"G_GSIZE_FORMAT, transfer->peer.count);

    g_string_append_printf(transfer->stringBuffer,
            ",name=%s", transfer->hostname);
    g_string_append_printf(transfer->stringBuffer,
            ",peername=%s", transfer->peer.hostname);

    g_string_append_printf(transfer->stringBuffer,
            ",sendsize=%"G_GSIZE_FORMAT, transfer->send.requestedBytes);
    g_string_append_printf(transfer->stringBuffer,
            ",recvsize=%"G_GSIZE_FORMAT, transfer->recv.requestedBytes);

    g_string_append_printf(transfer->stringBuffer,
            ",sendstate=%s", _tgentransfer_sendStateToString(transfer->send.state));
    g_string_append_printf(transfer->stringBuffer,
            ",recvstate=%s", _tgentransfer_recvStateToString(transfer->recv.state));

    g_string_append_printf(transfer->stringBuffer,
            ",error=%s", _tgentransfer_errorToString(transfer->error));

    return transfer->stringBuffer->str;
}

static void _tgentransfer_resetString(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    if(transfer->stringBuffer) {
        g_string_free(transfer->stringBuffer, TRUE);
        transfer->stringBuffer = NULL;
    }
}

static void _tgentransfer_changeRecvState(TGenTransfer* transfer, TGenTransferRecvState state) {
    TGEN_ASSERT(transfer);
    tgen_info("transfer %s moving from recv state %s to recv state %s",
            _tgentransfer_toString(transfer),
            _tgentransfer_recvStateToString(transfer->recv.state),
            _tgentransfer_recvStateToString(state));
    transfer->recv.state = state;
    _tgentransfer_resetString(transfer);
}

static void _tgentransfer_changeSendState(TGenTransfer* transfer, TGenTransferSendState state) {
    TGEN_ASSERT(transfer);
    tgen_info("transfer %s moving from send state %s to send state %s",
            _tgentransfer_toString(transfer),
            _tgentransfer_sendStateToString(transfer->send.state),
            _tgentransfer_sendStateToString(state));
    transfer->send.state = state;
    _tgentransfer_resetString(transfer);
}

static void _tgentransfer_changeError(TGenTransfer* transfer, TGenTransferErrorType error) {
    TGEN_ASSERT(transfer);
    tgen_info("transfer %s moving from error %s to error %s",
            _tgentransfer_toString(transfer),
            _tgentransfer_errorToString(transfer->error),
            _tgentransfer_errorToString(error));
    transfer->error = error;
    _tgentransfer_resetString(transfer);
}

static gssize _tgentransfer_readBuffered(TGenTransfer* transfer, guchar* buffer, gsize limit) {
    TGEN_ASSERT(transfer);
    g_assert(transfer->recv.buffer);
    g_assert(transfer->recv.buffer->len > 0);

    tgen_debug("Trying to read %"G_GSIZE_FORMAT" bytes, we already have %"G_GSIZE_FORMAT
            " in the read buffer", limit, transfer->recv.buffer->len);

    /* we need to drain the recv buffer first */
    if(transfer->recv.buffer->len <= limit) {
        /* take all of the recv buffer */
        gsize amount = transfer->recv.buffer->len;
        g_memmove(buffer, transfer->recv.buffer->str, amount);

        /* don't need the recv buffer any more */
        g_string_free(transfer->recv.buffer, TRUE);
        transfer->recv.buffer = NULL;

        /* their buffer might be larger than what they need, so return what we have */
        return (gssize) amount;
    } else {
        /* we already have more buffered than the caller wants */
        g_memmove(buffer, transfer->recv.buffer->str, limit);

        /* we want to keep the remaining bytes that we did not return */
        GString* newBuffer = g_string_new(&transfer->recv.buffer->str[limit]);

        /* replace the read buffer */
        g_string_free(transfer->recv.buffer, TRUE);
        transfer->recv.buffer = newBuffer;

        /* we read everything they wanted, don't try to read more */
        return (gssize) limit;
    }
}

static gssize _tgentransfer_read(TGenTransfer* transfer, guchar* buffer, gsize limit) {
    TGEN_ASSERT(transfer);
    g_assert(buffer);
    g_assert(limit > 0);

    /* if there is anything left over in the recv buffer, use that first */
    if(transfer->recv.buffer) {
        return _tgentransfer_readBuffered(transfer, buffer, limit);
    }

    /* by now the recv buffer should be empty */
    g_assert(transfer->recv.buffer == NULL);

    /* get more bytes from the transport */
    gssize bytes = tgentransport_read(transfer->transport, &(buffer[0]), limit);

    /* check for errors and EOF */
    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_READ);

        tgen_critical("read(): transport %s transfer %s error %i: %s",
                tgentransport_toString(transfer->transport),
                _tgentransfer_toString(transfer),
                errno, g_strerror(errno));
    } else if(bytes == 0) {
        /* reading an EOF is only an error if we were expecting a certain recvsize */
        if(transfer->recv.state != TGEN_XFER_RECV_PAYLOAD ||
                (transfer->recv.requestedBytes > 0 &&
                        transfer->recv.payloadBytes < transfer->recv.requestedBytes)) {
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_ERROR);
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_READEOF);

            tgen_critical("read(): transport %s transfer %s closed unexpectedly",
                    tgentransport_toString(transfer->transport),
                    _tgentransfer_toString(transfer));
        }
    } else {
        transfer->recv.totalBytes += bytes;
    }

    return bytes;
}

static GString* _tgentransfer_getLine(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* our read buffer */
    guchar buffer[DEFAULT_XFER_READ_BUFLEN];

    /* get some data */
    gssize bytes = _tgentransfer_read(transfer, buffer, DEFAULT_XFER_READ_BUFLEN);

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
        if(transfer->recv.buffer == NULL) {
            transfer->recv.buffer = g_string_new_len(&buffer[offset], remaining);
        } else {
            g_string_append_len(transfer->recv.buffer, &buffer[offset], remaining);
        }
    }

    /* return the line, which may be NULL if we didn't find it yet */
    return line;
}

static gboolean _tgentransfer_readAuthenticate(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    guchar authbuf[24];
    gsize amt = 21 - transfer->recv.authIndex;
    gssize bytes = _tgentransfer_read(transfer, &(authbuf[0]), amt);

    if(bytes <= 0 || transfer->recv.state != TGEN_XFER_RECV_AUTHENTICATE) {
        /* we didn't get anything or some error when reading */
        return FALSE;
    }

    /* check the bytes we got */
    for (gssize loc = 0; loc < bytes; loc++) {
        gchar c = authbuf[loc];

        if(transfer->recv.authIndex == 20) {
            /* we just read the space following the password, so we are now done */
            tgen_info("transfer authentication successful!");
            return TRUE;
        }

        g_assert(transfer->recv.authIndex < 20);

        if(c == TGEN_AUTH_PW[transfer->recv.authIndex]) {
            /* this character matched */
            transfer->recv.authIndex++;
        } else {
            /* password doesn't match */
            tgen_info("transfer authentication error: incorrect authentication token");
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_ERROR);
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_AUTHENTICATE);
            return FALSE;
        }
    }

    /* all the bytes that we got matched, but we didn't get everything yet */
    return FALSE;
}

static gboolean _tgentransfer_readHeader(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    GString* line = _tgentransfer_getLine(transfer);
    if(!line) {
        /* unable to receive an entire line, wait for the rest */
        return FALSE;
    }

    TGenTransferHeaderFlags parsedKeys = TGEN_HEADER_FLAG_NONE;

    /* we have read the entire command header from the other end */
    gboolean hasError = FALSE;

    tgen_debug("Parsing header string now: %s", line->str);

    /* lets parse the string */
    gchar** parts = g_strsplit(line->str, " ", 0);

    /* parse all of the key=value pairs */
    for(gint i = 0; !hasError && parts != NULL && parts[i] != NULL; i++) {
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
                        hasError = TRUE;
                    }
                }

                if(versions != NULL) {
                    g_strfreev(versions);
                }
            } else if(!g_ascii_strcasecmp(key, "HOSTNAME")) {
                transfer->peer.hostname = g_strdup(value);
                parsedKeys |= TGEN_HEADER_FLAG_HOSTNAME;
            } else if(!g_ascii_strcasecmp(key, "TRANSFER_ID")) {
                transfer->id = g_strdup(value);
                parsedKeys |= TGEN_HEADER_FLAG_ID;
            } else if(!g_ascii_strcasecmp(key, "TRANSFER_COUNT")) {
                transfer->peer.count = (gsize)g_ascii_strtoull(value, NULL, 10);
                parsedKeys |= TGEN_HEADER_FLAG_COUNT;
            } else if(!g_ascii_strcasecmp(key, "SEND_SIZE")) {
                /* the other side's send size is our recv size */
                if(value[0] == '~') {
                    transfer->recv.requestedBytes = 0;
                    transfer->recv.requestedZero = TRUE;
                    tgen_info("Peer requested 0 recv bytes on stream %s",
                            _tgentransfer_toString(transfer));
                } else {
                    transfer->recv.requestedBytes = (gsize)g_ascii_strtoull(value, NULL, 10);
                }
                parsedKeys |= TGEN_HEADER_FLAG_SENDSIZE;
            } else if(!g_ascii_strcasecmp(key, "RECV_SIZE")) {
                /* the other side's recv size is our send size */
                if(value[0] == '~') {
                    transfer->send.requestedBytes = 0;
                    transfer->send.requestedZero = TRUE;
                    tgen_info("Peer requested 0 send bytes on stream %s",
                            _tgentransfer_toString(transfer));
                } else {
                    transfer->send.requestedBytes = (gsize)g_ascii_strtoull(value, NULL, 10);
                }
                parsedKeys |= TGEN_HEADER_FLAG_RECVSIZE;
            } else if(!g_ascii_strcasecmp(key, "MODEL_NAME")) {
                transfer->peer.modelName = g_strdup(value);
                parsedKeys |= TGEN_HEADER_FLAG_MODELNAME;
            } else if(!g_ascii_strcasecmp(key, "MODEL_SEED")) {
                transfer->peer.modelSeed = (guint32)atol(value);
                parsedKeys |= TGEN_HEADER_FLAG_MODELSEED;
            } else if(!g_ascii_strcasecmp(key, "MODEL_SIZE")) {
                long long int modelSize = atoll(value);

                /* we allocate memory of this size, so check bounds for safety */
#define TEN_MIB 1024*1024*10
                if(modelSize > 0 && modelSize <= TEN_MIB) {
                    /* the model size is OK */
                    transfer->peer.modelSize = (gsize)modelSize;
                    parsedKeys |= TGEN_HEADER_FLAG_MODELSIZE;
                } else {
                    tgen_info("Client requested model size %lli is out of bounds", modelSize);
                    hasError = TRUE;
                }
            } else {
                tgen_info("Client sent unrecognized key '%s', ignoring", key);
            }

            if(!hasError) {
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

    if(transfer->isCommander) {
        TGenTransferHeaderFlags required = (TGEN_HEADER_FLAG_PROTOCOL |
                TGEN_HEADER_FLAG_HOSTNAME | TGEN_HEADER_FLAG_COUNT);
        if((parsedKeys & required) != required) {
            tgen_info("Finished parsing header flags, we did not receive all required flags.");
            hasError = TRUE;
        }
    } else {
        TGenTransferHeaderFlags required = (TGEN_HEADER_FLAG_PROTOCOL |
                TGEN_HEADER_FLAG_HOSTNAME | TGEN_HEADER_FLAG_ID |
                TGEN_HEADER_FLAG_COUNT | TGEN_HEADER_FLAG_SENDSIZE |
                TGEN_HEADER_FLAG_RECVSIZE | TGEN_HEADER_FLAG_MODELNAME |
                TGEN_HEADER_FLAG_MODELSEED | TGEN_HEADER_FLAG_MODELSIZE);
        if((parsedKeys & required) != required) {
            tgen_info("Finished parsing header flags, we did not receive all required flags.");
            hasError = TRUE;
        }
    }

    if(hasError) {
        _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_HEADER);
        return FALSE;
    } else {
        /* we need to update our string with the new command info */
        _tgentransfer_resetString(transfer);
        if(transfer->isCommander) {
            /* we are done receive the response */
            transfer->time.response = g_get_monotonic_time();
        }
        return TRUE;
    }
}

static gboolean _tgentransfer_readModel(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(transfer->peer.modelSize == 0) {
        _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_MODEL);
        return FALSE;
    }

    if(!transfer->peer.buffer) {
        transfer->peer.buffer = g_string_sized_new(transfer->peer.modelSize);
    }

    guchar buffer[DEFAULT_XFER_READ_BUFLEN];

    gsize remaining = transfer->peer.modelSize - transfer->peer.buffer->len;
    gsize requested = MIN(DEFAULT_XFER_READ_BUFLEN, remaining);
    g_assert(requested > 0);

    gssize bytes = _tgentransfer_read(transfer, &buffer[0], requested);

    if(bytes <= 0 || transfer->recv.state != TGEN_XFER_RECV_MODEL) {
        /* we didn't get anything or some error when reading */
        return FALSE;
    }

    g_string_append_len(transfer->peer.buffer, (gchar*)buffer, bytes);

    /* we should not have read more than the size of the model */
    g_assert(transfer->peer.buffer->len <= transfer->peer.modelSize);

    if(transfer->peer.buffer->len == transfer->peer.modelSize) {
        tgen_info("Parsing Markov model of size %"G_GSIZE_FORMAT, transfer->peer.buffer->len);

        /* done with the model, lets instantiate and parse it */
        transfer->mmodel = tgenmarkovmodel_newFromString(transfer->peer.modelName,
                transfer->peer.modelSeed, transfer->peer.buffer);

        /* clean up the read buffer */
        g_string_free(transfer->peer.buffer, TRUE);
        transfer->peer.buffer = NULL;

        if(transfer->mmodel) {
            tgen_info("We received a valid Markov model");
            if(!transfer->isCommander) {
                /* we are done receive the command */
                transfer->time.command = g_get_monotonic_time();
            }
            return TRUE;
        } else {
            /* some problem with the model */
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_ERROR);
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_MODEL);

            tgen_critical("We received model '%s', but could not instantiate it",
                    transfer->peer.modelName);
        }
    }

    return FALSE;
}

static gboolean _tgentransfer_readPayload(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(transfer->recv.requestedBytes == 0 && transfer->recv.requestedZero) {
        /* we should not have any payload, just move on */
        tgen_debug("Ignoring payload on stream requesting 0 bytes");
        return TRUE;
    }

    guchar buffer[DEFAULT_XFER_READ_BUFLEN];
    gsize limit = DEFAULT_XFER_READ_BUFLEN;

    /* if the requested bytes is non-zero, then we have a specific total amount to read */
    if(transfer->recv.requestedBytes > 0) {
        g_assert(transfer->recv.payloadBytes <= transfer->recv.requestedBytes);
        gsize remaining = transfer->recv.requestedBytes - transfer->recv.payloadBytes;
        limit = MIN(limit, remaining);
    }

    /* we only run through the read loop once in order to give other sockets a chance for i/o */
    gssize bytes = _tgentransfer_read(transfer, &buffer[0], limit);

    /* EOF is a valid end state for transfers where we don't know the payload size */
    if(bytes == 0 && transfer->recv.requestedBytes == 0) {
        /* its possible they didnt send us anything */
        if(transfer->recv.payloadBytes > 0) {
            transfer->time.lastPayloadByte = g_get_monotonic_time();
        }

        return TRUE;
    }

    if(bytes <= 0 || transfer->recv.state != TGEN_XFER_RECV_PAYLOAD) {
        /* we didn't get anything or some error when reading */
        return FALSE;
    }

    if(transfer->recv.payloadBytes == 0) {
        transfer->time.firstPayloadByte = g_get_monotonic_time();
    }

    transfer->recv.payloadBytes += bytes;

    /* only track the checksum if we know the final size.
     * TODO this needs to be updated if we support checksums on general streams. */
    if(transfer->recv.requestedBytes > 0) {
        g_checksum_update(transfer->recv.checksum, buffer, bytes);
    }

    /* valid end state for transfers where we know the payload size */
    if(transfer->recv.requestedBytes > 0 &&
            transfer->recv.payloadBytes >= transfer->recv.requestedBytes) {
        tgen_debug("Finished reading %"G_GSIZE_FORMAT
                " requested payload bytes", transfer->recv.payloadBytes);
        transfer->time.lastPayloadByte = g_get_monotonic_time();
        return TRUE;
    }

    /* still want more */
    return FALSE;
}

static gboolean _tgentransfer_readChecksum(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(transfer->recv.requestedBytes == 0) {
        /* we don't handle checksums if we don't know the total size, so just move on.
         * TODO this needs to be updated if we support checksums on general streams. */
        tgen_debug("Ignoring checksum on stream with no requested bytes");
        return TRUE;
    }

    GString* line = _tgentransfer_getLine(transfer);
    if(!line) {
        return FALSE;
    }

    /* we have read the entire checksum from the other end */
    transfer->time.checksum = g_get_monotonic_time();

    gchar** parts = g_strsplit(line->str, " ", 0);
    const gchar* receivedSum = parts[1];

    gchar* computedSum = g_strdup(g_checksum_get_string(transfer->recv.checksum));
    g_assert(computedSum);

    gssize sumLength = g_checksum_type_get_length(G_CHECKSUM_MD5);
    g_assert(sumLength >= 0);

    gboolean matched = (g_ascii_strncasecmp(computedSum, receivedSum, (gsize)sumLength) == 0);
    gboolean isSuccess = FALSE;

    /* check that the sums match */
    if(receivedSum && matched) {
        tgen_info("transport %s transfer %s MD5 checksums passed: computed=%s received=%s",
                tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer),
                computedSum, receivedSum);
        isSuccess = TRUE;
    } else {
        if(receivedSum) {
            tgen_message("MD5 checksums failed: computed=%s received=%s", computedSum, receivedSum);
        } else {
            tgen_message("MD5 checksums failed: received sum is NULL");
        }
        _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_CHECKSUM);
        isSuccess = FALSE;
    }

    g_free(computedSum);
    g_strfreev(parts);
    g_string_free(line, TRUE);

    return isSuccess;
}

static void _tgentransfer_onReadable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    tgen_debug("active transfer %s is readable", _tgentransfer_toString(transfer));
    gsize startBytes = transfer->recv.totalBytes;

    if(transfer->recv.state == TGEN_XFER_RECV_AUTHENTICATE) {
        if(_tgentransfer_readAuthenticate(transfer)) {
            /* want to receive the header next */
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_HEADER);
        }
    }

    if(transfer->recv.state == TGEN_XFER_RECV_HEADER) {
        if(_tgentransfer_readHeader(transfer)) {
            if(transfer->isCommander) {
                /* now we can move to the payload stage */
                _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_PAYLOAD);
            } else {
                /* need to receive the model next */
                _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_MODEL);
            }
        }
    }

    /* only the non-commander */
    if(transfer->recv.state == TGEN_XFER_RECV_MODEL) {
        g_assert(!transfer->isCommander);
        if(_tgentransfer_readModel(transfer)) {
            /* now we send the response */
            _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_RESPONSE);

            /* and we start receiving the payload */
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_PAYLOAD);
        }
    }

    if(transfer->recv.state == TGEN_XFER_RECV_PAYLOAD) {
        if(_tgentransfer_readPayload(transfer)) {
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_CHECKSUM);
        }
    }

    if(transfer->recv.state == TGEN_XFER_RECV_CHECKSUM) {
        if(_tgentransfer_readChecksum(transfer)) {
            /* yay, now we are done! */
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_SUCCESS);
        }
    }

    gsize endBytes = transfer->recv.totalBytes;
    gsize totalBytes = endBytes - startBytes;

    tgen_debug("active transfer %s read %"G_GSIZE_FORMAT" more bytes",
            _tgentransfer_toString(transfer), totalBytes);

    if(totalBytes > 0) {
        transfer->time.lastProgress = g_get_monotonic_time();
    }
}

static GString* _tgentransfer_getRandomString(gsize size) {
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

static gsize _tgentransfer_flushOut(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(!transfer->send.buffer) {
        return 0;
    }

    gchar* position = &(transfer->send.buffer->str[transfer->send.offset]);
    gsize limit = transfer->send.buffer->len - transfer->send.offset;
    gssize bytes = tgentransport_write(transfer->transport, position, limit);

    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_WRITE);

        tgen_critical("write(): transport %s transfer %s error %i: %s",
                tgentransport_toString(transfer->transport),
                _tgentransfer_toString(transfer),
                errno, g_strerror(errno));
    } else if(bytes == 0) {
        _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_WRITEEOF);

        tgen_critical("write(): transport %s transfer %s closed unexpectedly",
                tgentransport_toString(transfer->transport),
                _tgentransfer_toString(transfer));
    } else if(bytes > 0) {
        transfer->send.offset += bytes;

        /* if we wrote everything, free the buffer */
        if(transfer->send.offset >= transfer->send.buffer->len) {
            transfer->send.offset = 0;
            g_string_free(transfer->send.buffer, TRUE);
            transfer->send.buffer = NULL;
        }

        transfer->send.totalBytes += bytes;
        return (gsize) bytes;
    }

    return 0;
}

static gboolean _tgentransfer_writeCommand(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* buffer the command if we have not done that yet */
    if(!transfer->send.buffer) {
        transfer->send.buffer = g_string_new(NULL);

        /* we will send the model as a string */
        GString* modelGraphml = tgenmarkovmodel_toGraphmlString(transfer->mmodel);
        g_assert(modelGraphml);

        /* Send useful information about the transfer. All but the PW are tagged
         * to make it easier to extend in the future. */
        g_string_printf(transfer->send.buffer, "%s", TGEN_AUTH_PW);

        g_string_append_printf(transfer->send.buffer,
                " PROTOCOL_VERSION=%i.%i", TGEN_PROTO_VERS_MAJ, TGEN_PROTO_VERS_MIN);
        g_string_append_printf(transfer->send.buffer,
                " HOSTNAME=%s", transfer->hostname);
        g_string_append_printf(transfer->send.buffer,
                " TRANSFER_ID=%s", transfer->id);
        g_string_append_printf(transfer->send.buffer,
                " TRANSFER_COUNT=%"G_GSIZE_FORMAT, transfer->count);
        if(transfer->send.requestedZero) {
            g_string_append_printf(transfer->send.buffer,
                    " SEND_SIZE=~");
        } else {
            g_string_append_printf(transfer->send.buffer,
                    " SEND_SIZE=%"G_GSIZE_FORMAT, transfer->send.requestedBytes);
        }
        if(transfer->recv.requestedZero) {
            g_string_append_printf(transfer->send.buffer,
                    " RECV_SIZE=~");
        } else {
            g_string_append_printf(transfer->send.buffer,
                    " RECV_SIZE=%"G_GSIZE_FORMAT, transfer->recv.requestedBytes);
        }
        g_string_append_printf(transfer->send.buffer,
                " MODEL_NAME=%s", tgenmarkovmodel_getName(transfer->mmodel));
        g_string_append_printf(transfer->send.buffer,
                " MODEL_SEED=%"G_GUINT32_FORMAT, tgenmarkovmodel_getSeed(transfer->mmodel));
        g_string_append_printf(transfer->send.buffer,
                " MODEL_SIZE=%"G_GSIZE_FORMAT, modelGraphml->len);

        /* close off the tagged data with a newline */
        g_string_append_c(transfer->send.buffer, '\n');

        /* then we write the graphml string of the specified size */
        g_string_append_printf(transfer->send.buffer, "%s", modelGraphml->str);

        /* clean up */
        g_string_free(modelGraphml, TRUE);
    }

    _tgentransfer_flushOut(transfer);

    if(!transfer->send.buffer) {
        /* entire command was sent, move to payload phase */
        transfer->time.command = g_get_monotonic_time();
        return TRUE;
    } else {
        /* still need to write/flush more */
        return FALSE;
    }
}

static gboolean _tgentransfer_writeResponse(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* buffer the command if we have not done that yet */
    if(!transfer->send.buffer) {
        transfer->send.buffer = g_string_new(NULL);
        g_string_printf(transfer->send.buffer, "%s", TGEN_AUTH_PW);

        g_string_append_printf(transfer->send.buffer,
                " PROTOCOL_VERSION=%i.%i", TGEN_PROTO_VERS_MAJ, TGEN_PROTO_VERS_MIN);
        g_string_append_printf(transfer->send.buffer,
                " HOSTNAME=%s", transfer->hostname);
        g_string_append_printf(transfer->send.buffer,
                " TRANSFER_COUNT=%"G_GSIZE_FORMAT, transfer->count);

        /* close off the tagged data with a newline */
        g_string_append_c(transfer->send.buffer, '\n');
    }

    _tgentransfer_flushOut(transfer);

    if(!transfer->send.buffer) {
        /* entire response was sent */
        transfer->time.response = g_get_monotonic_time();
        return TRUE;
    } else {
        /* unable to send entire command, wait for next chance to write */
        return FALSE;
    }
}

static gboolean _tgentransfer_writePayload(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(!transfer->mmodel) {
        tgen_info("Trying to write payload but we have no Markov model");
        _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_MODEL);
        return FALSE;
    }

    /* try to flush any leftover bytes */
    transfer->send.payloadBytes += _tgentransfer_flushOut(transfer);

    if (transfer->send.buffer) {
        /* still need to write it next time */
        return FALSE;
    }

    /* we are done if we sent the total requested bytes or have no requested
     * bytes but reached the end of the model */
    if(transfer->send.requestedBytes > 0) {
        if(transfer->send.payloadBytes >= transfer->send.requestedBytes) {
            return TRUE;
        }
    } else {
        if(transfer->send.requestedZero) {
            /* we should not send anything */
            return TRUE;
        } else if(tgenmarkovmodel_isInEndState(transfer->mmodel)) {
            /* they didn't request 0, so we end when the model ends */
            return TRUE;
        }
    }

    /* We limit our write buffer size in order to give other sockets a chance for i/o.
     * This allows us to return to the epoll loop and service other active transfers.
     * If we don't do this, it's possible that this single transfer will block the others. */
    gsize limit = DEFAULT_XFER_WRITE_BUFLEN;
    if(transfer->send.requestedBytes > 0) {
        g_assert(transfer->send.requestedBytes >= transfer->send.payloadBytes);
        gsize remaining = transfer->send.requestedBytes - transfer->send.payloadBytes;
        limit = MIN(remaining, DEFAULT_XFER_WRITE_BUFLEN);
    }

    gsize cumulativeSize = 0;
    guint64 cumulativeDelay = 0;
    guint64 interPacketDelay = 0;

    while(cumulativeSize < limit) {
        guint64 obsDelay = 0;
        Observation obs = tgenmarkovmodel_getNextObservation(transfer->mmodel, &obsDelay);

        if((transfer->isCommander && obs == OBSERVATION_TO_ORIGIN)
                || (!transfer->isCommander && obs == OBSERVATION_TO_SERVER)) {
            /* the other end is sending us a packet, we have nothing to do.
             * but this delay should be included in the delay for our next outgoing packet. */
            interPacketDelay += obsDelay;
            cumulativeDelay += obsDelay;
        } else if((transfer->isCommander && obs == OBSERVATION_TO_SERVER)
                || (!transfer->isCommander && obs == OBSERVATION_TO_ORIGIN)) {
            /* this means we should send a packet */
            cumulativeSize += TGEN_MMODEL_PACKET_DATA_SIZE;
            /* since we sent a packet, now we reset the delay */
            interPacketDelay = obsDelay;
            cumulativeDelay += obsDelay;
        } else if(obs == OBSERVATION_END) {
            /* if we have a specific requested send size, we need to reset and keep sending.
             * we never reset when requestedBytes is 0 (it either means no bytes, or end
             * when the model ends) */
            if(transfer->send.requestedBytes > 0) {
                tgenmarkovmodel_reset(transfer->mmodel);
            } else {
                /* the model reached the end and we should stop sending */
                break;
            }
        } else {
            /* we should not be getting other observation types in a stream model */
            tgen_info("Got a non-packet model observation from the Markov model");
            _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_ERROR);
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_MODEL);
            return FALSE;
        }

        if(interPacketDelay > TGEN_MMODEL_MICROS_AT_ONCE) {
            /* pause before we continue sending more */
            transfer->send.deferBarrierMicros = g_get_monotonic_time() + (gint64)interPacketDelay;
            break;
        }
    }

    gsize newBufLen = MIN(limit, cumulativeSize);
    if(newBufLen > 0) {
        transfer->send.buffer = _tgentransfer_getRandomString(newBufLen);

        /* only send a checksum when we know the final size.
         * TODO this needs to be updated if we support checksums on general streams. */
        if(transfer->send.requestedBytes > 0) {
            g_checksum_update(transfer->send.checksum, (guchar*)transfer->send.buffer->str,
                    (gssize)transfer->send.buffer->len);
        }

        transfer->send.payloadBytes += _tgentransfer_flushOut(transfer);
    }

    /* return false so we stay in the payload state so we can flush and
     * double check the end conditions again. */
    return FALSE;
}

static gboolean _tgentransfer_writeChecksum(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* buffer the checksum if we have not done that yet */
    if(!transfer->send.buffer) {
        transfer->send.buffer = g_string_new(NULL);
        g_string_printf(transfer->send.buffer, "MD5 %s",
                g_checksum_get_string(transfer->send.checksum));
        g_string_append_c(transfer->send.buffer, '\n');
        tgen_debug("Sending checksum '%s'", transfer->send.buffer->str);
    }

    _tgentransfer_flushOut(transfer);

    if(!transfer->send.buffer) {
        /* we were able to send all of the checksum */
        transfer->time.checksum = g_get_monotonic_time();
        return TRUE;
    } else {
        /* unable to send entire checksum, wait for next chance to write */
        return FALSE;
    }
}

static void _tgentransfer_onWritable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    tgen_debug("active transfer %s is writable", _tgentransfer_toString(transfer));
    gsize startBytes = transfer->send.totalBytes;

    /* if we previously wanted to defer writes, reset the cache */
    if(transfer->send.deferBarrierMicros > 0) {
        g_assert(g_get_monotonic_time() >= transfer->send.deferBarrierMicros);
        transfer->send.deferBarrierMicros = 0;
    }

    if(transfer->send.state == TGEN_XFER_SEND_COMMAND) {
        /* only the commander sends the command */
        g_assert(transfer->isCommander);

        if(_tgentransfer_writeCommand(transfer)) {
            /* now we start waiting for the response */
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_AUTHENTICATE);

            /* and we start sending the payload */
            _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_PAYLOAD);
        }
    }

    if(transfer->send.state == TGEN_XFER_SEND_RESPONSE) {
        /* only the non-commander sends the response */
        g_assert(!transfer->isCommander);

        if(_tgentransfer_writeResponse(transfer)) {
            /* start sending the payload */
            _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_PAYLOAD);
        }
    }

    if(transfer->send.state == TGEN_XFER_SEND_PAYLOAD) {
        if(_tgentransfer_writePayload(transfer)) {
            _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_CHECKSUM);
        }
    }

    if(transfer->send.state == TGEN_XFER_SEND_CHECKSUM) {
        if(_tgentransfer_writeChecksum(transfer)) {
            /* now we just need to make sure we finished flushing */
            _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_FLUSH);
        }
    }

    if(transfer->send.state == TGEN_XFER_SEND_FLUSH) {
        /* make sure we flush the rest of the send buffer */
        _tgentransfer_flushOut(transfer);
        if(transfer->send.buffer == NULL) {
            /* yay, now we are done sending everything! */
            _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_SUCCESS);

            tgen_debug("Stream finished writing, shutting down transport writes now");
            tgentransport_shutdownWrites(transfer->transport);
        }
    }

    gsize endBytes = transfer->send.totalBytes;
    gsize totalBytes = endBytes - startBytes;
    tgen_debug("active transfer %s wrote %"G_GSIZE_FORMAT" more bytes",
                _tgentransfer_toString(transfer), totalBytes);

    if(totalBytes > 0) {
        transfer->time.lastProgress = g_get_monotonic_time();
    }
}

static gchar* _tgentransfer_getBytesStatusReport(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    GString* buffer = g_string_new(NULL);

    g_string_printf(buffer,
            "total-bytes-recv=%"G_GSIZE_FORMAT, transfer->recv.totalBytes);
    g_string_append_printf(buffer,
            " total-bytes-send=%"G_GSIZE_FORMAT, transfer->send.totalBytes);
    g_string_append_printf(buffer,
            " payload-bytes-recv=%"G_GSIZE_FORMAT, transfer->recv.payloadBytes);
    g_string_append_printf(buffer,
            " payload-bytes-send=%"G_GSIZE_FORMAT, transfer->send.payloadBytes);

    if(transfer->recv.requestedBytes > 0) {
        gdouble progress = (gdouble)transfer->recv.payloadBytes /
                (gdouble)transfer->recv.requestedBytes * 100.0f;
        g_string_append_printf(buffer, " payload-progress-recv=%.2f%%", progress);
    } else if(transfer->recv.state == TGEN_XFER_RECV_SUCCESS ||
            (transfer->recv.requestedBytes == 0 && transfer->recv.requestedZero)) {
        g_string_append_printf(buffer, " payload-progress-recv=%.2f%%", 100.0f);
    } else {
        g_string_append_printf(buffer, " payload-progress-recv=?");
    }

    if(transfer->send.requestedBytes > 0) {
        gdouble progress = (gdouble)transfer->send.payloadBytes /
                (gdouble)transfer->send.requestedBytes * 100.0f;
        g_string_append_printf(buffer, " payload-progress-send=%.2f%%", progress);
    } else if(transfer->send.state == TGEN_XFER_SEND_SUCCESS ||
            (transfer->send.requestedBytes == 0 && transfer->send.requestedZero)) {
        g_string_append_printf(buffer, " payload-progress-send=%.2f%%", 100.0f);
    } else {
        g_string_append_printf(buffer, " payload-progress-send=?");
    }

    return g_string_free(buffer, FALSE);
}

static gchar* _tgentransfer_getTimeStatusReport(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    gchar* proxyTimeStr = tgentransport_getTimeStatusReport(transfer->transport);

    gint64 command = (transfer->time.command > 0 && transfer->time.start > 0) ?
            (transfer->time.command - transfer->time.start) : -1;
    gint64 response = (transfer->time.response > 0 && transfer->time.start > 0) ?
            (transfer->time.response - transfer->time.start) : -1;
    gint64 firstPayloadByte = (transfer->time.firstPayloadByte > 0 && transfer->time.start > 0) ?
            (transfer->time.firstPayloadByte - transfer->time.start) : -1;
    gint64 lastPayloadByte = (transfer->time.lastPayloadByte > 0 && transfer->time.start > 0) ?
            (transfer->time.lastPayloadByte - transfer->time.start) : -1;
    gint64 checksum = (transfer->time.checksum > 0 && transfer->time.start > 0) ?
            (transfer->time.checksum - transfer->time.start) : -1;

    GString* buffer = g_string_new(NULL);

    /* print the times in milliseconds */
    g_string_printf(buffer,
            "%s usecs-to-command=%"G_GINT64_FORMAT" usecs-to-response=%"G_GINT64_FORMAT" "
            "usecs-to-first-byte=%"G_GINT64_FORMAT" usecs-to-last-byte=%"G_GINT64_FORMAT" "
            "usecs-to-checksum=%"G_GINT64_FORMAT, proxyTimeStr,
            command, response, firstPayloadByte, lastPayloadByte, checksum);

    g_free(proxyTimeStr);
    return g_string_free(buffer, FALSE);
}

static void _tgentransfer_log(TGenTransfer* transfer, gboolean wasActive) {
    TGEN_ASSERT(transfer);

    if(transfer->recv.state == TGEN_XFER_RECV_ERROR ||
            transfer->send.state == TGEN_XFER_SEND_ERROR) {
        /* we had an error at some point and will unlikely be able to complete.
         * only log an error once. */
        if(transfer->time.lastTimeErrorReport == 0) {
            gchar* bytesMessage = _tgentransfer_getBytesStatusReport(transfer);
            gchar* timeMessage = _tgentransfer_getTimeStatusReport(transfer);

            tgen_message("[transfer-error] transport %s transfer %s %s %s",
                    tgentransport_toString(transfer->transport),
                    _tgentransfer_toString(transfer), bytesMessage, timeMessage);

            gint64 now = g_get_monotonic_time();
            transfer->time.lastBytesStatusReport = now;
            transfer->time.lastTimeErrorReport = now;
            g_free(bytesMessage);
        }
    } else if(transfer->recv.state == TGEN_XFER_RECV_SUCCESS &&
            transfer->send.state == TGEN_XFER_SEND_SUCCESS) {
        /* we completed the transfer. yay. only log once. */
        if(transfer->time.lastTimeStatusReport == 0) {
            gchar* bytesMessage = _tgentransfer_getBytesStatusReport(transfer);
            gchar* timeMessage = _tgentransfer_getTimeStatusReport(transfer);

            tgen_message("[transfer-complete] transport %s transfer %s %s %s",
                    tgentransport_toString(transfer->transport),
                    _tgentransfer_toString(transfer), bytesMessage, timeMessage);

            gint64 now = g_get_monotonic_time();
            transfer->time.lastBytesStatusReport = now;
            transfer->time.lastTimeStatusReport = now;
            g_free(bytesMessage);
            g_free(timeMessage);
        }
    } else {
        /* the transfer is still working. only log on new activity */
        if(wasActive) {
            gchar* bytesMessage = _tgentransfer_getBytesStatusReport(transfer);

            /* TODO should this be logged at debug? */
            tgen_info("[transfer-status] transport %s transfer %s %s",
                    tgentransport_toString(transfer->transport),
                    _tgentransfer_toString(transfer), bytesMessage);

            transfer->time.lastBytesStatusReport = g_get_monotonic_time();;
            g_free(bytesMessage);
        }
    }
}

static TGenIOResponse _tgentransfer_runTransportEventLoop(TGenTransfer* transfer, TGenEvent events) {
    TGenIOResponse response;
    memset(&response, 0, sizeof(TGenIOResponse));

    TGenEvent retEvents = tgentransport_onEvent(transfer->transport, events);
    if(retEvents == TGEN_EVENT_NONE) {
        /* proxy failed */
        tgen_critical("proxy connection failed, transfer cannot begin");
        _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_PROXY);
        _tgentransfer_log(transfer, FALSE);

        /* return DONE to the io module so it does deregistration */
        response.events = TGEN_EVENT_DONE;
    } else {
        transfer->time.lastProgress = g_get_monotonic_time();
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

static TGenEvent _tgentransfer_computeWantedEvents(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* the events we need in order to make progress */
    TGenEvent wantedEvents = TGEN_EVENT_NONE;

    /* each part of the conn is done if we have reached success or error */
    gboolean recvDone = (transfer->recv.state == TGEN_XFER_RECV_SUCCESS ||
            transfer->recv.state == TGEN_XFER_RECV_ERROR) ? TRUE : FALSE;
    /* for sending, we also need to make sure we flush the outbuf after success */
    gboolean sendDone = (transfer->send.state == TGEN_XFER_SEND_SUCCESS ||
            transfer->send.state == TGEN_XFER_SEND_ERROR) ? TRUE : FALSE;

    /* the transfer is done if both sending and receiving states are done,
     * and we have flushed our entire send buffer */
    if(recvDone && sendDone) {
        wantedEvents |= TGEN_EVENT_DONE;
    } else {
        if(!recvDone && transfer->recv.state != TGEN_XFER_RECV_NONE) {
            wantedEvents |= TGEN_EVENT_READ;
        }

        if(!sendDone && transfer->send.state != TGEN_XFER_SEND_NONE) {
            /* check if we should defer writes */
            if(transfer->send.deferBarrierMicros > 0) {
                wantedEvents |= TGEN_EVENT_WRITE_DEFERRED;
            } else {
                wantedEvents |= TGEN_EVENT_WRITE;
            }
        }
    }

    return wantedEvents;
}

static TGenIOResponse _tgentransfer_runTransferEventLoop(TGenTransfer* transfer, TGenEvent events) {
    TGEN_ASSERT(transfer);

    gsize recvBefore = transfer->recv.payloadBytes;
    gsize sendBefore = transfer->send.payloadBytes;

    /* process the events */
    if(events & TGEN_EVENT_READ) {
        _tgentransfer_onReadable(transfer);
    }
    if(events & TGEN_EVENT_WRITE) {
        /* process the events */
        _tgentransfer_onWritable(transfer);
    }

    /* check if we want to log any progress information */
    gboolean recvActive = (transfer->recv.payloadBytes > recvBefore) ? TRUE : FALSE;
    gboolean sendActive = (transfer->send.payloadBytes > sendBefore) ? TRUE : FALSE;
    gboolean wasActive = (recvActive || sendActive) ? TRUE : FALSE;
    _tgentransfer_log(transfer, wasActive);

    /* figure out which events we need to advance */
    TGenIOResponse response;
    memset(&response, 0, sizeof(TGenIOResponse));
    response.events = _tgentransfer_computeWantedEvents(transfer);

    /* if the transfer is done, notify the driver */
    if(response.events & TGEN_EVENT_DONE) {
        if(transfer->notify) {
            /* execute the callback to notify that we are complete */
            gboolean wasSuccess = transfer->error == TGEN_XFER_ERR_NONE ? TRUE : FALSE;
            transfer->notify(transfer->data1, transfer->data2, wasSuccess);
            /* make sure we only do the notification once */
            transfer->notify = NULL;
        }
    } else if(response.events & TGEN_EVENT_WRITE_DEFERRED) {
        g_assert(transfer->send.deferBarrierMicros > 0);
        response.deferUntilUSec = transfer->send.deferBarrierMicros;
    }

    return response;
}

TGenIOResponse tgentransfer_onEvent(TGenTransfer* transfer, gint descriptor, TGenEvent events) {
    TGEN_ASSERT(transfer);

    if(transfer->transport && tgentransport_wantsEvents(transfer->transport)) {
        /* transport layer wants to do some IO, redirect as needed */
        return _tgentransfer_runTransportEventLoop(transfer, events);
    } else {
        /* transport layer is happy, our turn to start the transfer */
        return _tgentransfer_runTransferEventLoop(transfer, events);
    }
}

gboolean tgentransfer_onCheckTimeout(TGenTransfer* transfer, gint descriptor) {
    TGEN_ASSERT(transfer);

    /* the io module is checking to see if we are in a timeout state. if we are, then
     * the transfer will be cancelled, de-registered and destroyed. */
    gint64 now = g_get_monotonic_time();

    gint64 stalloutCutoff = transfer->time.lastProgress + transfer->stalloutUSecs;
    gint64 timeoutCutoff = transfer->time.start + transfer->timeoutUSecs;

    gboolean madeSomeProgress = (transfer->time.lastProgress > 0) ? TRUE : FALSE;
    gboolean transferStalled = (madeSomeProgress && (now >= stalloutCutoff)) ? TRUE : FALSE;
    gboolean transferTookTooLong = (now >= timeoutCutoff) ? TRUE : FALSE;

    if((transfer->stalloutUSecs > 0 && transferStalled) ||
            (transfer->timeoutUSecs > 0 && transferTookTooLong)) {
        /* if the recv side is in a non-terminal state, change it to error */
        if(transfer->recv.state != TGEN_XFER_RECV_SUCCESS &&
                transfer->recv.state != TGEN_XFER_RECV_ERROR) {
            _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_ERROR);
        }

        /* if the send side is in a non-terminal state, change it to error */
        if(transfer->send.state != TGEN_XFER_SEND_SUCCESS &&
                transfer->send.state != TGEN_XFER_SEND_ERROR) {
            _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_ERROR);
        }

        /* it's either a stallout or timeout error */
        if(transferStalled) {
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_STALLOUT);
        } else {
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_TIMEOUT);
        }

        /* log the error and notify driver before the transfer is destroyed */
        _tgentransfer_runTransferEventLoop(transfer, TGEN_EVENT_NONE);

        /* this transfer will be destroyed by the io module */
        return TRUE;
    } else {
        /* this transfer is still in progress */
        return FALSE;
    }
}

TGenTransfer* tgentransfer_new(const gchar* idStr, TGenStreamOptions* options,
        TGenMarkovModel* mmodel, TGenTransport* transport,
        TGenTransfer_notifyCompleteFunc notify,
        gpointer data1, GDestroyNotify destructData1,
        gpointer data2, GDestroyNotify destructData2) {
    TGenTransfer* transfer = g_new0(TGenTransfer, 1);
    transfer->magic = TGEN_MAGIC;
    transfer->refcount = 1;

    transfer->time.start = g_get_monotonic_time();

    transfer->notify = notify;
    transfer->data1 = data1;
    transfer->destructData1 = destructData1;
    transfer->data2 = data2;
    transfer->destructData2 = destructData2;

    /* get the hostname */
    gchar nameBuffer[256];
    memset(nameBuffer, 0, 256);
    transfer->hostname = (0 == tgenconfig_gethostname(nameBuffer, 255)) ? g_strdup(nameBuffer) : NULL;

    /* save the transfer context values */
    if(idStr) {
        transfer->id = g_strdup(idStr);
    }

    /* the timeout after which we abandon this transfer */
    guint64 timeoutNanos = (options && options->timeoutNanos.isSet) ?
            options->timeoutNanos.value : DEFAULT_XFER_TIMEOUT_NSEC;
    transfer->timeoutUSecs = (gint64)(timeoutNanos / 1000);

    /* the stallout after which we abandon this transfer */
    guint64 stalloutNanos = (options && options->stalloutNanos.isSet) ?
            options->stalloutNanos.value : DEFAULT_XFER_STALLOUT_NSEC;
    transfer->stalloutUSecs = (gint64)(stalloutNanos / 1000);

    if(options && options->sendSize.isSet) {
        transfer->send.requestedBytes = options->sendSize.value;
        if(options->sendSize.value == 0) {
            /* they explicitly requested 0, so they want 0 send bytes */
            transfer->send.requestedZero = TRUE;
        }
    } else {
        /* they didn't set a size, so we end when the model ends */
        transfer->send.requestedBytes = 0;
        transfer->send.requestedZero = FALSE;
    }

    /* if they explicitly requested 0, they mean they want 0 recv bytes */
    if(options && options->recvSize.isSet) {
        transfer->recv.requestedBytes = options->recvSize.value;
        if(options->recvSize.value == 0) {
            /* they explicitly requested 0, so they want 0 recv bytes */
            transfer->recv.requestedZero = TRUE;
        }
    } else {
        /* they didn't set a size, so we end when the model ends */
        transfer->recv.requestedBytes = 0;
        transfer->send.requestedZero = FALSE;
    }

    transfer->send.checksum = g_checksum_new(G_CHECKSUM_MD5);
    transfer->recv.checksum = g_checksum_new(G_CHECKSUM_MD5);

    if(transport) {
        tgentransport_ref(transport);
        transfer->transport = transport;
    }

    if(mmodel) {
        tgenmarkovmodel_ref(mmodel);
        transfer->mmodel = mmodel;

        /* the commander first sends the command */
        transfer->isCommander = TRUE;
        _tgentransfer_changeSendState(transfer, TGEN_XFER_SEND_COMMAND);
    } else {
        /* the non-commander waits for the command, the first part is authentication */
        transfer->isCommander = FALSE;
        _tgentransfer_changeRecvState(transfer, TGEN_XFER_RECV_AUTHENTICATE);
    }

    return transfer;
}

static void _tgentransfer_free(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(transfer->hostname) {
        g_free(transfer->hostname);
    }

    if(transfer->id) {
        g_free(transfer->id);
    }

    if(transfer->stringBuffer) {
        g_string_free(transfer->stringBuffer, TRUE);
    }

    if(transfer->peer.hostname) {
        g_free(transfer->peer.hostname);
    }

    if(transfer->peer.modelName) {
        g_free(transfer->peer.modelName);
    }

    if(transfer->peer.buffer) {
        g_string_free(transfer->peer.buffer, TRUE);
    }

    if(transfer->send.buffer) {
        g_string_free(transfer->send.buffer, TRUE);
    }

    if(transfer->recv.buffer) {
        g_string_free(transfer->recv.buffer, TRUE);
    }

    if(transfer->send.checksum) {
        g_checksum_free(transfer->send.checksum);
    }

    if(transfer->recv.checksum) {
        g_checksum_free(transfer->recv.checksum);
    }

    if(transfer->destructData1 && transfer->data1) {
        transfer->destructData1(transfer->data1);
    }

    if(transfer->destructData2 && transfer->data2) {
        transfer->destructData2(transfer->data2);
    }

    if(transfer->transport) {
        tgentransport_unref(transfer->transport);
    }

    if(transfer->mmodel) {
        tgenmarkovmodel_unref(transfer->mmodel);
    }

    transfer->magic = 0;
    g_free(transfer);
}

void tgentransfer_ref(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    transfer->refcount++;
}

void tgentransfer_unref(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    transfer->refcount--;
    if(transfer->refcount <= 0) {
        _tgentransfer_free(transfer);
    }
}
