/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_TRANSFER_H_
#define TGEN_TRANSFER_H_

typedef struct _TGenTransfer TGenTransfer;

typedef void (*TGenTransfer_notifyCompleteFunc)(gpointer data1, gpointer data2, gboolean wasSuccess);

TGenTransfer* tgentransfer_new(const gchar* idStr, gsize count, TGenStreamOptions* options,
        TGenMarkovModel* mmodel, TGenIO* io, TGenTransport* transport,
        TGenTransfer_notifyCompleteFunc notify,
        gpointer data1, GDestroyNotify destructData1,
        gpointer data2, GDestroyNotify destructData2);

void tgentransfer_ref(TGenTransfer* transfer);
void tgentransfer_unref(TGenTransfer* transfer);

TGenEvent tgentransfer_onEvent(TGenTransfer* transfer, gint descriptor, TGenEvent events);
gboolean tgentransfer_onCheckTimeout(TGenTransfer* transfer, gint descriptor);

#endif /* TGEN_TRANSFER_H_ */
