/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_GENERATOR_H_
#define TGEN_GENERATOR_H_

#include <glib.h>

typedef struct _TGenGenerator TGenGenerator;

TGenGenerator* tgengenerator_new(const gchar* streamModelPath, const gchar* packetModelPath,
        TGenActionID actionID);
void tgengenerator_ref(TGenGenerator* gen);
void tgengenerator_unref(TGenGenerator* gen);

gboolean tgengenerator_generateStream(TGenGenerator* gen,
        gchar** localSchedule, gchar** remoteSchedule, guint64* pauseTimeUSec);

TGenActionID tgengenerator_getModelActionID(TGenGenerator* gen);
void tgengenerator_onTransferCreated(TGenGenerator* gen);
void tgengenerator_onTransferCompleted(TGenGenerator* gen);
gboolean tgengenerator_isDoneGenerating(TGenGenerator* gen);

guint tgengenerator_getNumOutstandingTransfers(TGenGenerator* gen);
guint tgengenerator_getNumStreamsGenerated(TGenGenerator* gen);
guint tgengenerator_getNumPacketsGenerated(TGenGenerator* gen);

#endif /* TGEN_GENERATOR_H_ */
