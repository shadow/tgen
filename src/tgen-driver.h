/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_DRIVER_H_
#define TGEN_DRIVER_H_

#include "tgen.h"

/* opaque struct containing trafficgenerator data */
typedef struct _TGenDriver TGenDriver;

TGenDriver* tgendriver_new(TGenGraph* graph);
void tgendriver_ref(TGenDriver* driver);
void tgendriver_unref(TGenDriver* driver);

void tgendriver_activate(TGenDriver* driver);

gboolean tgendriver_hasEnded(TGenDriver* driver);
gint tgendriver_getEpollDescriptor(TGenDriver* driver);

#endif /* TGEN_DRIVER_H_ */
