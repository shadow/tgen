/*
 * See LICENSE for licensing information
 */

#ifndef SRC_TGEN_FLOW_H_
#define SRC_TGEN_FLOW_H_

#include <glib.h>

typedef struct _TGenFlow TGenFlow;

TGenFlow* tgenflow_new(TGenFlowOptions* flowOptions, TGenStreamOptions* streamOptions,
        TGenActionID actionID, const gchar* actionIDStr, TGenIO* io,
        TGenTransport_notifyBytesFunc onBytes,
        TGen_notifyFunc onComplete,
        gpointer arg, GDestroyNotify argRef, GDestroyNotify argUnref);

void tgenflow_start(TGenFlow* flow);

#endif /* SRC_TGEN_FLOW_H_ */
