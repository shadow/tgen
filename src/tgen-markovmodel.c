/*
 * See LICENSE for licensing information
 */

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

#include <igraph.h>

#include "tgen-log.h"
#include "tgen-markovmodel.h"

#if 1 /* #ifdef DEBUG */
#define TGEN_MMODEL_MAGIC 0xDEEFCABA
#define TGEN_MMODEL_ASSERT(obj) g_assert(obj && (obj->magic == TGEN_MMODEL_MAGIC))
#else
#define TGEN_MMODEL_MAGIC 0
#define TGEN_MMODEL_ASSERT(obj)
#endif

typedef enum _VertexAttribute VertexAttribute;
enum _VertexAttribute {
    VERTEX_ATTR_ID=1,
    VERTEX_ATTR_TYPE=2,
};

typedef enum _VertexID VertexID;
enum _VertexID {
    VERTEX_ID_START=3,
    VERTEX_ID_TO_SERVER=4,
    VERTEX_ID_TO_ORIGIN=5,
    VERTEX_ID_END=6,
};

typedef enum _VertexType VertexType;
enum _VertexType {
    VERTEX_TYPE_STATE=7,
    VERTEX_TYPE_OBSERVATION=8,
};

typedef enum _EdgeAttribute EdgeAttribute;
enum _EdgeAttribute {
    EDGE_ATTR_TYPE=9,
    EDGE_ATTR_WEIGHT=10,
    EDGE_ATTR_DISTRIBUTION=11,
    EDGE_ATTR_PARAM_LOCATION=12,
    EDGE_ATTR_PARAM_SCALE=13,
    EDGE_ATTR_PARAM_RATE=14,
    EDGE_ATTR_PARAM_SHAPE=15,
};

typedef enum _EdgeType EdgeType;
enum _EdgeType {
    EDGE_TYPE_TRANSITION=16,
    EDGE_TYPE_EMISSION=17,
};

typedef enum _EdgeDistribution EdgeDistribution;
enum _EdgeDistribution {
    EDGE_DISTRIBUTION_NORMAL=18,
    EDGE_DISTRIBUTION_LOGNORMAL=19,
    EDGE_DISTRIBUTION_EXPONENTIAL=20,
    EDGE_DISTRIBUTION_PARETO=21,
};

struct _TGenMarkovModel {
    gint refcount;

    /* For generating deterministic pseudo-random sequences. */
    GRand* prng;
    guint32 prngSeed;

    /* The name of the graphml file that we loaded. */
    gchar* name;

    igraph_t* graph;
    igraph_integer_t startVertexIndex;
    igraph_integer_t currentStateVertexIndex;
    gboolean foundEndState;

    guint magic;
};

static const gchar* _tgenmarkovmodel_vertexAttributeToString(VertexAttribute attr) {
    if(attr == VERTEX_ATTR_ID) {
        return "name";
    } else if(attr == VERTEX_ATTR_TYPE) {
        return "type";
    } else {
        return "unknown";
    }
}

static const gchar* _tgenmarkovmodel_edgeAttributeToString(EdgeAttribute attr) {
    if(attr == EDGE_ATTR_TYPE) {
        return "type";
    } else if(attr == EDGE_ATTR_WEIGHT) {
        return "weight";
    } else if(attr == EDGE_ATTR_DISTRIBUTION) {
        return "distribution";
    } else if(attr == EDGE_ATTR_PARAM_LOCATION) {
        return "param_location";
    } else if(attr == EDGE_ATTR_PARAM_SCALE) {
        return "param_scale";
    } else if(attr == EDGE_ATTR_PARAM_RATE) {
        return "param_rate";
    } else if(attr == EDGE_ATTR_PARAM_SHAPE) {
        return "param_shape";
    } else {
        return "unknown";
    }
}

static const gchar* _tgenmarkovmodel_vertexTypeToString(VertexType type) {
    if(type == VERTEX_TYPE_STATE) {
        return "state";
    } else if(type == VERTEX_TYPE_OBSERVATION) {
        return "observation";
    } else {
        return "unknown";
    }
}

static size_t _tgenmarkovmodel_vertexTypeStringLength(VertexType type) {
    return strlen(_tgenmarkovmodel_vertexTypeToString(type));
}

static gboolean _tgenmarkovmodel_vertexTypeIsEqual(const gchar* typeStr, VertexType type) {
    const gchar* constTypeStr = _tgenmarkovmodel_vertexTypeToString(type);
    size_t length = _tgenmarkovmodel_vertexTypeStringLength(type);
    return (g_ascii_strncasecmp(typeStr, constTypeStr, length) == 0) ? TRUE : FALSE;
}

static const gchar* _tgenmarkovmodel_edgeTypeToString(EdgeType type) {
    if(type == EDGE_TYPE_TRANSITION) {
        return "transition";
    } else if(type == EDGE_TYPE_EMISSION) {
        return "emission";
    } else {
        return "unknown";
    }
}

static size_t _tgenmarkovmodel_edgeTypeStringLength(EdgeType type) {
    return strlen(_tgenmarkovmodel_edgeTypeToString(type));
}

static gboolean _tgenmarkovmodel_edgeTypeIsEqual(const gchar* typeStr, EdgeType type) {
    const gchar* constTypeStr = _tgenmarkovmodel_edgeTypeToString(type);
    size_t length = _tgenmarkovmodel_edgeTypeStringLength(type);
    return (g_ascii_strncasecmp(typeStr, constTypeStr, length) == 0) ? TRUE : FALSE;
}

static const gchar* _tgenmarkovmodel_edgeDistToString(EdgeDistribution dist) {
    if(dist == EDGE_DISTRIBUTION_NORMAL) {
        return "normal";
    } else if(dist == EDGE_DISTRIBUTION_LOGNORMAL) {
        return "lognormal";
    } else if(dist == EDGE_DISTRIBUTION_EXPONENTIAL) {
        return "exponential";
    } else if(dist == EDGE_DISTRIBUTION_PARETO) {
        return "pareto";
    } else {
        return "unknown";
    }
}

static size_t _tgenmarkovmodel_edgeDistStringLength(EdgeDistribution dist) {
    return strlen(_tgenmarkovmodel_edgeDistToString(dist));
}

static gboolean _tgenmarkovmodel_edgeDistIsEqual(const gchar* distStr, EdgeDistribution dist) {
    const gchar* constDistStr = _tgenmarkovmodel_edgeDistToString(dist);
    size_t length = _tgenmarkovmodel_edgeDistStringLength(dist);
    return (g_ascii_strncasecmp(distStr, constDistStr, length) == 0) ? TRUE : FALSE;
}

static const gchar* _tgenmarkovmodel_vertexIDToString(VertexID id) {
    if(id == VERTEX_ID_START) {
        return "start";
    } else if(id == VERTEX_ID_TO_SERVER) {
        return "+";
    } else if(id == VERTEX_ID_TO_ORIGIN) {
        return "-";
    } else if(id == VERTEX_ID_END) {
        return "F";
    } else {
        return "?";
    }
}

static size_t _tgenmarkovmodel_vertexIDStringLength(VertexID id) {
    return strlen(_tgenmarkovmodel_vertexIDToString(id));
}

static gboolean _tgenmarkovmodel_vertexIDIsEqual(const gchar* idStr, VertexID id) {
    const gchar* constIDStr = _tgenmarkovmodel_vertexIDToString(id);
    size_t length = _tgenmarkovmodel_vertexIDStringLength(id);
    return (g_ascii_strncasecmp(idStr, constIDStr, length) == 0) ? TRUE : FALSE;
}

static gboolean _tgenmarkovmodel_vertexIDIsEmission(const gchar* idStr) {
    if(_tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_TO_SERVER) ||
            _tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_TO_ORIGIN) ||
            _tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_END)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

/* if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _tgenmarkovmodel_findVertexAttributeString(TGenMarkovModel* mmodel, igraph_integer_t vertexIndex,
        VertexAttribute attr, const gchar** valueOut) {
    TGEN_MMODEL_ASSERT(mmodel);

    if(valueOut != NULL) {
        *valueOut = NULL;
    }

    const gchar* name = _tgenmarkovmodel_vertexAttributeToString(attr);

    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_VERTEX, name)) {
        const gchar* value = igraph_cattribute_VAS(mmodel->graph, name, vertexIndex);
        if(value != NULL && value[0] != '\0') {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _tgenmarkovmodel_findEdgeAttributeDouble(TGenMarkovModel* mmodel, igraph_integer_t edgeIndex,
        EdgeAttribute attr, gdouble* valueOut) {
    TGEN_MMODEL_ASSERT(mmodel);

    const gchar* name = _tgenmarkovmodel_edgeAttributeToString(attr);

    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, name)) {
        gdouble value = (gdouble) igraph_cattribute_EAN(mmodel->graph, name, edgeIndex);
        if(isnan(value) == 0) {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _tgenmarkovmodel_findEdgeAttributeString(TGenMarkovModel* mmodel, igraph_integer_t edgeIndex,
        EdgeAttribute attr, const gchar** valueOut) {
    TGEN_MMODEL_ASSERT(mmodel);

    if(valueOut != NULL) {
        *valueOut = NULL;
    }

    const gchar* name = _tgenmarkovmodel_edgeAttributeToString(attr);

    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, name)) {
        const gchar* value = igraph_cattribute_EAS(mmodel->graph, name, edgeIndex);
        if(value != NULL && value[0] != '\0') {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static gboolean _tgenmarkovmodel_checkVertexAttributes(TGenMarkovModel* mmodel, igraph_integer_t vertexIndex) {
    TGEN_MMODEL_ASSERT(mmodel);
    g_assert(mmodel->graph);

    gboolean isSuccess = TRUE;
    GString* message = g_string_new(NULL);
    g_string_printf(message, "found vertex %li", (glong)vertexIndex);

    /* keep a copy of the id once we get it to make the following message more understandable */
    gchar* idStr = NULL;

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* idKey = _tgenmarkovmodel_vertexAttributeToString(VERTEX_ATTR_ID);
    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_VERTEX, idKey)) {
        const gchar* vidStr;
        if(_tgenmarkovmodel_findVertexAttributeString(mmodel, vertexIndex, VERTEX_ATTR_ID, &vidStr)) {
            g_string_append_printf(message, " %s='%s'", idKey, vidStr);
            idStr = g_strdup(vidStr);
        } else {
            tgen_warning("required attribute '%s' on vertex %li is NULL", idKey, (glong)vertexIndex);
            isSuccess = FALSE;
            idStr = g_strdup("NULL");
        }
    } else {
        tgen_warning("required attribute '%s' on vertex %li is missing", idKey, (glong)vertexIndex);
        isSuccess = FALSE;
        idStr = g_strdup("MISSING");
    }

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* typeKey = _tgenmarkovmodel_vertexAttributeToString(VERTEX_ATTR_TYPE);
    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_VERTEX, typeKey)) {
        const gchar* typeStr;
        if(_tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_START)) {
            /* start vertex doesnt need any attributes */
        } else if(_tgenmarkovmodel_findVertexAttributeString(mmodel, vertexIndex, VERTEX_ATTR_TYPE, &typeStr)) {
            g_string_append_printf(message, " %s='%s'", typeKey, typeStr);

            if(_tgenmarkovmodel_vertexTypeIsEqual(typeStr, VERTEX_TYPE_STATE)) {
                /* pass, nothing to do for now */
            } else if(_tgenmarkovmodel_vertexTypeIsEqual(typeStr, VERTEX_TYPE_OBSERVATION)) {
                if(!_tgenmarkovmodel_vertexIDIsEmission(idStr)) {
                    tgen_warning("'$s' type on vertex %li must be one of '%s', '%s', or '%s', "
                            "but you gave %s='%s'",
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_TYPE_OBSERVATION),
                            (glong)vertexIndex,
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_ID_TO_SERVER),
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_ID_TO_ORIGIN),
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_ID_END),
                            idKey, idStr);
                    isSuccess = FALSE;
                }
            } else {
                tgen_warning("required attribute '%s' value '%s' on vertex %li is invalid, "
                        "need '%s' or '%s'",
                        typeKey, typeStr, (glong)vertexIndex,
                        _tgenmarkovmodel_vertexTypeToString(VERTEX_TYPE_STATE),
                        _tgenmarkovmodel_vertexTypeToString(VERTEX_TYPE_OBSERVATION));
                isSuccess = FALSE;
            }
        } else {
            tgen_warning("required attribute '%s' on vertex %li is NULL", typeKey, (glong)vertexIndex);
            isSuccess = FALSE;
        }
    } else {
        tgen_warning("required attribute '%s' on vertex %li is missing", typeKey, (glong)vertexIndex);
        isSuccess = FALSE;
    }

    tgen_debug("%s", message->str);

    g_string_free(message, TRUE);
    g_free(idStr);

    return isSuccess;
}

static gboolean _tgenmarkovmodel_validateVertices(TGenMarkovModel* mmodel, igraph_integer_t* startVertexID) {
    TGEN_MMODEL_ASSERT(mmodel);
    g_assert(mmodel->graph);

    gboolean isSuccess = TRUE;
    gboolean foundStart = FALSE;

    igraph_vit_t vertexIterator;
    gint result = igraph_vit_create(mmodel->graph, igraph_vss_all(), &vertexIterator);

    if (result != IGRAPH_SUCCESS) {
        isSuccess = FALSE;
        return isSuccess;
    }

    while (!IGRAPH_VIT_END(vertexIterator)) {
        igraph_integer_t vertexIndex = IGRAPH_VIT_GET(vertexIterator);

        isSuccess = _tgenmarkovmodel_checkVertexAttributes(mmodel, vertexIndex);

        if(!isSuccess) {
            break;
        }

        const gchar* idStr = VAS(mmodel->graph,
                _tgenmarkovmodel_vertexAttributeToString(VERTEX_ATTR_ID), vertexIndex);
        if (_tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_START)) {
            /* found the start vertex */
            foundStart = TRUE;

            if(startVertexID) {
                *startVertexID = vertexIndex;
            }
        }

        IGRAPH_VIT_NEXT(vertexIterator);
    }

    if(!foundStart) {
        tgen_warning("unable to find start id in markov model graph");
    }

    igraph_vit_destroy(&vertexIterator);
    return isSuccess && foundStart;
}

gboolean _tgenmarkovmodel_checkEdgeAttributeParamExists(TGenMarkovModel* mmodel,
        igraph_integer_t edgeIndex, EdgeAttribute paramAttr, GString* message,
        const gchar* distStr, const gchar* fromIDStr, const gchar* toIDStr,
        const gchar** keyOut, gdouble* valueOut) {
    const gchar* key = _tgenmarkovmodel_edgeAttributeToString(paramAttr);
    gdouble value;
    if (igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, key)
            && _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, paramAttr, &value)) {
        if(keyOut) {
            *keyOut = key;
        }
        if(valueOut) {
            *valueOut = value;
        }
        return TRUE;
    } else {
        tgen_warning("required attribute '%s' for distribution '%s' "
                "on edge %li (from '%s' to '%s') is missing or NAN", key, distStr,
                (glong )edgeIndex, fromIDStr, toIDStr);
        return FALSE;
    }
}

gboolean _tgenmarkovmodel_checkEdgeAttributeParamNonNegative(TGenMarkovModel* mmodel,
        igraph_integer_t edgeIndex, GString* message,
        const gchar* distStr, const gchar* fromIDStr, const gchar* toIDStr,
        const gchar* key, gdouble value) {
    if (value >= 0.0f) {
        g_string_append_printf(message, " %s='%f'", key, value);
        return TRUE;
    } else {
        /* its an error if they gave a value that is incorrect */
        tgen_warning("required attribute '%s' for distribution '%s' "
                "on edge %li (from '%s' to '%s') must be non-negative", key, distStr,
                (glong )edgeIndex, fromIDStr, toIDStr);
        return FALSE;
    }
}

gboolean _tgenmarkovmodel_checkEdgeAttributeParamPositive(TGenMarkovModel* mmodel,
        igraph_integer_t edgeIndex, GString* message,
        const gchar* distStr, const gchar* fromIDStr, const gchar* toIDStr,
        const gchar* key, gdouble value) {
    if (value > 0.0f) {
        g_string_append_printf(message, " %s='%f'", key, value);
        return TRUE;
    } else {
        /* its an error if they gave a value that is incorrect */
        tgen_warning("required attribute '%s' for distribution '%s' "
                "on edge %li (from '%s' to '%s') must be positive", key, distStr,
                (glong )edgeIndex, fromIDStr, toIDStr);
        return FALSE;
    }
}

gboolean _tgenmarkovmodel_checkEdgeAttributeLocation(TGenMarkovModel* mmodel,
        igraph_integer_t edgeIndex, GString* message, const gchar* distStr,
        const gchar* fromIDStr, const gchar* toIDStr) {
    const gchar* locKey = NULL;
    gdouble locValue = 0;

    if(_tgenmarkovmodel_checkEdgeAttributeParamExists(mmodel, edgeIndex, EDGE_ATTR_PARAM_LOCATION,
            message, distStr, fromIDStr, toIDStr, &locKey, &locValue)) {
        if(_tgenmarkovmodel_checkEdgeAttributeParamNonNegative(mmodel, edgeIndex, message,
                distStr, fromIDStr, toIDStr, locKey, locValue)) {
            return TRUE;
        }
    }

    return FALSE;
}

gboolean _tgenmarkovmodel_checkEdgeAttributeScale(TGenMarkovModel* mmodel,
        igraph_integer_t edgeIndex, GString* message, const gchar* distStr,
        const gchar* fromIDStr, const gchar* toIDStr) {
    const gchar* scaleKey = NULL;
    gdouble scaleValue = 0;

    if(_tgenmarkovmodel_checkEdgeAttributeParamExists(mmodel, edgeIndex, EDGE_ATTR_PARAM_SCALE,
            message, distStr, fromIDStr, toIDStr, &scaleKey, &scaleValue)) {
        if(_tgenmarkovmodel_checkEdgeAttributeParamNonNegative(mmodel, edgeIndex, message,
                distStr, fromIDStr, toIDStr, scaleKey, scaleValue)) {
            return TRUE;
        }
    }

    return FALSE;
}

gboolean _tgenmarkovmodel_checkEdgeAttributeRate(TGenMarkovModel* mmodel,
        igraph_integer_t edgeIndex, GString* message, const gchar* distStr,
        const gchar* fromIDStr, const gchar* toIDStr) {
    const gchar* rateKey = NULL;
    gdouble rateValue = 0;

    if(_tgenmarkovmodel_checkEdgeAttributeParamExists(mmodel, edgeIndex, EDGE_ATTR_PARAM_RATE,
            message, distStr, fromIDStr, toIDStr, &rateKey, &rateValue)) {
        if(_tgenmarkovmodel_checkEdgeAttributeParamPositive(mmodel, edgeIndex, message,
                distStr, fromIDStr, toIDStr, rateKey, rateValue)) {
            return TRUE;
        }
    }

    return FALSE;
}

gboolean _tgenmarkovmodel_checkEdgeAttributeShape(TGenMarkovModel* mmodel,
        igraph_integer_t edgeIndex, GString* message, const gchar* distStr,
        const gchar* fromIDStr, const gchar* toIDStr) {
    const gchar* shapeKey = NULL;
    gdouble shapeValue = 0;

    if(_tgenmarkovmodel_checkEdgeAttributeParamExists(mmodel, edgeIndex, EDGE_ATTR_PARAM_SHAPE,
            message, distStr, fromIDStr, toIDStr, &shapeKey, &shapeValue)) {
        if(_tgenmarkovmodel_checkEdgeAttributeParamPositive(mmodel, edgeIndex, message,
                distStr, fromIDStr, toIDStr, shapeKey, shapeValue)) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean _tgenmarkovmodel_checkEdgeAttributes(TGenMarkovModel* mmodel, igraph_integer_t edgeIndex) {
    TGEN_MMODEL_ASSERT(mmodel);
    g_assert(mmodel->graph);

    igraph_integer_t fromVertexIndex, toVertexIndex;
    gint result = igraph_edge(mmodel->graph, edgeIndex, &fromVertexIndex, &toVertexIndex);

    if(result != IGRAPH_SUCCESS) {
        tgen_warning("igraph_edge return non-success code %i", result);
        return FALSE;
    }

    gboolean found = FALSE;
    const gchar* fromIDStr = NULL;
    const gchar* toIDStr = NULL;

    found = _tgenmarkovmodel_findVertexAttributeString(mmodel, fromVertexIndex, VERTEX_ATTR_ID, &fromIDStr);
    if(!found) {
        tgen_warning("unable to find source vertex for edge %li", (glong)edgeIndex);
        return FALSE;
    }

    found = _tgenmarkovmodel_findVertexAttributeString(mmodel, toVertexIndex, VERTEX_ATTR_ID, &toIDStr);
    if(!found) {
        tgen_warning("unable to find destination vertex for edge %li", (glong)edgeIndex);
        return FALSE;
    }

    gboolean isSuccess = TRUE;
    GString* message = g_string_new(NULL);
    g_string_printf(message, "found edge %li (from %s to %s)", (glong)edgeIndex, fromIDStr, toIDStr);

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* weightKey = _tgenmarkovmodel_edgeAttributeToString(EDGE_ATTR_WEIGHT);
    gdouble weightValue;
    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, weightKey) &&
            _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_WEIGHT, &weightValue)) {
        if(weightValue >= 0.0f) {
            g_string_append_printf(message, " %s='%f'", weightKey, weightValue);
        } else {
            /* its an error if they gave a value that is incorrect */
            tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') must be non-negative",
                    weightKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    } else {
        tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing or NAN",
                weightKey, (glong)edgeIndex, fromIDStr, toIDStr);
        isSuccess = FALSE;
    }

    gboolean isValidEmission = FALSE;

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* typeKey = _tgenmarkovmodel_edgeAttributeToString(EDGE_ATTR_TYPE);
    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, typeKey)) {
        const gchar* typeStr;
        if(_tgenmarkovmodel_findEdgeAttributeString(mmodel, edgeIndex, EDGE_ATTR_TYPE, &typeStr)) {
            g_string_append_printf(message, " %s='%s'", typeKey, typeStr);

            if(_tgenmarkovmodel_edgeTypeIsEqual(typeStr, EDGE_TYPE_TRANSITION)) {
                if(_tgenmarkovmodel_vertexIDIsEmission(fromIDStr)) {
                    /* TODO, we could lookup the vertex and check its type attribute */
                    tgen_warning("id of source vertex on edge %li (from '%s' to '%s') must not be an emission type vertex",
                            (glong)edgeIndex, fromIDStr, toIDStr);
                    isSuccess = FALSE;
                }

                if(_tgenmarkovmodel_vertexIDIsEmission(toIDStr)) {
                    /* TODO, we could lookup the vertex and check its type attribute */
                    tgen_warning("id of destination vertex on edge %li (from '%s' to '%s') must not be an emission type vertex",
                            (glong)edgeIndex, fromIDStr, toIDStr);
                    isSuccess = FALSE;
                }
            } else if(_tgenmarkovmodel_edgeTypeIsEqual(typeStr, EDGE_TYPE_EMISSION)) {
                isValidEmission = TRUE;

                if(_tgenmarkovmodel_vertexIDIsEmission(fromIDStr)) {
                    /* TODO, we could lookup the vertex and check its type attribute */
                    tgen_warning("id of source vertex on edge %li (from '%s' to '%s') must not be an emission type vertex",
                            (glong)edgeIndex, fromIDStr, toIDStr);
                    isSuccess = FALSE;
                    isValidEmission = FALSE;
                }

                if(!_tgenmarkovmodel_vertexIDIsEmission(toIDStr)) {
                    /* TODO, we could lookup the vertex and check its type attribute */
                    tgen_warning("id of destination vertex on edge %li (from '%s' to '%s') must be an emission type vertex",
                            (glong)edgeIndex, fromIDStr, toIDStr);
                    isSuccess = FALSE;
                    isValidEmission = FALSE;
                }
            } else {
                tgen_warning("required attribute '%s' value '%s' on edge %li (from '%s' to '%s') is invalid, "
                        "need '%s' or '%s'", typeKey, typeStr,
                        (glong)edgeIndex, fromIDStr, toIDStr,
                        _tgenmarkovmodel_edgeTypeToString(EDGE_TYPE_TRANSITION),
                        _tgenmarkovmodel_edgeTypeToString(EDGE_TYPE_EMISSION));
                isSuccess = FALSE;
            }
        } else {
            tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is NULL",
                    typeKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    } else {
        tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing",
                typeKey, (glong)edgeIndex, fromIDStr, toIDStr);
        isSuccess = FALSE;
    }

    if(isValidEmission) {
        /* this attribute is required, so it is an error if it doesn't exist */
        const gchar* distKey = _tgenmarkovmodel_edgeAttributeToString(EDGE_ATTR_DISTRIBUTION);
        if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, distKey)) {
            const gchar* distStr;
            if(_tgenmarkovmodel_findEdgeAttributeString(mmodel, edgeIndex, EDGE_ATTR_DISTRIBUTION, &distStr)) {
                g_string_append_printf(message, " %s='%s'", distKey, distStr);

                if(_tgenmarkovmodel_edgeDistIsEqual(distStr, EDGE_DISTRIBUTION_NORMAL) ||
                        _tgenmarkovmodel_edgeDistIsEqual(distStr, EDGE_DISTRIBUTION_LOGNORMAL)) {
                    /* normal needs both location(mu) and scale(sigma) */
                    if(!_tgenmarkovmodel_checkEdgeAttributeLocation(mmodel, edgeIndex,
                            message, distStr, fromIDStr, toIDStr)) {
                        isSuccess = FALSE;
                    }
                    if(!_tgenmarkovmodel_checkEdgeAttributeScale(mmodel, edgeIndex,
                            message, distStr, fromIDStr, toIDStr)) {
                        isSuccess = FALSE;
                    }
                } else if(_tgenmarkovmodel_edgeDistIsEqual(distStr, EDGE_DISTRIBUTION_EXPONENTIAL)) {
                    /* exponential requires rate(lambda) */
                    if(!_tgenmarkovmodel_checkEdgeAttributeRate(mmodel, edgeIndex,
                            message, distStr, fromIDStr, toIDStr)) {
                        isSuccess = FALSE;
                    }
                } else if(_tgenmarkovmodel_edgeDistIsEqual(distStr, EDGE_DISTRIBUTION_PARETO)) {
                    /* pareto requires scale (x_m) and shape(alpha) */
                    if(!_tgenmarkovmodel_checkEdgeAttributeScale(mmodel, edgeIndex,
                            message, distStr, fromIDStr, toIDStr)) {
                        isSuccess = FALSE;
                    }
                    if(!_tgenmarkovmodel_checkEdgeAttributeShape(mmodel, edgeIndex,
                            message, distStr, fromIDStr, toIDStr)) {
                        isSuccess = FALSE;
                    }
                } else {
                    tgen_warning("required attribute '%s' value '%s' on edge %li (from '%s' to '%s') is invalid, "
                            "need one of '%s', '%s', '%s', or '%s'", distKey, distStr,
                            (glong)edgeIndex, fromIDStr, toIDStr,
                            _tgenmarkovmodel_edgeDistToString(EDGE_DISTRIBUTION_NORMAL),
                            _tgenmarkovmodel_edgeDistToString(EDGE_DISTRIBUTION_LOGNORMAL),
                            _tgenmarkovmodel_edgeDistToString(EDGE_DISTRIBUTION_EXPONENTIAL),
                            _tgenmarkovmodel_edgeDistToString(EDGE_DISTRIBUTION_PARETO));
                    isSuccess = FALSE;
                }
            } else {
                tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is NULL",
                        distKey, (glong)edgeIndex, fromIDStr, toIDStr);
                isSuccess = FALSE;
            }
        } else {
            tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing",
                    distKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    }

    tgen_debug("%s", message->str);

    g_string_free(message, TRUE);

    return isSuccess;
}

static gboolean _tgenmarkovmodel_validateEdges(TGenMarkovModel* mmodel) {
    TGEN_MMODEL_ASSERT(mmodel);
    g_assert(mmodel->graph);

    gboolean isSuccess = TRUE;

    /* we will iterate through the edges */
    igraph_eit_t edgeIterator;
    gint result = igraph_eit_create(mmodel->graph, igraph_ess_all(IGRAPH_EDGEORDER_ID), &edgeIterator);

    if(result != IGRAPH_SUCCESS) {
        tgen_warning("igraph_eit_create return non-success code %i", result);
        return FALSE;
    }

    /* count the edges as we iterate */
    while (!IGRAPH_EIT_END(edgeIterator)) {
        igraph_integer_t edgeIndex = IGRAPH_EIT_GET(edgeIterator);

        /* call the hook function for each edge */
        isSuccess = _tgenmarkovmodel_checkEdgeAttributes(mmodel, edgeIndex);
        if(!isSuccess) {
            break;
        }

        IGRAPH_EIT_NEXT(edgeIterator);
    }

    igraph_eit_destroy(&edgeIterator);

    return isSuccess;
}

static igraph_t* _tgenmarkovmodel_loadGraph(FILE* graphFileStream, const gchar* graphName) {
    tgen_debug("Computing size of markov model graph '%s'", graphName);

    /* try to get the file size by first seeking to the end */
    gint result = fseek(graphFileStream, 0, SEEK_END);
    if(result < 0) {
        tgen_warning("Could not retrieve file size for graph name '%s', "
                "fseek() error %i: %s", graphName, errno, g_strerror(errno));
        return NULL;
    }

    /* now check the position */
    glong graphFileSize = (gsize)ftell(graphFileStream);
    if(graphFileSize < 0) {
        tgen_warning("Could not retrieve file size for graph name '%s', "
                "ftell() error %i: %s", graphName, errno, g_strerror(errno));
        return NULL;
    }

    /* rewind the file the the start so we can read the contents */
    rewind(graphFileStream);

    igraph_t* graph = g_new0(igraph_t, 1);

    /* make sure we use the correct attribute handler */
    igraph_i_set_attribute_table(&igraph_cattribute_table);

    result = igraph_read_graph_graphml(graph, graphFileStream, 0);

    if (result != IGRAPH_SUCCESS) {
        if(result == IGRAPH_PARSEERROR) {
            tgen_warning("IGraph reported that there was either a problem reading "
                    "the markov model graph name '%s', or that the file "
                    "was syntactically incorrect.", graphName);
        } else if(result == IGRAPH_UNIMPLEMENTED) {
            tgen_warning("We are unable to read the markov model graph name '%s'"
                    "because IGraph was not compiled with support for graphml.",
                    graphName);
        }

        tgen_warning("Loading the markov model name '%s' failed.", graphName);
        g_free(graph);
        return NULL;
    }

    tgen_info("Successfully read and parsed markov model graph name '%s' "
            "of size %li", graphName, graphFileSize);

    return graph;
}

static void _tgenmarkovmodel_free(TGenMarkovModel* mmodel) {
    TGEN_MMODEL_ASSERT(mmodel);
    g_assert(mmodel->refcount == 0);

    if(mmodel->graph) {
        igraph_destroy(mmodel->graph);
        g_free(mmodel->graph);
        mmodel->graph = NULL;
    }

    if(mmodel->prng) {
        g_rand_free(mmodel->prng);
    }

    if(mmodel->name) {
        g_free(mmodel->name);
    }

    mmodel->magic = 0;
    g_free(mmodel);
}

void tgenmarkovmodel_ref(TGenMarkovModel* mmodel) {
    TGEN_MMODEL_ASSERT(mmodel);
    mmodel->refcount++;
}

void tgenmarkovmodel_unref(TGenMarkovModel* mmodel) {
    TGEN_MMODEL_ASSERT(mmodel);
    if (--(mmodel->refcount) == 0) {
        _tgenmarkovmodel_free(mmodel);
    }
}

static TGenMarkovModel* _tgenmarkovmodel_new(igraph_t* graph, const gchar* name, guint32 seed) {
    g_assert(graph);
    g_assert(name);

    TGenMarkovModel* mmodel = g_new0(TGenMarkovModel, 1);
    mmodel->magic = TGEN_MMODEL_MAGIC;
    mmodel->refcount = 1;

    /* create our local prng for this model */
    mmodel->prng = g_rand_new_with_seed(seed);
    mmodel->prngSeed = seed;

    mmodel->graph = graph;
    mmodel->name = g_strdup(name);

    tgen_info("Starting graph validation on markov model name '%s'", name);

    gboolean verticesPassed = _tgenmarkovmodel_validateVertices(mmodel, &(mmodel->startVertexIndex));
    if(verticesPassed) {
        tgen_info("Markov model name '%s' passed vertex validation", name);
    } else {
        tgen_warning("Markov model name '%s' failed vertex validation", name);
    }

    gboolean edgesPassed = _tgenmarkovmodel_validateEdges(mmodel);
    if(edgesPassed) {
        tgen_info("Markov model name '%s' passed edge validation", name);
    } else {
        tgen_warning("Markov model name '%s' failed edge validation", name);
    }

    if(!verticesPassed || !edgesPassed) {
        tgenmarkovmodel_unref(mmodel);
        tgen_info("Failed to create markov model object");
        return NULL;
    }

    mmodel->currentStateVertexIndex = mmodel->startVertexIndex;

    tgen_info("Successfully validated markov model name '%s', "
            "found start vertex at index %i", name, (int)mmodel->startVertexIndex);

    return mmodel;
}

TGenMarkovModel* tgenmarkovmodel_newFromPath(const gchar* name, guint32 seed, const gchar* graphmlFilePath) {
    if(!graphmlFilePath) {
        tgen_warning("We failed to load the markov model graph because the filename was NULL");
        return NULL;
    }

    if(!g_file_test(graphmlFilePath, G_FILE_TEST_EXISTS)) {
        tgen_warning("We failed to load the markov model graph because the "
                "given path '%s' does not exist", graphmlFilePath);
        return NULL;
    }

    if(!g_file_test(graphmlFilePath, G_FILE_TEST_IS_REGULAR)) {
        tgen_warning("We failed to load the markov model graph because the file at the "
                "given path '%s' is not a regular file", graphmlFilePath);
        return NULL;
    }

    tgen_debug("Opening markov model graph file '%s'", graphmlFilePath);

    FILE* graphFileStream = fopen(graphmlFilePath, "r");
    if (!graphFileStream) {
        tgen_warning("Unable to open markov model graph file at "
                "path '%s', fopen returned NULL with errno %i: %s",
                graphmlFilePath, errno, strerror(errno));
        return NULL;
    }

    igraph_t* graph = _tgenmarkovmodel_loadGraph(graphFileStream, name);

    fclose(graphFileStream);

    TGenMarkovModel* mmodel = graph ? _tgenmarkovmodel_new(graph, name, seed) : NULL;
    return mmodel;
}

TGenMarkovModel* tgenmarkovmodel_newFromString(const gchar* name, guint32 seed, const GString* graphmlString) {
    if(!graphmlString) {
        return NULL;
    }

    FILE* memoryStream = fmemopen(graphmlString->str, graphmlString->len, "r");

    if(!memoryStream) {
        tgen_warning("Unable to open new memory stream for reading: error %i: %s",
                errno, g_strerror(errno));
        return NULL;
    }

    igraph_t* graph = _tgenmarkovmodel_loadGraph(memoryStream, name);

    fclose(memoryStream);

    TGenMarkovModel* mmodel = graph ? _tgenmarkovmodel_new(graph, name, seed) : NULL;
    return mmodel;
}

static gboolean _tgenmarkovmodel_chooseEdge(TGenMarkovModel* mmodel, EdgeType type,
        igraph_integer_t fromVertexIndex,
        igraph_integer_t* edgeIndexOut, igraph_integer_t* toVertexIndexOut) {
    TGEN_MMODEL_ASSERT(mmodel);

    int result = 0;
    gboolean isSuccess = FALSE;

    /* first we 'select' the incident edges, that is, those to which vertexIndex is connected */
    igraph_es_t edgeSelector;
    result = igraph_es_incident(&edgeSelector, fromVertexIndex, IGRAPH_OUT);

    if(result != IGRAPH_SUCCESS) {
        tgen_warning("igraph_es_incident return non-success code %i", result);
        return FALSE;
    }

    /* now we set up an iterator on these edges */
    igraph_eit_t edgeIterator;
    result = igraph_eit_create(mmodel->graph, edgeSelector, &edgeIterator);

    if(result != IGRAPH_SUCCESS) {
        tgen_warning("igraph_eit_create return non-success code %i", result);
        igraph_es_destroy(&edgeSelector);
        return FALSE;
    }

    /* TODO
     * Ideally, we should do this initial iteration over the edges to compute the total
     * weight once at the graph load time. If we want to support graphs with weights
     * that don't add to one, we can normalize the weights ourselves at load time and
     * update the edge weight values. That way, in this function we only have to get
     * the random value and iterate one to find the appropriate choice.
     */

    /* iterate over the edges to get the total weight, filtering by edge type */
    gdouble totalWeight = 0.0;
    guint numEdgesTotal = 0;
    guint numEdgesType = 0;

    /* iterate */
    while (!IGRAPH_EIT_END(edgeIterator)) {
        igraph_integer_t edgeIndex = IGRAPH_EIT_GET(edgeIterator);
        numEdgesTotal++;

        const gchar* typeStr;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeString(mmodel, edgeIndex, EDGE_ATTR_TYPE, &typeStr);
        g_assert(isSuccess);

        if(_tgenmarkovmodel_edgeTypeIsEqual(typeStr, type)) {
            numEdgesType++;

            gdouble weightValue = 0;
            isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_WEIGHT, &weightValue);
            g_assert(isSuccess);

            totalWeight += weightValue;
        }

        IGRAPH_EIT_NEXT(edgeIterator);
    }

    tgen_debug("We found a total weight of %f from %u of %u edges that matched type '%s'",
            totalWeight, numEdgesType, numEdgesTotal, _tgenmarkovmodel_edgeTypeToString(type));

    /* select a random weight value */
    gdouble randomValue = g_rand_double_range(mmodel->prng, (gdouble)0.0, totalWeight);

    tgen_debug("Using random value %f from total weight %f", randomValue, totalWeight);

    /* now iterate again to actually select one of the edges */
    igraph_integer_t chosenEdgeIndex = 0;
    gdouble cumulativeWeight = 0;
    gboolean foundEdge = FALSE;

    /* reset the iterator and iterate again */
    IGRAPH_EIT_RESET(edgeIterator);
    while (!IGRAPH_EIT_END(edgeIterator)) {
        igraph_integer_t edgeIndex = IGRAPH_EIT_GET(edgeIterator);

        const gchar* typeStr;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeString(mmodel, edgeIndex, EDGE_ATTR_TYPE, &typeStr);
        g_assert(isSuccess);

        if(_tgenmarkovmodel_edgeTypeIsEqual(typeStr, type)) {
            gdouble weightValue = 0;
            isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_WEIGHT, &weightValue);
            g_assert(isSuccess);

            cumulativeWeight += weightValue;

            if(cumulativeWeight >= randomValue) {
                foundEdge = TRUE;
                chosenEdgeIndex = edgeIndex;
                break;
            }
        }

        IGRAPH_EIT_NEXT(edgeIterator);
    }

    /* clean up */
    igraph_es_destroy(&edgeSelector);
    igraph_eit_destroy(&edgeIterator);

    if(!foundEdge) {
        tgen_warning("Unable to choose random outgoing edge from vertex %i, "
                "%u of %u edges matched edge type '%s'. "
                "Total weight was %f, cumulative weight was %f, and randomValue was %f.",
                (int)fromVertexIndex, numEdgesType, numEdgesTotal,
                _tgenmarkovmodel_edgeTypeToString(type),
                totalWeight, cumulativeWeight, randomValue);
        return FALSE;
    }

    if(edgeIndexOut) {
        *edgeIndexOut = chosenEdgeIndex;
    }

    if(toVertexIndexOut) {
        /* get the other vertex that we chose */
         igraph_integer_t from, to;
         result = igraph_edge(mmodel->graph, chosenEdgeIndex, &from, &to);

         if(result != IGRAPH_SUCCESS) {
             tgen_warning("igraph_edge return non-success code %i", result);
             return FALSE;
         }

         *toVertexIndexOut = to;
    }

    return TRUE;
}

static gboolean _tgenmarkovmodel_chooseTransition(TGenMarkovModel* mmodel,
        igraph_integer_t fromVertexIndex,
        igraph_integer_t* transitionEdgeIndex, igraph_integer_t* transitionStateVertexIndex) {
    TGEN_MMODEL_ASSERT(mmodel);
    return _tgenmarkovmodel_chooseEdge(mmodel, EDGE_TYPE_TRANSITION, fromVertexIndex,
            transitionEdgeIndex, transitionStateVertexIndex);
}

static gboolean _tgenmarkovmodel_chooseEmission(TGenMarkovModel* mmodel,
        igraph_integer_t fromVertexIndex,
        igraph_integer_t* emissionEdgeIndex, igraph_integer_t* emissionObservationVertexIndex) {
    TGEN_MMODEL_ASSERT(mmodel);
    return _tgenmarkovmodel_chooseEdge(mmodel, EDGE_TYPE_EMISSION, fromVertexIndex,
            emissionEdgeIndex, emissionObservationVertexIndex);
}

static gdouble _tgenmarkovmodel_getUniform(TGenMarkovModel* mmodel) {
    gdouble u = 0;
    do {
        u = g_rand_double_range(mmodel->prng, (gdouble)0.0, (gdouble)1.0);
    } while(u <= DBL_MIN);
    return u;
}

static gdouble _tgenmarkovmodel_generateNormalVariate(TGenMarkovModel* mmodel) {
    /* the Box-Muller method */
    gdouble u = _tgenmarkovmodel_getUniform(mmodel);
    gdouble v = _tgenmarkovmodel_getUniform(mmodel);

    /* this gives us 2 normally-distributed values, x and y */
    gdouble two = (gdouble)2;
    gdouble x = sqrt(-two * log(u)) * cos(two * M_PI * v);
    //double y = sqrt(-two * log(u)) * sin(two * M_PI * v);

    return x;
}

static gdouble _tgenmarkovmodel_generateNormalValue(TGenMarkovModel* mmodel,
        gdouble location, gdouble scale) {
    /* location is mu, scale is sigma */
    gdouble x = _tgenmarkovmodel_generateNormalVariate(mmodel);
    return location + (scale * x);
}

static gdouble _tgenmarkovmodel_generateLogNormalValue(TGenMarkovModel* mmodel,
        gdouble location, gdouble scale) {
    /* location is mu, scale is sigma */
    gdouble x = _tgenmarkovmodel_generateNormalValue(mmodel, location, scale);
    return exp(x);
}

static gdouble _tgenmarkovmodel_generateExponentialValue(TGenMarkovModel* mmodel, gdouble rate) {
    /* inverse transform sampling, rate is lambda */
    gdouble uniform = _tgenmarkovmodel_getUniform(mmodel);
    return -log(uniform)/rate;
}

static gdouble _tgenmarkovmodel_generateParetoValue(TGenMarkovModel* mmodel,
        gdouble scale, gdouble shape) {
    /* inverse transform sampling, scale is x_m, shape is alpha */
    gdouble uniform = _tgenmarkovmodel_getUniform(mmodel);
    return scale/pow(uniform, (1/shape));
}

/* returns a generated value in microseconds, rounded to the nearest microsecond */
static guint64 _tgenmarkovmodel_generateDelay(TGenMarkovModel* mmodel,
        igraph_integer_t edgeIndex) {
    TGEN_MMODEL_ASSERT(mmodel);

    /* we already validated the attributes, so assert that they exist here */
    gboolean isSuccess = FALSE;

    const gchar* typeStr;
    isSuccess = _tgenmarkovmodel_findEdgeAttributeString(mmodel, edgeIndex, EDGE_ATTR_TYPE, &typeStr);
    g_assert(isSuccess);
    g_assert(_tgenmarkovmodel_edgeTypeIsEqual(typeStr, EDGE_TYPE_EMISSION));

    const gchar* distStr;
    isSuccess = _tgenmarkovmodel_findEdgeAttributeString(mmodel, edgeIndex, EDGE_ATTR_DISTRIBUTION, &distStr);
    g_assert(isSuccess);

    /* now we generate a value following the configured distribution type */
    gdouble generatedValue = 0;

    if(_tgenmarkovmodel_edgeDistIsEqual(distStr, EDGE_DISTRIBUTION_NORMAL)) {
        gdouble location = 0;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_PARAM_LOCATION, &location);
        g_assert(isSuccess);
        g_assert(location >= 0);

        gdouble scale = 0;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_PARAM_SCALE, &scale);
        g_assert(isSuccess);
        g_assert(scale >= 0);

        generatedValue = _tgenmarkovmodel_generateNormalValue(mmodel, location, scale);
    } else if(_tgenmarkovmodel_edgeDistIsEqual(distStr, EDGE_DISTRIBUTION_LOGNORMAL)) {
        gdouble location = 0;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_PARAM_LOCATION, &location);
        g_assert(isSuccess);
        g_assert(location >= 0);

        gdouble scale = 0;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_PARAM_SCALE, &scale);
        g_assert(isSuccess);
        g_assert(scale >= 0);

        generatedValue = _tgenmarkovmodel_generateLogNormalValue(mmodel, location, scale);
    } else if(_tgenmarkovmodel_edgeDistIsEqual(distStr, EDGE_DISTRIBUTION_EXPONENTIAL)) {
        gdouble rate = 0;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_PARAM_RATE, &rate);
        g_assert(isSuccess);
        g_assert(rate > 0);

        generatedValue = _tgenmarkovmodel_generateExponentialValue(mmodel, rate);
    } else if(_tgenmarkovmodel_edgeDistIsEqual(distStr, EDGE_DISTRIBUTION_PARETO)) {
        gdouble scale = 0;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_PARAM_SCALE, &scale);
        g_assert(isSuccess);
        g_assert(scale >= 0);

        gdouble shape = 0;
        isSuccess = _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_PARAM_SHAPE, &shape);
        g_assert(isSuccess);
        g_assert(shape > 0);

        generatedValue = _tgenmarkovmodel_generateParetoValue(mmodel, scale, shape);
    } else {
        g_assert_not_reached();
    }

    if(generatedValue > UINT64_MAX) {
        return (guint64)UINT64_MAX;
    } else if(generatedValue < 0) {
        return (guint64)0;
    } else {
        return (guint64)round(generatedValue);
    }
}

static Observation _tgenmarkovmodel_vertexToObservation(TGenMarkovModel* mmodel,
        igraph_integer_t vertexIndex) {
    TGEN_MMODEL_ASSERT(mmodel);

    /* we already validated the attributes, so assert that they exist here */
    gboolean isSuccess = FALSE;

    const gchar* typeStr;
    isSuccess = _tgenmarkovmodel_findVertexAttributeString(mmodel, vertexIndex, VERTEX_ATTR_TYPE, &typeStr);
    g_assert(isSuccess);
    g_assert(_tgenmarkovmodel_vertexTypeIsEqual(typeStr, VERTEX_TYPE_OBSERVATION));

    const gchar* vidStr;
    isSuccess = _tgenmarkovmodel_findVertexAttributeString(mmodel, vertexIndex, VERTEX_ATTR_ID, &vidStr);
    g_assert(isSuccess);

    if(_tgenmarkovmodel_vertexIDIsEqual(vidStr, VERTEX_ID_TO_ORIGIN)) {
        tgen_debug("Returning OBSERVATION_PACKET_TO_ORIGIN");
        return OBSERVATION_TO_ORIGIN;
    } else if(_tgenmarkovmodel_vertexIDIsEqual(vidStr, VERTEX_ID_TO_SERVER)) {
        tgen_debug("Returning OBSERVATION_PACKET_TO_SERVER");
        return OBSERVATION_TO_SERVER;
    } else {
        mmodel->foundEndState = TRUE;
        tgen_debug("Returning OBSERVATION_END");
        return OBSERVATION_END;
    }
}

/* the returned delay is in microseconds */
Observation tgenmarkovmodel_getNextObservation(TGenMarkovModel* mmodel, guint64* delay) {
    TGEN_MMODEL_ASSERT(mmodel);

    if(mmodel->foundEndState) {
        return OBSERVATION_END;
    }

    tgen_debug("About to choose transition from vertex %li", (glong)mmodel->currentStateVertexIndex);

    /* first choose the next state through a transition edge */
    igraph_integer_t nextStateVertexIndex = 0;
    gboolean isSuccess = _tgenmarkovmodel_chooseTransition(mmodel,
            mmodel->currentStateVertexIndex, NULL, &nextStateVertexIndex);

    if(!isSuccess) {
        const gchar* fromIDStr;
        _tgenmarkovmodel_findVertexAttributeString(mmodel,
                mmodel->currentStateVertexIndex, VERTEX_ATTR_ID, &fromIDStr);

        tgen_warning("Failed to choose a transition edge from state %li (%s)",
                (glong)mmodel->currentStateVertexIndex, fromIDStr);
        tgen_warning("Prematurely returning end observation");

        return OBSERVATION_END;
    }

#ifdef DEBUG
    const gchar* from;
    const gchar* to;
    _tgenmarkovmodel_findVertexAttributeString(mmodel,
            mmodel->currentStateVertexIndex, VERTEX_ATTR_ID, &from);
    _tgenmarkovmodel_findVertexAttributeString(mmodel,
            nextStateVertexIndex, VERTEX_ATTR_ID, &to);
    tgen_debug("Found transition from state vertex %li (%s) to state vertex %li (%s)",
            mmodel->currentStateVertexIndex, from, (glong)nextStateVertexIndex, to);
#endif


    /* update our current state */
    mmodel->currentStateVertexIndex = nextStateVertexIndex;

#ifdef DEBUG
    from = to;
    tgen_debug("About to choose emission from vertex %li (%s)",
            (glong)mmodel->currentStateVertexIndex, from);
#endif

    /* now choose an observation through an emission edge */
    igraph_integer_t emissionEdgeIndex = 0;
    igraph_integer_t emissionObservationVertexIndex = 0;
    isSuccess = _tgenmarkovmodel_chooseEmission(mmodel, mmodel->currentStateVertexIndex,
            &emissionEdgeIndex, &emissionObservationVertexIndex);

    if(!isSuccess) {
        const gchar* fromIDStr;
        _tgenmarkovmodel_findVertexAttributeString(mmodel,
                mmodel->currentStateVertexIndex, VERTEX_ATTR_ID, &fromIDStr);

        tgen_warning("Failed to choose an emission edge from state %li (%s)",
                (glong)mmodel->currentStateVertexIndex, fromIDStr);
        tgen_warning("Prematurely returning end observation");

        return OBSERVATION_END;
    }

#ifdef DEBUG
    _tgenmarkovmodel_findVertexAttributeString(mmodel,
            emissionObservationVertexIndex, VERTEX_ATTR_ID, &to);
    tgen_debug("Found emission on edge %li from state vertex %li (%s) to observation vertex %li (%s)",
            (glong)emissionEdgeIndex, (glong)mmodel->currentStateVertexIndex, from,
            (glong)emissionObservationVertexIndex, to);
#endif

    if(delay) {
        *delay = _tgenmarkovmodel_generateDelay(mmodel, emissionEdgeIndex);
        if(*delay > 60000000){
            *delay = 60000000; // FIXME isn't a 60 second inter-stream delay reasonable!?
        }
    }

    return _tgenmarkovmodel_vertexToObservation(mmodel, emissionObservationVertexIndex);
}

void tgenmarkovmodel_reset(TGenMarkovModel* mmodel) {
    TGEN_MMODEL_ASSERT(mmodel);

    mmodel->foundEndState = FALSE;
    mmodel->currentStateVertexIndex = mmodel->startVertexIndex;

    tgen_debug("Markov model graph name '%s' was reset", mmodel->name);
}

gboolean tgenmarkovmodel_isInEndState(TGenMarkovModel* mmodel) {
    TGEN_MMODEL_ASSERT(mmodel);
    return mmodel->foundEndState;
}

guint32 tgenmarkovmodel_getSeed(TGenMarkovModel* mmodel) {
    TGEN_MMODEL_ASSERT(mmodel);
    return mmodel->prngSeed;
}

/* from the returned path, you could get the filename with g_path_get_basename() */
const gchar* tgenmarkovmodel_getName(TGenMarkovModel* mmodel) {
    TGEN_MMODEL_ASSERT(mmodel);
    return mmodel->name;
}

/* returns a new buffer containing the graph in graphml format. the size of the buffer
 * is returned in outBufferSize. The buffer must be freed by the caller. */
GString* tgenmarkovmodel_toGraphmlString(TGenMarkovModel* mmodel) {
    char* bufferLocation = NULL;
    size_t bufferSize = 0;
    FILE* graphStream = open_memstream(&bufferLocation, &bufferSize);

    if(!graphStream) {
        tgen_warning("Unable to open new memory stream for writing: error %i: %s",
                errno, g_strerror(errno));
        return NULL;
    }

    /* This is a workaround because igraph tries to store vertex ids as attributes.
     * Normally igraph would print the following warning to stderr:
     *   Warning: Could not add vertex ids, there is already an 'id' vertex attribute
     *   in file foreign-graphml.c, line 443
     * The fix is to remove the id *attributes* since we don't use it anyway. It will still
     * store the ids in the vertex id field, but this fix ensures it doesn't *also* store
     * the ids as vertex attributes. */
    igraph_cattribute_remove_v(mmodel->graph, "id");

    int result = igraph_write_graph_graphml(mmodel->graph, graphStream, FALSE);

    if(result != IGRAPH_SUCCESS) {
        tgen_warning("IGraph error when writing graph name '%s'", mmodel->name);
        fclose(graphStream);
        if(bufferLocation) {
            g_free(bufferLocation);
        }
        return NULL;
    }

    result = fclose(graphStream);

    if(result != 0) {
        tgen_warning("Error closing the graph stream with fclose(): error %i: %s",
                errno, g_strerror(errno));
        if(bufferLocation) {
            g_free(bufferLocation);
        }
        return NULL;
    }

    tgen_info("Successfully wrote graph to buffer of size %"G_GSIZE_FORMAT, (gsize)bufferSize);

    GString* graphString = g_string_new(bufferLocation);

    g_assert(graphString);
    g_assert(graphString->len == (gsize)bufferSize);

    return graphString;
}
