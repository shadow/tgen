/*
 * See LICENSE for licensing information
 */

#ifndef SRC_TGEN_GENERATOR_H_
#define SRC_TGEN_GENERATOR_H_

#include <glib.h>

typedef struct _TGenGenerator TGenGenerator;

TGenGenerator* tgengenerator_new(TGenTrafficOptions* trafficOptions,
        TGenFlowOptions* flowOptions, TGenStreamOptions* streamOptions,
        TGenActionID actionID, const gchar* actionIDStr, TGenIO* io,
        TGenTransport_notifyBytesFunc onBytes,
        TGen_notifyFunc notifyFunc, gpointer notifyArg,
        GDestroyNotify notifyArgRef, GDestroyNotify notifyArgUnref);

void tgengenerator_start(TGenGenerator* flow);

#endif /* SRC_TGEN_GENERATOR_H_ */
