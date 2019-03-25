/*
 * See LICENSE for licensing information
 */

#ifndef SRC_TGEN_FLOW_H_
#define SRC_TGEN_FLOW_H_

#include <glib.h>

typedef struct _TGenFlow TGenFlow;

typedef enum _TGenFlowFlags {
    TGEN_FLOW_NONE = 0,
    TGEN_FLOW_STREAM_COMPLETE = 1 << 0,
    TGEN_FLOW_STREAM_SUCCESS = 1 << 1,
    TGEN_FLOW_COMPLETE = 1 << 2,
} TGenFlowFlags;

typedef void (*TGenFlow_notifyCompleteFunc)(gpointer data1, TGenActionID actionID, TGenFlowFlags flags);

TGenFlow* tgenflow_new(TGenMarkovModel* streamModel, TGenStreamOptions* streamOptions,
        TGenActionID actionID, const gchar* actionIDStr, TGenIO* io,
        TGenTransport_notifyBytesFunc onBytes,
        TGenFlow_notifyCompleteFunc onComplete,
        gpointer arg, GDestroyNotify argRef, GDestroyNotify argUnref);

void tgenflow_start(TGenFlow* flow);

#endif /* SRC_TGEN_FLOW_H_ */
