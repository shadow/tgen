/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>
#include <math.h>
#include <igraph.h>

#include "tgen.h"

typedef enum {
    TGEN_A_NONE = 0,
    TGEN_EA_WEIGHT = 1 << 1,
    TGEN_VA_ID = 1 << 2,
    TGEN_VA_SERVERPORT = 1 << 3,
    TGEN_VA_TIME = 1 << 4,
    TGEN_VA_HEARTBEAT = 1 << 5,
    TGEN_VA_LOGLEVEL = 1 << 6,
    TGEN_VA_PACKETMODELPATH = 1 << 7,
    TGEN_VA_PACKETMODELSEED = 1 << 8,
    TGEN_VA_PEERS = 1 << 9,
    TGEN_VA_SOCKSPROXY = 1 << 10,
    TGEN_VA_SOCKSUSERNAME = 1 << 11,
    TGEN_VA_SOCKSPASSWORD = 1 << 12,
    TGEN_VA_SENDSIZE = 1 << 13,
    TGEN_VA_RECVSIZE = 1 << 14,
    TGEN_VA_TIMEOUT = 1 << 15,
    TGEN_VA_STALLOUT = 1 << 16,
    TGEN_VA_STREAMMODELPATH = 1 << 17,
    TGEN_VA_STREAMMODELSEED = 1 << 18,
    TGEN_VA_COUNT = 1 << 19,
} AttributeFlags;

typedef struct _TGenAction {
    TGenActionType type;
    gpointer options;
    /* used for pause action */
    glong totalIncomingEdges;
    glong completedIncomingEdges;
    guint magic;
} TGenAction;

struct _TGenGraph {
    igraph_t* graph;
    gchar* graphPath;

    /* known attributes that we found in the graph header */
    AttributeFlags knownAttributes;

    /* graph properties */
    igraph_integer_t clusterCount;
    igraph_integer_t vertexCount;
    igraph_integer_t edgeCount;
    igraph_bool_t isConnected;
    igraph_bool_t isDirected;

    GHashTable* actions;
    GHashTable* weights;

    gboolean hasStartAction;
    igraph_integer_t startActionVertexIndex;

    gboolean startHasPeers;
    gboolean transferMissingPeers;

    gint refcount;
    guint magic;
};

static TGenAction* _tgengraph_newAction(TGenActionType type, gpointer options) {
    TGenAction* action = g_new0(TGenAction, 1);
    action->type = type;
    action->options = options;
    action->magic = TGEN_MAGIC;
    return action;
}

/* frees memory allocated to store internal options like strings */
static void _tgengraph_freeActionHelper(TGenActionType type, gpointer optionsptr) {
    if(type == TGEN_ACTION_START) {
        TGenStartOptions* options = optionsptr;
        if(options) {
            _tgengraph_freeActionHelper(TGEN_ACTION_STREAM, &options->defaultStreamOpts);
        }
    } else if (type == TGEN_ACTION_PAUSE) {
        TGenPauseOptions* options = optionsptr;
        if(options && options->times.value) {
            tgenpool_unref(options->times.value);
        }
    } else if (type == TGEN_ACTION_END) {
        /* nothing internal to free */
    } else if (type == TGEN_ACTION_STREAM) {
        TGenStreamOptions* options = optionsptr;
        if(options) {
            if(options->packetModelPath.value) {
                g_free(options->packetModelPath.value);
            }
            if(options->peers.value) {
                tgenpool_unref(options->peers.value);
            }
            if(options->socksProxy.value) {
                tgenpeer_unref(options->socksProxy.value);
            }
            if(options->socksUsername.value) {
                g_free(options->socksUsername.value);
            }
            if(options->socksPassword.value) {
                g_free(options->socksPassword.value);
            }
        }
    } else if (type == TGEN_ACTION_FLOW) {
        TGenFlowOptions* options = optionsptr;
        if(options) {
            if(options->streamModelPath.value) {
                g_free(options->streamModelPath.value);
            }
            _tgengraph_freeActionHelper(TGEN_ACTION_STREAM, &options->streamOpts);
        }
    }
}

static void _tgengraph_freeAction(TGenAction* action) {
    TGEN_ASSERT(action);
    /* free all the internal options that we allocated */
    _tgengraph_freeActionHelper(action->type, action->options);
    action->magic = 0;
    g_free(action);
}

static const gchar* _tgengraph_attributeToString(AttributeFlags attr) {
    switch (attr) {
        case TGEN_EA_WEIGHT: {
            return "weight";
        }
        case TGEN_VA_ID: {
            return "id";
        }
        case TGEN_VA_SERVERPORT: {
            return "serverport";
        }
        case TGEN_VA_TIME: {
            return "time";
        }
        case TGEN_VA_HEARTBEAT: {
            return "heartbeat";
        }
        case TGEN_VA_LOGLEVEL: {
            return "loglevel";
        }
        case TGEN_VA_PACKETMODELPATH: {
            return "packetmodelpath";
        }
        case TGEN_VA_PACKETMODELSEED: {
            return "packetmodelseed";
        }
        case TGEN_VA_PEERS: {
            return "peers";
        }
        case TGEN_VA_SOCKSPROXY: {
            return "socksproxy";
        }
        case TGEN_VA_SOCKSUSERNAME: {
            return "socksusername";
        }
        case TGEN_VA_SOCKSPASSWORD: {
            return "sockspassword";
        }
        case TGEN_VA_SENDSIZE: {
            return "sendsize";
        }
        case TGEN_VA_RECVSIZE: {
            return "recvsize";
        }
        case TGEN_VA_TIMEOUT: {
            return "timeout";
        }
        case TGEN_VA_STALLOUT: {
            return "stallout";
        }
        case TGEN_VA_STREAMMODELPATH: {
            return "streammodelpath";
        }
        case TGEN_VA_STREAMMODELSEED: {
            return "streammodelseed";
        }
        case TGEN_VA_COUNT: {
            return "count";
        }
        default:
        case TGEN_A_NONE: {
            return "none";
        }
    }
}

static AttributeFlags _tgengraph_vertexAttributeToFlag(const gchar* stringAttribute) {
    if(stringAttribute) {
        if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_ID))) {
            return TGEN_VA_ID;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_SERVERPORT))) {
            return TGEN_VA_SERVERPORT;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_TIME))) {
            return TGEN_VA_TIME;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_HEARTBEAT))) {
            return TGEN_VA_HEARTBEAT;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_LOGLEVEL))) {
            return TGEN_VA_LOGLEVEL;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_PACKETMODELPATH))) {
            return TGEN_VA_PACKETMODELPATH;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_PACKETMODELSEED))) {
            return TGEN_VA_PACKETMODELSEED;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_PEERS))) {
            return TGEN_VA_PEERS;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_SOCKSPROXY))) {
            return TGEN_VA_SOCKSPROXY;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_SOCKSUSERNAME))) {
            return TGEN_VA_SOCKSUSERNAME;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_SOCKSPASSWORD))) {
            return TGEN_VA_SOCKSPASSWORD;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_SENDSIZE))) {
            return TGEN_VA_SENDSIZE;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_RECVSIZE))) {
            return TGEN_VA_RECVSIZE;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_TIMEOUT))) {
            return TGEN_VA_TIMEOUT;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_STALLOUT))) {
            return TGEN_VA_STALLOUT;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_STREAMMODELPATH))) {
            return TGEN_VA_STREAMMODELPATH;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_STREAMMODELSEED))) {
            return TGEN_VA_STREAMMODELSEED;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_COUNT))) {
            return TGEN_VA_COUNT;
        }
    }
    return TGEN_A_NONE;
}

static AttributeFlags _tgengraph_edgeAttributeToFlag(const gchar* stringAttribute) {
    if(stringAttribute) {
        if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_EA_WEIGHT))) {
            return TGEN_EA_WEIGHT;
        }
    }
    return TGEN_A_NONE;
}

static gchar* _tgengraph_getHomePath(const gchar* path) {
    g_assert(path);
    GString* sbuffer = g_string_new(path);
    if(g_ascii_strncasecmp(path, "~", 1) == 0) {
        /* replace ~ with home directory */
        const gchar* home = g_get_home_dir();
        g_string_erase(sbuffer, 0, 1);
        g_string_prepend(sbuffer, home);
    }
    return g_string_free(sbuffer, FALSE);
}

static gdouble* _tgengraph_getWeight(TGenGraph* g, igraph_integer_t edgeIndex) {
    TGEN_ASSERT(g);
    return g_hash_table_lookup(g->weights, GINT_TO_POINTER(edgeIndex));
}

static void _tgengraph_storeWeight(TGenGraph* g, gdouble weight, igraph_integer_t edgeIndex) {
    TGEN_ASSERT(g);

    gdouble* val = g_new0(gdouble, 1);
    *val = weight;
    g_hash_table_insert(g->weights, GINT_TO_POINTER(edgeIndex), val);
}

static GError* _tgengraph_parseGraphEdges(TGenGraph* g) {
    TGEN_ASSERT(g);

    tgen_debug("checking graph edges...");

    /* we will iterate through the edges */
    igraph_eit_t edgeIterator;

    gint result = igraph_eit_create(g->graph, igraph_ess_all(IGRAPH_EDGEORDER_ID), &edgeIterator);
    if(result != IGRAPH_SUCCESS) {
        return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                "igraph_eit_create return non-success code %i", result);
    }

    /* count the edges as we iterate */
    igraph_integer_t edgeCount = 0;
    GError* error = NULL;

    while (!IGRAPH_EIT_END(edgeIterator)) {
        igraph_integer_t edgeIndex = IGRAPH_EIT_GET(edgeIterator);

        igraph_integer_t fromVertexIndex, toVertexIndex;

        gint result = igraph_edge(g->graph, edgeIndex, &fromVertexIndex, &toVertexIndex);
        if(result != IGRAPH_SUCCESS) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                    "igraph_edge return non-success code %i", result);
            break;
        }

        const gchar* fromIDStr = (g->knownAttributes&TGEN_VA_ID) ?
                VAS(g->graph, _tgengraph_attributeToString(TGEN_VA_ID), fromVertexIndex) : NULL;
        if(!fromIDStr) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                    "found vertex %li with missing '%s' attribute",
                    _tgengraph_attributeToString(TGEN_VA_ID), (glong)fromVertexIndex);
            break;
        }

        const gchar* toIDStr = (g->knownAttributes&TGEN_VA_ID) ?
                VAS(g->graph, _tgengraph_attributeToString(TGEN_VA_ID), toVertexIndex) : NULL;
        if(!toIDStr) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                    "found vertex %li with missing '%s' attribute",
                    _tgengraph_attributeToString(TGEN_VA_ID), (glong)toVertexIndex);
            break;
        }

        tgen_debug("found edge %li from vertex %li (%s) to vertex %li (%s)",
                (glong)edgeIndex, (glong)fromVertexIndex, fromIDStr, (glong)toVertexIndex, toIDStr);

        const gchar* weightStr = (g->knownAttributes&TGEN_EA_WEIGHT) ?
                EAS(g->graph, _tgengraph_attributeToString(TGEN_EA_WEIGHT), edgeIndex) : NULL;
        if(weightStr != NULL) {
            if(g_ascii_strncasecmp(weightStr, "\0", (gsize) 1)) {
                gdouble weight = g_ascii_strtod(weightStr, NULL);
                _tgengraph_storeWeight(g, weight, edgeIndex);
            }
        }

        edgeCount++;
        IGRAPH_EIT_NEXT(edgeIterator);
    }

    igraph_eit_destroy(&edgeIterator);

    if(!error) {
        g->edgeCount = igraph_ecount(g->graph);
        if(g->edgeCount != edgeCount) {
            tgen_warning("igraph_vcount %f does not match iterator count %f", g->edgeCount, edgeCount);
        }

        tgen_info("%u graph edges ok", (guint) g->edgeCount);
    }

    return error;
}

static void _tgengraph_storeAction(TGenGraph* g, igraph_integer_t vertexIndex, TGenAction* a) {
    TGEN_ASSERT(g);
    g_hash_table_replace(g->actions, GINT_TO_POINTER(vertexIndex), a);
}

static TGenAction* _tgengraph_getAction(TGenGraph* g, igraph_integer_t vertexIndex) {
    TGEN_ASSERT(g);
    return g_hash_table_lookup(g->actions, GINT_TO_POINTER(vertexIndex));
}

static gboolean _tgengraph_hasSelfLoop(TGenGraph* g, igraph_integer_t vertexIndex) {
    TGEN_ASSERT(g);
    gboolean isLoop = FALSE;

    igraph_vector_t* resultNeighborVertices = g_new0(igraph_vector_t, 1);
    gint result = igraph_vector_init(resultNeighborVertices, 0);

    if(result == IGRAPH_SUCCESS) {
        result = igraph_neighbors(g->graph, resultNeighborVertices, vertexIndex, IGRAPH_OUT);
        if(result == IGRAPH_SUCCESS) {
            glong nVertices = igraph_vector_size(resultNeighborVertices);
            for (gint i = 0; i < nVertices; i++) {
                igraph_integer_t dstVertexIndex = igraph_vector_e(resultNeighborVertices, i);
                if(vertexIndex == dstVertexIndex) {
                    isLoop = TRUE;
                    break;
                }
            }
        }
    }

    igraph_vector_destroy(resultNeighborVertices);
    g_free(resultNeighborVertices);
    return isLoop;
}

static glong _tgengraph_countIncomingEdges(TGenGraph* g, igraph_integer_t vertexIndex) {
    /* Count up the total number of incoming edges */

    /* initialize a vector to hold the result neighbor vertices for this action */
    igraph_vector_t* resultNeighborVertices = g_new0(igraph_vector_t, 1);

    /* initialize with 0 entries, since we dont know how many neighbors we have */
    gint result = igraph_vector_init(resultNeighborVertices, 0);
    if(result != IGRAPH_SUCCESS) {
        tgen_critical("igraph_vector_init return non-success code %i", result);
        g_free(resultNeighborVertices);
        return -1;
    }

    /* now get all incoming 1-hop neighbors of the given action */
    result = igraph_neighbors(g->graph, resultNeighborVertices, vertexIndex, IGRAPH_IN);
    if(result != IGRAPH_SUCCESS) {
        tgen_critical("igraph_neighbors return non-success code %i", result);
        igraph_vector_destroy(resultNeighborVertices);
        g_free(resultNeighborVertices);
        return -1;
    }

    /* handle the results */
    glong totalIncoming = igraph_vector_size(resultNeighborVertices);
    tgen_debug("found %li incoming 1-hop neighbors to vertex %i", totalIncoming, (gint)vertexIndex);

    /* cleanup */
    igraph_vector_destroy(resultNeighborVertices);
    g_free(resultNeighborVertices);

    return totalIncoming;
}

static GError* _tgengraph_parseStreamAttributesHelper(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex, TGenStreamOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    GError* error;

    if(g->knownAttributes & TGEN_VA_PACKETMODELPATH) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_PACKETMODELPATH);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseString(name, valueStr, &options->packetModelPath);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_PACKETMODELSEED) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_PACKETMODELSEED);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseUInt32(name, valueStr, &options->packetModelSeed);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_PEERS) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_PEERS);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parsePeerList(name, valueStr, &options->peers);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_SOCKSPROXY) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_SOCKSPROXY);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parsePeer(name, valueStr, &options->socksProxy);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_SOCKSUSERNAME) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_SOCKSUSERNAME);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseString(name, valueStr, &options->socksUsername);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_SOCKSPASSWORD) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_SOCKSPASSWORD);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseString(name, valueStr, &options->socksPassword);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_SENDSIZE) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_SENDSIZE);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseBytes(name, valueStr, &options->sendSize);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_RECVSIZE) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_RECVSIZE);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseBytes(name, valueStr, &options->recvSize);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_TIMEOUT) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_TIMEOUT);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseTime(name, valueStr, &options->timeoutNanos);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_STALLOUT) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_STALLOUT);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseTime(name, valueStr, &options->stalloutNanos);
        if(error) {
            return error;
        }
    }

    return NULL;
}

static GError* _tgengraph_parseFlowAttributesHelper(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex, TGenFlowOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    GError* error = NULL;

    if(g->knownAttributes & TGEN_VA_STREAMMODELPATH) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_STREAMMODELPATH);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseString(name, valueStr, &options->streamModelPath);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_STREAMMODELSEED) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_STREAMMODELSEED);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseUInt32(name, valueStr, &options->streamModelSeed);
        if(error) {
            return error;
        }
    }

    error = _tgengraph_parseStreamAttributesHelper(g, idStr, vertexIndex, &options->streamOpts);
    if(error) {
        return error;
    }

    return NULL;
}

static GError* _tgengraph_parseStartAttributesHelper(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex, TGenStartOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    GError* error = NULL;

    if(g->knownAttributes & TGEN_VA_SERVERPORT) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_SERVERPORT);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseUInt16(name, valueStr, &options->serverport);
        if(error) {
            return error;
        } else {
            options->serverport.value = htons(options->serverport.value);
        }
    }

    if(g->knownAttributes & TGEN_VA_TIME) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_TIME);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseTime(name, valueStr, &options->timeNanos);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_HEARTBEAT) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_HEARTBEAT);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseTime(name, valueStr, &options->heartbeatPeriodNanos);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_LOGLEVEL) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_LOGLEVEL);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseLogLevel(name, valueStr, &options->loglevel);
        if(error) {
            return error;
        }
    }

    error = _tgengraph_parseStreamAttributesHelper(g, idStr, vertexIndex, &options->defaultStreamOpts);
    if(error) {
        return error;
    }

    return NULL;
}

static GError* _tgengraph_parsePauseAttributesHelper(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex, TGenPauseOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    GError* error = NULL;

    if(g->knownAttributes & TGEN_VA_TIME) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_TIME);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseTimeList(name, valueStr, &options->times);
        if(error) {
            return error;
        }
    }

    return NULL;
}

static GError* _tgengraph_parseEndAttributesHelper(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex, TGenEndOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    GError* error = NULL;

    if(g->knownAttributes & TGEN_VA_TIME) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_TIME);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseTime(name, valueStr, &options->timeNanos);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_COUNT) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_COUNT);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseUInt64(name, valueStr, &options->count);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_SENDSIZE) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_SENDSIZE);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseBytes(name, valueStr, &options->sendSize);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_RECVSIZE) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_RECVSIZE);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseBytes(name, valueStr, &options->recvSize);
        if(error) {
            return error;
        }
    }

    return NULL;
}

static GError* _tgengraph_parseStartVertex(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex) {
    TGEN_ASSERT(g);

    if(g->hasStartAction) {
        return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "only one start vertex is allowed in the action graph");
    }

    if(_tgengraph_hasSelfLoop(g, vertexIndex)) {
        return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "start vertex must not contain a self-loop");
    }

    GError* error = NULL;
    TGenStartOptions* options = g_new0(TGenStartOptions, 1);

    error = _tgengraph_parseStartAttributesHelper(g, idStr, vertexIndex, options);
    if(error) {
        g_free(options);
        return error;
    }

    g_assert(!g->hasStartAction);
    g->startActionVertexIndex = vertexIndex;
    g->hasStartAction = TRUE;

    if(options->defaultStreamOpts.peers) {
        g->startHasPeers = TRUE;
    }

    TGenAction* action = _tgengraph_newAction(TGEN_ACTION_START, options);
    _tgengraph_storeAction(g, vertexIndex, action);

    return NULL;
}

static GError* _tgengraph_parseEndVertex(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex) {
    TGEN_ASSERT(g);

    GError* error = NULL;
    TGenEndOptions* options = g_new0(TGenEndOptions, 1);

    error = _tgengraph_parseEndAttributesHelper(g, idStr, vertexIndex, options);
    if(error) {
        g_free(options);
        return error;
    }

    TGenAction* action = _tgengraph_newAction(TGEN_ACTION_END, options);
    _tgengraph_storeAction(g, vertexIndex, action);

    return NULL;
}

static GError* _tgengraph_parsePauseVertex(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex) {
    TGEN_ASSERT(g);

    GError* error = NULL;
    TGenPauseOptions* options = g_new0(TGenPauseOptions, 1);

    error = _tgengraph_parsePauseAttributesHelper(g, idStr, vertexIndex, options);
    if(error) {
        g_free(options);
        return error;
    }

    glong totalIncoming = _tgengraph_countIncomingEdges(g, vertexIndex);
    if(totalIncoming <= 0) {
        tgen_error("the number of incoming edges on vertex %i must be positive", (gint)vertexIndex);
        g_assert(totalIncoming > 0);
    }

    TGenAction* action = _tgengraph_newAction(TGEN_ACTION_PAUSE, options);
    action->totalIncomingEdges = totalIncoming;
    action->completedIncomingEdges = 0;
    _tgengraph_storeAction(g, vertexIndex, action);

    return NULL;
}

static GError* _tgengraph_parseStreamVertex(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex) {
    TGEN_ASSERT(g);

    GError* error = NULL;
    TGenStreamOptions* options = g_new0(TGenStreamOptions, 1);

    error = _tgengraph_parseStreamAttributesHelper(g, idStr, vertexIndex, options);
    if(error) {
        g_free(options);
        return error;
    }

    if(!options->peers.isSet) {
        g->transferMissingPeers = TRUE;
    }

    TGenAction* action = _tgengraph_newAction(TGEN_ACTION_STREAM, options);
    _tgengraph_storeAction(g, vertexIndex, action);

    return NULL;
}

static GError* _tgengraph_parseFlowVertex(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex) {
    TGEN_ASSERT(g);

    GError* error = NULL;
    TGenFlowOptions* options = g_new0(TGenFlowOptions, 1);

    error = _tgengraph_parseFlowAttributesHelper(g, idStr, vertexIndex, options);
    if(error) {
        g_free(options);
        return error;
    }

    TGenAction* action = _tgengraph_newAction(TGEN_ACTION_FLOW, options);
    _tgengraph_storeAction(g, vertexIndex, action);

    return NULL;
}

static GError* _tgengraph_parseGraphVertices(TGenGraph* g) {
    TGEN_ASSERT(g);

    tgen_debug("checking graph vertices...");

    /* we will iterate through the vertices */
    igraph_vit_t vertexIterator;

    gint result = igraph_vit_create(g->graph, igraph_vss_all(), &vertexIterator);
    if(result != IGRAPH_SUCCESS) {
        return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                "igraph_vit_create return non-success code %i", result);
    }

    /* count the vertices as we iterate */
    igraph_integer_t vertexCount = 0;
    GError* error = NULL;

    while (!IGRAPH_VIT_END(vertexIterator)) {
        igraph_integer_t vertexIndex = (igraph_integer_t)IGRAPH_VIT_GET(vertexIterator);

        /* get vertex attributes: S for string and N for numeric */
        const gchar* idStr = (g->knownAttributes&TGEN_VA_ID) ?
                VAS(g->graph, "id", vertexIndex) : NULL;

        if(!idStr) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                    "found vertex %li with missing action 'id' attribute", (glong)vertexIndex);
            break;
        }

        if(g_strstr_len(idStr, (gssize)-1, "start")) {
            error = _tgengraph_parseStartVertex(g, idStr, vertexIndex);
        } else if(g_strstr_len(idStr, (gssize)-1, "end")) {
            error = _tgengraph_parseEndVertex(g, idStr, vertexIndex);
        } else if(g_strstr_len(idStr, (gssize)-1, "pause")) {
            error = _tgengraph_parsePauseVertex(g, idStr, vertexIndex);
        } else if(g_strstr_len(idStr, (gssize)-1, "stream")) {
            error = _tgengraph_parseStreamVertex(g, idStr, vertexIndex);
        } else if(g_strstr_len(idStr, (gssize)-1, "flow")) {
            error = _tgengraph_parseFlowVertex(g, idStr, vertexIndex);
        } else {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                    "found vertex %li (%s) with an unknown action id '%s'",
                    (glong)vertexIndex, idStr, idStr);
        }

        if(error) {
            break;
        }

        vertexCount++;
        IGRAPH_VIT_NEXT(vertexIterator);
    }

    /* clean up */
    igraph_vit_destroy(&vertexIterator);

    if(!g->startHasPeers && g->transferMissingPeers) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "peers required in either the 'start' action, or *every* 'transfer' action");
    }

    if(!error) {
        g->vertexCount = igraph_vcount(g->graph);
        if(g->vertexCount != vertexCount) {
            tgen_warning("igraph_vcount %f does not match iterator count %f", g->vertexCount, vertexCount);
        }

        tgen_info("%u graph vertices ok", (guint) g->vertexCount);
    }

    return error;
}

static GError* _tgengraph_parseGraphProperties(TGenGraph* g) {
    TGEN_ASSERT(g);
    gint result = 0;

    tgen_debug("checking graph properties...");

    /* IGRAPH_WEAK means the undirected version of the graph is connected
     * IGRAPH_STRONG means a vertex can reach all others via a directed path */
    result = igraph_is_connected(g->graph, &(g->isConnected), IGRAPH_WEAK);
    if(result != IGRAPH_SUCCESS) {
        return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                "igraph_is_connected return non-success code %i", result);
    }

    igraph_integer_t clusterCount;
    result = igraph_clusters(g->graph, NULL, NULL, &(g->clusterCount), IGRAPH_WEAK);
    if(result != IGRAPH_SUCCESS) {
        return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                "igraph_clusters return non-success code %i", result);
    }

    /* it must be connected */
    if(!g->isConnected || g->clusterCount > 1) {
        return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "graph must be but is not connected");
    }

    g->isDirected = igraph_is_directed(g->graph);

    tgen_debug("checking graph attributes...");

    /* now check list of all attributes */
    igraph_strvector_t gnames, vnames, enames;
    igraph_vector_t gtypes, vtypes, etypes;
    igraph_strvector_init(&gnames, 25);
    igraph_vector_init(&gtypes, 25);
    igraph_strvector_init(&vnames, 25);
    igraph_vector_init(&vtypes, 25);
    igraph_strvector_init(&enames, 25);
    igraph_vector_init(&etypes, 25);

    result = igraph_cattribute_list(g->graph, &gnames, &gtypes, &vnames, &vtypes, &enames, &etypes);
    if(result != IGRAPH_SUCCESS) {
        return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                "igraph_cattribute_list return non-success code %i", result);
    }

    gint i = 0;
    for(i = 0; i < igraph_strvector_size(&gnames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&gnames, (glong) i, &name);

        tgen_debug("found graph attribute '%s'", name);
    }
    for(i = 0; i < igraph_strvector_size(&vnames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&vnames, (glong) i, &name);

        tgen_debug("found vertex attribute '%s'", name);
        g->knownAttributes |= _tgengraph_vertexAttributeToFlag(name);
    }
    for(i = 0; i < igraph_strvector_size(&enames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&enames, (glong) i, &name);

        tgen_debug("found edge attribute '%s'", name);
        g->knownAttributes |= _tgengraph_edgeAttributeToFlag(name);
    }

    igraph_strvector_destroy(&gnames);
    igraph_vector_destroy(&gtypes);
    igraph_strvector_destroy(&vnames);
    igraph_vector_destroy(&vtypes);
    igraph_strvector_destroy(&enames);
    igraph_vector_destroy(&etypes);

    tgen_info("successfully verified graph properties and attributes");

    return NULL;
}

static igraph_t* _tgengraph_loadNewGraph(const gchar* path) {
    /* get the file */
    FILE* graphFile = fopen(path, "r");
    if(!graphFile) {
        tgen_critical("fopen returned NULL, problem opening graph file path '%s'", path);
        return FALSE;
    }

    tgen_info("reading graphml action graph at '%s'...", path);

    igraph_t* graph = g_new0(igraph_t, 1);
    gint result = igraph_read_graph_graphml(graph, graphFile, 0);
    fclose(graphFile);

    if(result != IGRAPH_SUCCESS) {
        tgen_critical("igraph_read_graph_graphml return non-success code %i", result);
        g_free(graph);
        return NULL;
    }

    tgen_info("successfully read graphml action graph at '%s'", path);

    return graph;
}

static void _tgengraph_free(TGenGraph* g) {
    TGEN_ASSERT(g);
    g_assert(g->refcount <= 0);

    if(g->actions) {
        g_hash_table_destroy(g->actions);
    }
    if(g->weights) {
        g_hash_table_destroy(g->weights);
    }
    if(g->graph) {
        igraph_destroy(g->graph);
        g_free(g->graph);
    }
    if(g->graphPath) {
        g_free(g->graphPath);
    }

    g->magic = 0;
    g_free(g);
}

void tgengraph_ref(TGenGraph* g) {
    TGEN_ASSERT(g);
    g->refcount++;
}

void tgengraph_unref(TGenGraph* g) {
    TGEN_ASSERT(g);
    if(--g->refcount <= 0) {
        _tgengraph_free(g);
    }
}

TGenGraph* tgengraph_new(gchar* path) {
    if(!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR|G_FILE_TEST_EXISTS)) {
        tgen_critical("path '%s' to tgen config graph is not valid or does not exist", path);
        return NULL;
    }

    TGenGraph* g = g_new0(TGenGraph, 1);
    g->magic = TGEN_MAGIC;
    g->refcount = 1;

    g->actions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_tgengraph_freeAction);
    g->weights = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    g->graphPath = path ? _tgengraph_getHomePath(path) : NULL;

    GError* error = NULL;

    gboolean exists = g_file_test(g->graphPath, G_FILE_TEST_IS_REGULAR|G_FILE_TEST_EXISTS);
    if(!exists) {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                                    "graph file does not exist at path '%s'", g->graphPath);
    }

    if(!error && g->graphPath) {
        /* note - this if block requires a global lock if using the same igraph library
         * from multiple threads at the same time. this is not a problem when shadow
         * uses dlmopen to get a private namespace for each plugin. */

        /* use the built-in C attribute handler */
        igraph_attribute_table_t* oldHandler = igraph_i_set_attribute_table(&igraph_cattribute_table);

        g->graph = _tgengraph_loadNewGraph(g->graphPath);
        if(!g->graph) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                                    "unable to read graph at path '%s'", g->graphPath);
        }

        /* parse edges first for choose, needs hash table of weights filled for error handling */
        if(!error) {
            error = _tgengraph_parseGraphProperties(g);
        }
        if(!error) {
            error = _tgengraph_parseGraphEdges(g);
        }
        if(!error) {
            error = _tgengraph_parseGraphVertices(g);
        }

        /* replace the old handler */
        igraph_i_set_attribute_table(oldHandler);
    }

    if(error) {
        tgen_critical("error (%i) while loading graph: %s", error->code, error->message);
        g_error_free(error);
        tgengraph_unref(g);
        return NULL;
    }

    tgen_message("successfully loaded graphml file '%s' and validated actions: "
            "graph is %s with %u %s, %u %s, and %u %s", g->graphPath,
            g->isConnected ? "weakly connected" : "disconnected",
            (guint)g->clusterCount, g->clusterCount == 1 ? "cluster" : "clusters",
            (guint)g->vertexCount, g->vertexCount == 1 ? "vertex" : "vertices",
            (guint)g->edgeCount, g->edgeCount == 1 ? "edge" : "edges");

    return g;
}

TGenActionID tgengraph_getStartActionID(TGenGraph* g) {
    TGEN_ASSERT(g);
    return (TGenActionID)g->startActionVertexIndex;
}

GQueue* tgengraph_getNextActionIDs(TGenGraph* g, TGenActionID actionID) {
    TGEN_ASSERT(g);

    /* given an action, get all of the next actions in the dependency graph */

    igraph_integer_t srcVertexIndex = (igraph_integer_t) actionID;

    /* initialize a vector to hold the result neighbor vertices for this action */
    igraph_vector_t* resultNeighborVertices = g_new0(igraph_vector_t, 1);

    /* initialize with 0 entries, since we dont know how many neighbors we have */
    gint result = igraph_vector_init(resultNeighborVertices, 0);
    if(result != IGRAPH_SUCCESS) {
        tgen_critical("igraph_vector_init return non-success code %i", result);
        g_free(resultNeighborVertices);
        return NULL;
    }

    /* now get all outgoing 1-hop neighbors of the given action */
    result = igraph_neighbors(g->graph, resultNeighborVertices, srcVertexIndex, IGRAPH_OUT);
    if(result != IGRAPH_SUCCESS) {
        tgen_critical("igraph_neighbors return non-success code %i", result);
        igraph_vector_destroy(resultNeighborVertices);
        g_free(resultNeighborVertices);
        return NULL;
    }

    /* handle the results */
    glong nVertices = igraph_vector_size(resultNeighborVertices);
    tgen_debug("found %li outgoing neighbors from vertex %i", nVertices, (gint)srcVertexIndex);

    /* only follow one edge of all edges with the 'weight' attribute (do a weighted choice)
     * but follow all edges without the 'weight' attribute */
    GQueue* nextActions = g_queue_new();
    GQueue* chooseActions = g_queue_new();
    GQueue* chooseWeights = g_queue_new();
    gdouble totalWeight = 0.0;

    for (gint i = 0; i < nVertices; i++) {
        /* we have source, get destination */
        igraph_integer_t dstVertexIndex = igraph_vector_e(resultNeighborVertices, i);

        TGenAction* nextAction = _tgengraph_getAction(g, dstVertexIndex);
        if(!nextAction) {
            tgen_debug("src vertex %i dst vertex %i, next action is null", (gint)srcVertexIndex, (gint)dstVertexIndex);
            continue;
        }

        /* get edge id so we can check for weight */
        igraph_integer_t edgeIndex = 0;
        result = igraph_get_eid(g->graph, &edgeIndex, srcVertexIndex, dstVertexIndex, IGRAPH_DIRECTED, TRUE);
        if(result != IGRAPH_SUCCESS) {
            tgen_critical("igraph_get_eid return non-success code %i", result);
            igraph_vector_destroy(resultNeighborVertices);
            g_free(resultNeighborVertices);
            g_queue_free(nextActions);
            g_queue_free(chooseActions);
            g_queue_free(chooseWeights);
            return NULL;
        }

        /* check for a weight on the edge */
        gdouble* weightPtr = _tgengraph_getWeight(g, edgeIndex);

        if(weightPtr) {
            /* we will only choose one of all with weights */
            totalWeight += (gdouble) *weightPtr;
            g_queue_push_tail(chooseWeights, weightPtr);
            g_queue_push_tail(chooseActions, GINT_TO_POINTER(dstVertexIndex));
        } else {
            /* no weight, always add it */
            g_queue_push_tail(nextActions, GINT_TO_POINTER(dstVertexIndex));
        }
    }

    /* choose only one from 'choices' and add it to the next queue */
    guint numChoices = g_queue_get_length(chooseActions);
    if(numChoices > 0) {
        tgen_debug("src vertex %i, choosing among %u weighted outgoing edges", (gint)srcVertexIndex, numChoices);

        /* count up weights until the cumulative exceeds the random choice */
        gdouble cumulativeWeight = 0.0;
        guint nextChoicePosition = 0;
        /* do a weighted choice, this return a val in the range [0.0, totalWeight) */
        gdouble randomWeight = g_random_double_range((gdouble)0.0, totalWeight);

        do {
            gdouble* choiceWeightPtr = g_queue_pop_head(chooseWeights);
            g_assert(choiceWeightPtr);
            cumulativeWeight += *choiceWeightPtr;
            nextChoicePosition++;
        } while(cumulativeWeight <= randomWeight);

        /* the weight position matches the action position */
        gpointer choiceAction = g_queue_peek_nth(chooseActions, nextChoicePosition-1);
        g_queue_push_tail(nextActions, choiceAction);
    }

    /* cleanup */
    igraph_vector_destroy(resultNeighborVertices);
    g_free(resultNeighborVertices);
    g_queue_free(chooseActions);
    g_queue_free(chooseWeights);

    tgen_debug("src vertex %i, we have %u next actions", (gint)srcVertexIndex, g_queue_get_length(nextActions));

    return nextActions;
}

gboolean tgengraph_hasEdges(TGenGraph* g) {
    TGEN_ASSERT(g);
    return (g->edgeCount > 0) ? TRUE : FALSE;
}

const gchar* tgengraph_getActionIDStr(TGenGraph* g, TGenActionID actionID) {
    TGEN_ASSERT(g);

    igraph_integer_t vertexIndex = (igraph_integer_t) actionID;
    const gchar* idStr = VAS(g->graph, "id", vertexIndex);
    return idStr;
}

const gchar* tgengraph_getGraphPath(TGenGraph* g) {
    TGEN_ASSERT(g);
    return g->graphPath;
}

GLogLevelFlags tgengraph_getLogLevel(TGenGraph* g) {
    TGEN_ASSERT(g);
    TGenAction* startAction = _tgengraph_getAction(g, g->startActionVertexIndex);
    g_assert(startAction);
    TGenStartOptions* options = startAction->options;
    g_assert(options);
    if(options->loglevel.isSet) {
        return options->loglevel.value;
    } else {
        return G_LOG_LEVEL_MESSAGE;
    }
}

guint64 tgengraph_getHeartbeatPeriodMillis(TGenGraph* g) {
    TGEN_ASSERT(g);
    TGenAction* startAction = _tgengraph_getAction(g, g->startActionVertexIndex);
    g_assert(startAction);
    TGenStartOptions* options = startAction->options;
    g_assert(options);
    if(options->heartbeatPeriodNanos.isSet) {
        return (guint64)(options->heartbeatPeriodNanos.value / 1000000);
    } else {
        return 1000;
    }
}

//guint16 tgenaction_getServerPort(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_START);
//    return ((TGenActionStartData*)action->data)->serverport;
//}
//
//TGenPeer* tgenaction_getSocksProxy(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_START);
//    return ((TGenActionStartData*)action->data)->socksproxy;
//}
//
//guint64 tgenaction_getStartTimeMillis(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_START);
//    return (guint64)(((TGenActionStartData*)action->data)->timeNanos / 1000000);
//}
//
//guint64 tgenaction_getDefaultTimeoutMillis(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_START);
//    return (guint64)(((TGenActionStartData*)action->data)->timeoutNanos / 1000000);
//}
//
//guint64 tgenaction_getDefaultStalloutMillis(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_START);
//    return (guint64)(((TGenActionStartData*)action->data)->stalloutNanos / 1000000);
//}
//
//guint64 tgenaction_getHeartbeatPeriodMillis(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_START);
//    return (guint64)(((TGenActionStartData*)action->data)->heartbeatPeriodNanos / 1000000);
//}
//
//void tgenaction_getTransferParameters(TGenAction* action, TGenTransferType* typeOut,
//        TGenTransportProtocol* protocolOut, guint64* sizeOut, guint64 *ourSizeOut,
//        guint64 *theirSizeOut, guint64* timeoutOut, guint64* stalloutOut,
//        gchar** localSchedule, gchar** remoteSchedule) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_TRANSFER);
//
//    TGenActionTransferData* data = (TGenActionTransferData*)action->data;
//
//    if(typeOut) {
//        *typeOut = data->type;
//    }
//    if(protocolOut) {
//        *protocolOut = data->protocol;
//    }
//    if(sizeOut) {
//        *sizeOut = data->size;
//    }
//    if (ourSizeOut) {
//        *ourSizeOut = data->ourSize;
//    }
//    if (theirSizeOut) {
//        *theirSizeOut = data->theirSize;
//    }
//    if(timeoutOut) {
//        if(data->timeoutIsSet) {
//            /* nanoseconds to milliseconds */
//            *timeoutOut = (guint64)(data->timeoutNanos / 1000000);
//        }
//    }
//    if(stalloutOut) {
//        if(data->stalloutIsSet) {
//            /* nanoseconds to milliseconds */
//            *stalloutOut = (guint64)(data->stalloutNanos / 1000000);
//        }
//    }
//    if(localSchedule) {
//        *localSchedule = data->localSchedule;
//    }
//    if(remoteSchedule) {
//        *remoteSchedule = data->remoteSchedule;
//    }
//}
//
//void tgenaction_getModelPaths(TGenAction* action,
//        gchar** streamModelPathStr, gchar** packetModelPathStr) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_MODEL);
//
//    TGenActionModelData* data = (TGenActionModelData*)action->data;
//
//    if(streamModelPathStr) {
//        *streamModelPathStr = data->streamModelPath;
//    }
//    if(packetModelPathStr) {
//        *packetModelPathStr = data->packetModelPath;
//    }
//}
//
//void tgenaction_getSocksParams(TGenAction* action,
//        gchar** socksUsernameStr, gchar** socksPasswordStr) {
//    TGEN_ASSERT(action);
//    g_assert(action->data);
//    g_assert(action->type == TGEN_ACTION_MODEL || action->type == TGEN_ACTION_TRANSFER);
//
//    gchar* userStr = NULL;
//    gchar* passStr = NULL;
//
//    if(action->type == TGEN_ACTION_TRANSFER) {
//      TGenActionTransferData* data = (TGenActionTransferData*)action->data;
//      userStr = data->socksUsernameStr;
//      passStr = data->socksPasswordStr;
//    } else if(action->type == TGEN_ACTION_MODEL) {
//      TGenActionModelData* data = (TGenActionModelData*)action->data;
//      userStr = data->socksUsernameStr;
//      passStr = data->socksPasswordStr;
//    }
//
//    if(socksUsernameStr) {
//        *socksUsernameStr = userStr;
//    }
//    if(socksPasswordStr) {
//        *socksPasswordStr = passStr;
//    }
//}
//
//TGenPool* tgenaction_getPeers(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data);
//
//    if(action->type == TGEN_ACTION_TRANSFER) {
//        return ((TGenActionTransferData*)action->data)->peers;
//    } else if(action->type == TGEN_ACTION_MODEL) {
//        return ((TGenActionModelData*)action->data)->peers;
//    } else if(action->type == TGEN_ACTION_START) {
//        return ((TGenActionStartData*)action->data)->peers;
//    } else {
//        return NULL;
//    }
//}
//
//guint64 tgenaction_getEndTimeMillis(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_END);
//    return (guint64)(((TGenActionEndData*)action->data)->timeNanos / 1000000);
//}
//
//guint64 tgenaction_getEndCount(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_END);
//    return ((TGenActionEndData*)action->data)->count;
//}
//
//guint64 tgenaction_getEndSize(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_END);
//    return ((TGenActionEndData*)action->data)->size;
//}
//
//gboolean tgenaction_hasPauseTime(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_PAUSE);
//    return (((TGenActionPauseData*)action->data)->pauseTimesNanos) != NULL ? TRUE : FALSE;
//}
//
//guint64 tgenaction_getPauseTimeMillis(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_PAUSE);
//    guint64* time = tgenpool_getRandom(((TGenActionPauseData*)action->data)->pauseTimesNanos);
//    return (guint64)(*time / 1000000);
//}
//
//gboolean tgenaction_incrementPauseVisited(TGenAction* action) {
//    TGEN_ASSERT(action);
//    g_assert(action->data && action->type == TGEN_ACTION_PAUSE);
//    TGenActionPauseData* pauseData = (TGenActionPauseData*)action->data;
//
//    pauseData->completedIncomingEdges++;
//    if(pauseData->completedIncomingEdges >= pauseData->totalIncomingEdges) {
//        pauseData->completedIncomingEdges = 0;
//        return TRUE;
//    } else {
//        return FALSE;
//    }
//}
