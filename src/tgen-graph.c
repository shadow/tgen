/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>
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
    TGEN_VA_PACKETMODELMODE = 1 << 8,
    TGEN_VA_MARKOVMODELSEED = 1 << 9,
    TGEN_VA_PEERS = 1 << 10,
    TGEN_VA_SOCKSPROXY = 1 << 11,
    TGEN_VA_SOCKSUSERNAME = 1 << 12,
    TGEN_VA_SOCKSPASSWORD = 1 << 13,
    TGEN_VA_SOCKSAUTHSEED = 1 << 14,
    TGEN_VA_SENDSIZE = 1 << 15,
    TGEN_VA_RECVSIZE = 1 << 16,
    TGEN_VA_TIMEOUT = 1 << 17,
    TGEN_VA_STALLOUT = 1 << 18,
    TGEN_VA_STREAMMODELPATH = 1 << 19,
    TGEN_VA_FLOWMODELPATH = 1 << 20,
    TGEN_VA_COUNT = 1 << 21,
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
    GHashTable* validatedMarkovModelPaths;

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
            _tgengraph_freeActionHelper(TGEN_ACTION_TRAFFIC, &options->defaultTrafficOpts);
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
            if(options->packetModelMode.value) {
                g_free(options->packetModelMode.value);
            }
            if(options->seedGenerator.value) {
                tgenpool_unref(options->seedGenerator.value);
            }
            if(options->peers.value) {
                tgenpool_unref(options->peers.value);
            }
            if(options->socksProxies.value) {
                tgenpool_unref(options->socksProxies.value);
            }
            if(options->socksUsername.value) {
                g_free(options->socksUsername.value);
            }
            if(options->socksPassword.value) {
                g_free(options->socksPassword.value);
            }
            if(options->socksAuthGenerator.value) {
                tgenpool_unref(options->socksAuthGenerator.value);
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
    } else if (type == TGEN_ACTION_TRAFFIC) {
        TGenTrafficOptions* options = optionsptr;
        if(options) {
            if(options->flowModelPath.value) {
                g_free(options->flowModelPath.value);
            }
            _tgengraph_freeActionHelper(TGEN_ACTION_FLOW, &options->flowOpts);
        }
    }
}

static void _tgengraph_freeAction(TGenAction* action) {
    TGEN_ASSERT(action);
    /* free all the internal options that we allocated */
    _tgengraph_freeActionHelper(action->type, action->options);
    action->magic = 0;
    if(action->options) {
        g_free(action->options);
    }
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
        case TGEN_VA_PACKETMODELMODE: {
            return "packetmodelmode";
        }
        case TGEN_VA_MARKOVMODELSEED: {
            return "markovmodelseed";
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
        case TGEN_VA_SOCKSAUTHSEED: {
            return "socksauthseed";
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
        case TGEN_VA_FLOWMODELPATH: {
            return "flowmodelpath";
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
                _tgengraph_attributeToString(TGEN_VA_PACKETMODELMODE))) {
            return TGEN_VA_PACKETMODELMODE;
        } else if(!g_ascii_strcasecmp(stringAttribute,
                _tgengraph_attributeToString(TGEN_VA_MARKOVMODELSEED))) {
            return TGEN_VA_MARKOVMODELSEED;
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
                _tgengraph_attributeToString(TGEN_VA_SOCKSAUTHSEED))) {
            return TGEN_VA_SOCKSAUTHSEED;
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
                _tgengraph_attributeToString(TGEN_VA_FLOWMODELPATH))) {
            return TGEN_VA_FLOWMODELPATH;
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
                    (glong)fromVertexIndex, _tgengraph_attributeToString(TGEN_VA_ID));
            break;
        }

        const gchar* toIDStr = (g->knownAttributes&TGEN_VA_ID) ?
                VAS(g->graph, _tgengraph_attributeToString(TGEN_VA_ID), toVertexIndex) : NULL;
        if(!toIDStr) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                    "found vertex %li with missing '%s' attribute",
                    (glong)toVertexIndex, _tgengraph_attributeToString(TGEN_VA_ID));
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
    tgen_debug("found %li incoming 1-hop neighbors to vertex %li", totalIncoming, (glong)vertexIndex);

    /* cleanup */
    igraph_vector_destroy(resultNeighborVertices);
    g_free(resultNeighborVertices);

    return totalIncoming;
}

static GError* _tgengraph_validateMarkovModel(TGenGraph* g, const gchar* path, guint32 seed) {
    GError* error = NULL;

    /* check if we already validated this path */
    if(g->validatedMarkovModelPaths) {
        if(g_hash_table_contains(g->validatedMarkovModelPaths, path)) {
            /* this path was already used in the config and we already validated it */
            return NULL;
        }
    }

    gchar* name = g_path_get_basename(path);

    TGenMarkovModel* mmodel = tgenmarkovmodel_newFromPath(name, seed, path);

    g_free(name);

    if(mmodel) {
        tgen_message("Validation of Markov model at path '%s' was successful!", path);
        tgenmarkovmodel_unref(mmodel);

        /* store the path so we don't try to validate it again if it is used in another action */
        if(!g->validatedMarkovModelPaths) {
            g->validatedMarkovModelPaths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        }

        /* push a copy to the hash table, which takes ownership and will destroy it */
        gchar* pathCopy = g_strdup(path);
        g_hash_table_replace(g->validatedMarkovModelPaths, pathCopy, GINT_TO_POINTER(1));
    } else {
        error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                "Validation failed for Markov model at path '%s', "
                "please check the format of the file contents and try again.", path);
    }

    return error;
}

static GError* _tgengraph_parseStreamAttributesHelper(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex, TGenStreamOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    GError* error = NULL;

    if(g->knownAttributes & TGEN_VA_PACKETMODELPATH) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_PACKETMODELPATH);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseString(name, valueStr, &options->packetModelPath);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_PACKETMODELMODE) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_PACKETMODELMODE);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseString(name, valueStr, &options->packetModelMode);
        if(error) {
            return error;
        }
    }

    if(g->knownAttributes & TGEN_VA_MARKOVMODELSEED) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_MARKOVMODELSEED);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseUInt32(name, valueStr, &options->markovModelSeed);
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
        error = tgenoptionparser_parsePeerList(name, valueStr, &options->socksProxies);
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

    if(g->knownAttributes & TGEN_VA_SOCKSAUTHSEED) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_SOCKSAUTHSEED);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseUInt32(name, valueStr, &options->socksAuthSeed);
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

    /* validate the packet markov model */
    if(options->packetModelPath.isSet) {
        gchar* path = options->packetModelPath.value;
        guint32 seed = 12345;
        error = _tgengraph_validateMarkovModel(g, path, seed);
        if(error) {
            return error;
        }
    }

    if(options->packetModelMode.isSet) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_PACKETMODELMODE);
        gchar* mode = options->packetModelMode.value;

        /* if its not 'path' AND its not 'graphml', then its an error */
        if(g_ascii_strcasecmp(mode, "path") && g_ascii_strcasecmp(mode, "graphml")) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                                "The value of the '%s' attribute must be either 'path' or "
                                "'graphml', we got '%s'; please update your config and try again",
                                name, mode);
        }

        if(error) {
            return error;
        }
    }

    if(options->markovModelSeed.isSet) {
        GRand* seedPrng = g_rand_new_with_seed(options->markovModelSeed.value);
        TGenPool* seedPrngContainer = tgenpool_new((GDestroyNotify)g_rand_free);
        tgenpool_add(seedPrngContainer, seedPrng);

        options->seedGenerator.isSet = TRUE;
        options->seedGenerator.value = seedPrngContainer;
    }

    if(options->socksAuthSeed.isSet) {
        GRand* authPrng = g_rand_new_with_seed(options->socksAuthSeed.value);
        TGenPool* authPrngContainer = tgenpool_new((GDestroyNotify)g_rand_free);
        tgenpool_add(authPrngContainer, authPrng);

        options->socksAuthGenerator.isSet = TRUE;
        options->socksAuthGenerator.value = authPrngContainer;
    }

    return error;
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

    error = _tgengraph_parseStreamAttributesHelper(g, idStr, vertexIndex, &options->streamOpts);
    if(error) {
        return error;
    }

    /* validate the stream Markov model */
    if(options->streamModelPath.isSet) {
        gchar* path = options->streamModelPath.value;
        guint32 seed = 12345;
        error = _tgengraph_validateMarkovModel(g, path, seed);
        if(error) {
            return error;
        }
    }

    return NULL;
}

static GError* _tgengraph_parseTrafficAttributesHelper(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex, TGenTrafficOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    GError* error = NULL;

    if(g->knownAttributes & TGEN_VA_FLOWMODELPATH) {
        const gchar* name = _tgengraph_attributeToString(TGEN_VA_FLOWMODELPATH);
        const gchar* valueStr = VAS(g->graph, name, vertexIndex);
        error = tgenoptionparser_parseString(name, valueStr, &options->flowModelPath);
        if(error) {
            return error;
        }
    }

    error = _tgengraph_parseFlowAttributesHelper(g, idStr, vertexIndex, &options->flowOpts);
    if(error) {
        return error;
    }

    /* validate the flow Markov model */
    if(options->flowModelPath.isSet) {
        gchar* path = options->flowModelPath.value;
        guint32 seed = 12345;
        error = _tgengraph_validateMarkovModel(g, path, seed);
        if(error) {
            return error;
        }
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

    error = _tgengraph_parseTrafficAttributesHelper(g, idStr, vertexIndex, &options->defaultTrafficOpts);
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

    if(options->defaultTrafficOpts.flowOpts.streamOpts.peers.isSet) {
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

static GError* _tgengraph_parseTrafficVertex(TGenGraph* g, const gchar* idStr,
        igraph_integer_t vertexIndex) {
    TGEN_ASSERT(g);

    GError* error = NULL;
    TGenTrafficOptions* options = g_new0(TGenTrafficOptions, 1);

    error = _tgengraph_parseTrafficAttributesHelper(g, idStr, vertexIndex, options);
    if(error) {
        g_free(options);
        return error;
    }

    TGenAction* action = _tgengraph_newAction(TGEN_ACTION_TRAFFIC, options);
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
        } else if(g_strstr_len(idStr, (gssize)-1, "traffic")) {
            error = _tgengraph_parseTrafficVertex(g, idStr, vertexIndex);
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
                    "peers required in either the 'start' action, or *every* 'stream' action");
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

    GError* error = NULL;
    gint i = 0;
    for(i = 0; !error && i < igraph_strvector_size(&gnames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&gnames, (glong) i, &name);

        tgen_debug("found graph attribute '%s'", name);
    }

    for(i = 0; !error && i < igraph_strvector_size(&vnames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&vnames, (glong) i, &name);

        tgen_debug("found vertex attribute '%s'", name);

        AttributeFlags flag = _tgengraph_vertexAttributeToFlag(name);

        if(flag == TGEN_A_NONE) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                    "Vertex attribute '%s' is unknown, "
                    "please check your config and try again.", name);
        } else {
            g->knownAttributes |= flag;
        }
    }

    for(i = 0; !error && i < igraph_strvector_size(&enames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&enames, (glong) i, &name);

        tgen_debug("found edge attribute '%s'", name);

        AttributeFlags flag = _tgengraph_edgeAttributeToFlag(name);

        if(flag == TGEN_A_NONE) {
            error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                    "Edge attribute '%s' is unknown, "
                    "please check your config and try again.", name);
        } else {
            g->knownAttributes |= flag;
        }
    }

    igraph_strvector_destroy(&gnames);
    igraph_vector_destroy(&gtypes);
    igraph_strvector_destroy(&vnames);
    igraph_vector_destroy(&vtypes);
    igraph_strvector_destroy(&enames);
    igraph_vector_destroy(&etypes);

    if(error) {
        tgen_warning("failed to verify graph properties and attributes");
    } else {
        tgen_info("successfully verified graph properties and attributes");
    }

    return error;
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
    if(g->validatedMarkovModelPaths) {
        g_hash_table_destroy(g->validatedMarkovModelPaths);
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

    /* we don't need the Markov model validation cache any more */
    if(g->validatedMarkovModelPaths) {
        g_hash_table_destroy(g->validatedMarkovModelPaths);
        /* make sure it does not get destroyed again in _tgengraph_free() */
        g->validatedMarkovModelPaths = NULL;
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
    tgen_debug("found %li outgoing neighbors from vertex %li", nVertices, (glong)srcVertexIndex);

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
            tgen_debug("src vertex %i dst vertex %i, next action is null",
                    (gint)srcVertexIndex, (gint)dstVertexIndex);
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
        tgen_debug("src vertex %i, choosing among %u weighted outgoing edges",
                (gint)srcVertexIndex, numChoices);

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

    tgen_debug("src vertex %i, we have %u next actions",
            (gint)srcVertexIndex, g_queue_get_length(nextActions));

    return nextActions;
}

gboolean tgengraph_hasEdges(TGenGraph* g) {
    TGEN_ASSERT(g);
    return (g->edgeCount > 0) ? TRUE : FALSE;
}

const gchar* tgengraph_getGraphPath(TGenGraph* g) {
    TGEN_ASSERT(g);
    return g->graphPath;
}

TGenActionType tgengraph_getActionType(TGenGraph* g, TGenActionID actionID) {
    TGEN_ASSERT(g);
    TGenAction* action = _tgengraph_getAction(g, (igraph_integer_t) actionID);
    if(action) {
        return action->type;
    } else {
        return TGEN_ACTION_NONE;
    }
}

const gchar* tgengraph_getActionName(TGenGraph* g, TGenActionID actionID) {
    TGEN_ASSERT(g);

    igraph_integer_t vertexIndex = (igraph_integer_t) actionID;
    const gchar* idStr = VAS(g->graph, "id", vertexIndex);
    return idStr;
}

static gpointer _tgengraph_getOptionsHelper(TGenGraph* g, TGenActionID actionID,
        TGenActionType actionType, const gchar* name) {
    TGEN_ASSERT(g);

    /* get the action, which must be non-null in a valid graph */
    TGenAction* action = _tgengraph_getAction(g, (igraph_integer_t) actionID);
    if(!action) {
        tgen_error("The action object is NULL for vertex %i", (gint) actionID);
    }

    if(action->type != actionType) {
        tgen_error("Action type is not %s for vertex %i", name, (gint) actionID);
    }

    /* get the options, which must be non-null in a valid graph */
    gpointer options = action->options;
    if(!options) {
        tgen_error("The %s options object is NULL for vertex %i", name, (gint) actionID);
    }

    return options;
}

TGenStartOptions* tgengraph_getStartOptions(TGenGraph* g) {
    gpointer options = _tgengraph_getOptionsHelper(g, (TGenActionID)g->startActionVertexIndex,
            TGEN_ACTION_START, "start");
    return (TGenStartOptions*) options;
}

TGenPauseOptions* tgengraph_getPauseOptions(TGenGraph* g, TGenActionID actionID) {
    gpointer options = _tgengraph_getOptionsHelper(g, actionID, TGEN_ACTION_PAUSE, "pause");
    return (TGenPauseOptions*) options;
}

TGenEndOptions* tgengraph_getEndOptions(TGenGraph* g, TGenActionID actionID) {
    gpointer options = _tgengraph_getOptionsHelper(g, actionID, TGEN_ACTION_END, "end");
    return (TGenEndOptions*) options;
}

static void _tgengraph_copyDefaultStreamOptions(TGenGraph* g, TGenStreamOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    /* for all the given stream options that were not explicitly set, check if a
     * default was set in start vertex and copy the default if it was. */

    TGenStartOptions* startOptions = tgengraph_getStartOptions(g);
    g_assert(startOptions);

    TGenStreamOptions* defaults = &startOptions->defaultTrafficOpts.flowOpts.streamOpts;

    if(!options->packetModelPath.isSet && defaults->packetModelPath.isSet) {
        options->packetModelPath.isSet = TRUE;
        options->packetModelPath.value = g_strdup(defaults->packetModelPath.value);
    }

    if(!options->packetModelMode.isSet && defaults->packetModelMode.isSet) {
        options->packetModelMode.isSet = TRUE;
        options->packetModelMode.value = g_strdup(defaults->packetModelMode.value);
    }

    if(!options->markovModelSeed.isSet && defaults->markovModelSeed.isSet) {
        options->markovModelSeed.isSet = TRUE;
        options->markovModelSeed.value = defaults->markovModelSeed.value;
    }

    if(!options->seedGenerator.isSet && defaults->seedGenerator.isSet) {
        options->seedGenerator.isSet = TRUE;
        options->seedGenerator.value = defaults->seedGenerator.value;
        tgenpool_ref(defaults->seedGenerator.value);
    }

    if(!options->peers.isSet && defaults->peers.isSet) {
        options->peers.isSet = TRUE;
        options->peers.value = defaults->peers.value;
        tgenpool_ref(defaults->peers.value);
    }

    if(!options->recvSize.isSet && defaults->recvSize.isSet) {
        options->recvSize.isSet = TRUE;
        options->recvSize.value = defaults->recvSize.value;
    }

    if(!options->sendSize.isSet && defaults->sendSize.isSet) {
        options->sendSize.isSet = TRUE;
        options->sendSize.value = defaults->sendSize.value;
    }

    if(!options->socksProxies.isSet && defaults->socksProxies.isSet) {
        options->socksProxies.isSet = TRUE;
        options->socksProxies.value = defaults->socksProxies.value;
        tgenpool_ref(defaults->socksProxies.value);
    }

    if(!options->socksUsername.isSet && defaults->socksUsername.isSet) {
        options->socksUsername.isSet = TRUE;
        options->socksUsername.value = g_strdup(defaults->socksUsername.value);
    }

    if(!options->socksPassword.isSet && defaults->socksPassword.isSet) {
        options->socksPassword.isSet = TRUE;
        options->socksPassword.value = g_strdup(defaults->socksPassword.value);
    }

    if(!options->socksAuthSeed.isSet && defaults->socksAuthSeed.isSet) {
        options->socksAuthSeed.isSet = TRUE;
        options->socksAuthSeed.value = defaults->socksAuthSeed.value;
    }

    if(!options->socksAuthGenerator.isSet && defaults->socksAuthGenerator.isSet) {
        options->socksAuthGenerator.isSet = TRUE;
        options->socksAuthGenerator.value = defaults->socksAuthGenerator.value;
        tgenpool_ref(defaults->socksAuthGenerator.value);
    }

    if(!options->stalloutNanos.isSet && defaults->stalloutNanos.isSet) {
        options->stalloutNanos.isSet = TRUE;
        options->stalloutNanos.value = defaults->stalloutNanos.value;
    }

    if(!options->timeoutNanos.isSet && defaults->timeoutNanos.isSet) {
        options->timeoutNanos.isSet = TRUE;
        options->timeoutNanos.value = defaults->timeoutNanos.value;
    }
}

static void _tgengraph_copyDefaultFlowOptions(TGenGraph* g, TGenFlowOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    /* for all the given options that were not explicitly set, check if a
     * default was set in start vertex and copy the default if it was. */

    TGenStartOptions* startOptions = tgengraph_getStartOptions(g);
    g_assert(startOptions);

    TGenFlowOptions* defaults = &startOptions->defaultTrafficOpts.flowOpts;

    if(!options->streamModelPath.isSet && defaults->streamModelPath.isSet) {
        options->streamModelPath.isSet = TRUE;
        options->streamModelPath.value = g_strdup(defaults->streamModelPath.value);
    }
}

static void _tgengraph_copyDefaultTrafficOptions(TGenGraph* g, TGenTrafficOptions* options) {
    TGEN_ASSERT(g);
    g_assert(options);

    /* for all the given options that were not explicitly set, check if a
     * default was set in start vertex and copy the default if it was. */

    TGenStartOptions* startOptions = tgengraph_getStartOptions(g);
    g_assert(startOptions);

    TGenTrafficOptions* defaults = &startOptions->defaultTrafficOpts;

    if(!options->flowModelPath.isSet && defaults->flowModelPath.isSet) {
        options->flowModelPath.isSet = TRUE;
        options->flowModelPath.value = g_strdup(defaults->flowModelPath.value);
    }
}

TGenStreamOptions* tgengraph_getStreamOptions(TGenGraph* g, TGenActionID actionID) {
    gpointer options = _tgengraph_getOptionsHelper(g, actionID, TGEN_ACTION_STREAM, "stream");
    TGenStreamOptions* streamOptions = (TGenStreamOptions*) options;
    _tgengraph_copyDefaultStreamOptions(g, streamOptions);
    return streamOptions;
}

TGenFlowOptions* tgengraph_getFlowOptions(TGenGraph* g, TGenActionID actionID) {
    gpointer options = _tgengraph_getOptionsHelper(g, actionID, TGEN_ACTION_FLOW, "flow");
    TGenFlowOptions* flowOptions = (TGenFlowOptions*) options;
    _tgengraph_copyDefaultStreamOptions(g, &flowOptions->streamOpts);
    _tgengraph_copyDefaultFlowOptions(g, flowOptions);
    return flowOptions;
}

TGenTrafficOptions* tgengraph_getTrafficOptions(TGenGraph* g, TGenActionID actionID) {
    gpointer options = _tgengraph_getOptionsHelper(g, actionID, TGEN_ACTION_TRAFFIC, "traffic");
    TGenTrafficOptions* trafficOptions = (TGenTrafficOptions*) options;
    _tgengraph_copyDefaultStreamOptions(g, &trafficOptions->flowOpts.streamOpts);
    _tgengraph_copyDefaultFlowOptions(g, &trafficOptions->flowOpts);
    _tgengraph_copyDefaultTrafficOptions(g, trafficOptions);
    return trafficOptions;
}

gboolean tgengraph_incrementPauseVisited(TGenGraph* g, TGenActionID actionID) {
    TGEN_ASSERT(g);

    TGenAction* action = _tgengraph_getAction(g, (igraph_integer_t) actionID);
    if(!action) {
        tgen_error("The action object is NULL for vertex %i", (gint) actionID);
    }

    if(action->type != TGEN_ACTION_PAUSE) {
        tgen_error("Action type is not pause for vertex %i", (gint) actionID);
    }

    action->completedIncomingEdges++;
    if(action->completedIncomingEdges >= action->totalIncomingEdges) {
        action->completedIncomingEdges = 0;
        return TRUE;
    } else {
        return FALSE;
    }
}
