/*
 * See LICENSE for licensing information
 */

#ifndef SRC_TGEN_STREAM_H_
#define SRC_TGEN_STREAM_H_

typedef struct _TGenStream TGenStream;

typedef void (*TGenStream_notifyCompleteFunc)(gpointer data1, gpointer data2, gboolean wasSuccess);

TGenStream* tgenstream_new(const gchar* idStr, TGenStreamOptions* options,
        TGenMarkovModel* mmodel, TGenTransport* transport,
        TGenStream_notifyCompleteFunc notify,
        gpointer data1, GDestroyNotify destructData1,
        gpointer data2, GDestroyNotify destructData2);

void tgenstream_ref(TGenStream* stream);
void tgenstream_unref(TGenStream* stream);

TGenIOResponse tgenstream_onEvent(TGenStream* stream, gint descriptor, TGenEvent events);
gboolean tgenstream_onCheckTimeout(TGenStream* stream, gint descriptor);

#endif /* SRC_TGEN_STREAM_H_ */
