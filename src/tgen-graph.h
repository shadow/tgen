/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_GRAPH_H_
#define TGEN_GRAPH_H_

#include <glib.h>

typedef struct _TGenGraph TGenGraph;

typedef enum _TGenActionType {
    TGEN_ACTION_NONE,
    TGEN_ACTION_START,
    TGEN_ACTION_END,
    TGEN_ACTION_PAUSE,
    TGEN_ACTION_STREAM,
    TGEN_ACTION_FLOW,
    TGEN_ACTION_TRAFFIC,
} TGenActionType;

typedef struct _TGenStreamOptions {
    TGenOptionString packetModelPath;
    TGenOptionString packetModelMode;
    TGenOptionUInt32 markovModelSeed;
    TGenOptionPool seedGenerator; /* using pool as a ref-counted container */
    TGenOptionPool peers;
    TGenOptionPeer socksProxy;
    TGenOptionString socksUsername;
    TGenOptionString socksPassword;
    TGenOptionUInt32 socksAuthSeed;
    TGenOptionPool socksAuthGenerator; /* using pool as a ref-counted container */
    TGenOptionUInt64 sendSize;
    TGenOptionUInt64 recvSize;
    TGenOptionUInt64 timeoutNanos;
    TGenOptionUInt64 stalloutNanos;
} TGenStreamOptions;

typedef struct _TGenFlowOptions {
    TGenOptionString streamModelPath;
    TGenStreamOptions streamOpts;
} TGenFlowOptions;

typedef struct _TGenTrafficOptions {
    TGenOptionString flowModelPath;
    TGenFlowOptions flowOpts;
} TGenTrafficOptions;

typedef struct _TGenStartOptions {
    TGenOptionUInt16 serverport;
    TGenOptionUInt64 timeNanos;
    TGenOptionUInt64 heartbeatPeriodNanos;
    TGenOptionLogLevel loglevel;
    TGenTrafficOptions defaultTrafficOpts;
} TGenStartOptions;

typedef struct _TGenPauseOptions {
    TGenOptionPool times;
} TGenPauseOptions;

typedef struct _TGenEndOptions {
    TGenOptionUInt64 timeNanos;
    TGenOptionUInt64 count;
    TGenOptionUInt64 sendSize;
    TGenOptionUInt64 recvSize;
} TGenEndOptions;

TGenGraph* tgengraph_new(gchar* path);
void tgengraph_ref(TGenGraph* g);
void tgengraph_unref(TGenGraph* g);

const gchar* tgengraph_getGraphPath(TGenGraph* g);
gboolean tgengraph_hasEdges(TGenGraph* g);

TGenActionID tgengraph_getStartActionID(TGenGraph* g);
GQueue* tgengraph_getNextActionIDs(TGenGraph* g, TGenActionID actionID);

TGenActionType tgengraph_getActionType(TGenGraph* g, TGenActionID actionID);
const gchar* tgengraph_getActionName(TGenGraph* g, TGenActionID actionID);

gboolean tgengraph_incrementPauseVisited(TGenGraph* g, TGenActionID actionID);

TGenStartOptions* tgengraph_getStartOptions(TGenGraph* g);
TGenPauseOptions* tgengraph_getPauseOptions(TGenGraph* g, TGenActionID actionID);
TGenEndOptions* tgengraph_getEndOptions(TGenGraph* g, TGenActionID actionID);
TGenStreamOptions* tgengraph_getStreamOptions(TGenGraph* g, TGenActionID actionID);
TGenFlowOptions* tgengraph_getFlowOptions(TGenGraph* g, TGenActionID actionID);
TGenTrafficOptions* tgengraph_getTrafficOptions(TGenGraph* g, TGenActionID actionID);

#endif /* TGEN_GRAPH_H_ */
