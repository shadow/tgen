/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_TRANSFER_H_
#define TGEN_TRANSFER_H_

typedef struct _TGenTransfer TGenTransfer;

typedef void (*TGenTransfer_notifyCompleteFunc)(gpointer data1, gpointer data2, gboolean wasSuccess);

TGenTransfer* tgentransfer_newActive(TGenMarkovModel* mmodel, const gchar* idStr, gsize count,
        gsize sendSize, gsize recvSize, guint64 timeout, guint64 stallout,
        TGenIO* io, TGenTransport* transport, TGenTransfer_notifyCompleteFunc notify,
        gpointer data1, gpointer data2, GDestroyNotify destructData1, GDestroyNotify destructData2);

TGenTransfer* tgentransfer_newPassive(gsize count, guint64 timeout, guint64 stallout,
        TGenIO* io, TGenTransport* transport, TGenTransfer_notifyCompleteFunc notify,
        gpointer data1, GDestroyNotify destructData1);

void tgentransfer_ref(TGenTransfer* transfer);
void tgentransfer_unref(TGenTransfer* transfer);

TGenEvent tgentransfer_onEvent(TGenTransfer* transfer, gint descriptor, TGenEvent events);
gboolean tgentransfer_onCheckTimeout(TGenTransfer* transfer, gint descriptor);

#endif /* TGEN_TRANSFER_H_ */
