/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_GRAPH_H_
#define TGEN_GRAPH_H_

#include <glib.h>

typedef struct _TGenGraph TGenGraph;

typedef igraph_integer_t TGenActionID;

typedef enum _TGenActionType {
    TGEN_ACTION_START,
    TGEN_ACTION_END,
    TGEN_ACTION_PAUSE,
    TGEN_ACTION_STREAM,
    TGEN_ACTION_FLOW,
} TGenActionType;

typedef struct _TGenStreamOptions {
    TGenOptionString packetModelPath;
    TGenOptionUInt32 packetModelSeed;
    TGenOptionPool peers;
    TGenOptionPeer socksProxy;
    TGenOptionString socksUsername;
    TGenOptionString socksPassword;
    TGenOptionUInt64 sendSize;
    TGenOptionUInt64 recvSize;
    TGenOptionUInt64 timeoutNanos;
    TGenOptionUInt64 stalloutNanos;
} TGenStreamOptions;

typedef struct _TGenFlowOptions {
    TGenOptionString streamModelPath;
    TGenOptionUInt32 streamModelSeed;
    TGenStreamOptions streamOpts;
} TGenFlowOptions;

typedef struct _TGenStartOptions {
    TGenOptionUInt16 serverport;
    TGenOptionUInt64 timeNanos;
    TGenOptionUInt64 heartbeatPeriodNanos;
    TGenOptionLogLevel loglevel;
    TGenStreamOptions defaultStreamOpts;
} TGenStartOptions;

typedef struct _TGenPauseOptions {
    TGenOptionPool times;
} TGenPauseOptions;

typedef struct _TGenEndOptions {
    TGenOptionUInt64 timeNanos;
    TGenOptionUInt32 count;
    TGenOptionUInt64 sendSize;
    TGenOptionUInt64 recvSize;
} TGenEndOptions;

TGenGraph* tgengraph_new(gchar* path);
void tgengraph_ref(TGenGraph* g);
void tgengraph_unref(TGenGraph* g);

TGenActionID tgengraph_getStartActionID(TGenGraph* g);
GQueue* tgengraph_getNextActionIDs(TGenGraph* g, TGenActionID actionID);
gboolean tgengraph_hasEdges(TGenGraph* g);
const gchar* tgengraph_getActionIDStr(TGenGraph* g, TGenActionID actionID);
const gchar* tgengraph_getGraphPath(TGenGraph* g);


GLogLevelFlags tgengraph_getLogLevel(TGenGraph* g);

#endif /* TGEN_GRAPH_H_ */
