/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef TGEN_TIMER_H_
#define TGEN_TIMER_H_

#include <glib.h>

typedef struct _TGenTimer TGenTimer;

/* return TRUE to cancel the timer, FALSE to continue the timer as originally configured */
typedef gboolean (*TGenTimer_notifyExpiredFunc)(gpointer data1, gpointer data2);

TGenTimer* tgentimer_new(guint64 microseconds, gboolean isPersistent,
        TGenTimer_notifyExpiredFunc notify, gpointer data1, gpointer data2,
        GDestroyNotify destructData1, GDestroyNotify destructData2);
void tgentimer_ref(TGenTimer* timer);
void tgentimer_unref(TGenTimer* timer);

TGenIOResponse tgentimer_onEvent(TGenTimer* timer, gint descriptor, TGenEvent events);
gint tgentimer_getDescriptor(TGenTimer* timer);
void tgentimer_setExpireTimeMicros(TGenTimer *timer, guint64 micros);
void tgentimer_cancel(TGenTimer *timer);

#endif /* TGEN_TIMER_H_ */
