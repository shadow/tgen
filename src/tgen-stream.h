/*
 * See LICENSE for licensing information
 */

#ifndef SRC_TGEN_STREAM_H_
#define SRC_TGEN_STREAM_H_

typedef struct _TGenStream TGenStream;

typedef struct _NotifyCompleteCallback NotifyCompleteCallback;
struct _NotifyCompleteCallback {
    TGen_notifyFunc func;
    gpointer arg;
    GDestroyNotify argRef;
    GDestroyNotify argUnref;
    TGenActionID actionID;
};

TGenStream* tgenstream_new(const gchar* idStr, TGenStreamOptions* options,
        TGenMarkovModel* mmodel, TGenTransport* transport, NotifyCompleteCallback completeCB);

void tgenstream_ref(TGenStream* stream);
void tgenstream_unref(TGenStream* stream);

TGenIOResponse tgenstream_onEvent(TGenStream* stream, gint descriptor, TGenEvent events);
gboolean tgenstream_onCheckTimeout(TGenStream* stream, gint descriptor);

#endif /* SRC_TGEN_STREAM_H_ */
