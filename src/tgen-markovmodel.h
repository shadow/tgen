/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_MARKOVMODEL_H_
#define TGEN_MARKOVMODEL_H_

#include <glib.h>

/* this is how many bytes we send for each packet type observation */
#define TGEN_MMODEL_PACKET_DATA_SIZE 1434
/* and packets sent within this many microseconds will be sent
 * at the same time for efficiency reasons */
#define TGEN_MMODEL_MICROS_AT_ONCE 1000

typedef enum _Observation Observation;
enum _Observation {
    OBSERVATION_PACKET_TO_SERVER,
    OBSERVATION_PACKET_TO_ORIGIN,
    OBSERVATION_STREAM,
    OBSERVATION_END,
};

typedef struct _TGenMarkovModel TGenMarkovModel;

TGenMarkovModel* tgenmarkovmodel_new(const gchar* modelPath);
TGenMarkovModel* tgenmarkovmodel_newWithSeed(const gchar* modelPath, guint32 seed);
void tgenmarkovmodel_ref(TGenMarkovModel* mmodel);
void tgenmarkovmodel_unref(TGenMarkovModel* mmodel);

Observation tgenmarkovmodel_getNextObservation(TGenMarkovModel* mmodel, guint64* delay);
void tgenmarkovmodel_reset(TGenMarkovModel* mmodel);

guint32 tgenmarkovmodel_getSeed(TGenMarkovModel* mmodel);
const gchar* tgenmarkovmodel_getGraphmlFilePath(TGenMarkovModel* mmodel);
gsize tgenmarkovmodel_getGraphmlFileSize(TGenMarkovModel* mmodel);

#endif /* TGEN_MARKOVMODEL_H_ */
