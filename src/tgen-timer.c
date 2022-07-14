/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <fcntl.h>
#include <sys/timerfd.h>

#include "tgen.h"

struct _TGenTimer {
    TGenTimer_notifyExpiredFunc notify;
    gpointer data1;
    gpointer data2;
    GDestroyNotify destructData1;
    GDestroyNotify destructData2;

    gint timerD;
    gboolean isPersistent;

    gint64 armedInstantMicros;
    gint64 intervalMicros;

    gint refcount;
    guint magic;
};

static void _tgentimer_disarm(TGenTimer* timer) {
    TGEN_ASSERT(timer);

    if(timer->timerD > 0) {
        struct itimerspec disarm;

        disarm.it_value.tv_sec = 0;
        disarm.it_value.tv_nsec = 0;
        disarm.it_interval.tv_sec = 0;
        disarm.it_interval.tv_nsec = 0;

        timer->armedInstantMicros = 0;
        timer->intervalMicros = 0;
        timerfd_settime(timer->timerD, 0, &disarm, NULL);
    }
}

/* disarms the timer so that its notification function is not called.
 * note that the data passed on tgentimer_new is *not* freed by this
 * function (use tgentimer_unref to free any data). */
void tgentimer_cancel(TGenTimer *timer) {
    TGEN_ASSERT(timer);
    _tgentimer_disarm(timer);
}

/** Sets the timer to go off in the given number of microseconds. If the timer
 * is persistent, then configure it to continue going off at the new interval. */
void tgentimer_setExpireTimeMicros(TGenTimer *timer, guint64 micros) {
    TGEN_ASSERT(timer);

    struct itimerspec arm;
    guint64 seconds = micros / 1000000;
    guint64 nanoseconds = (micros % 1000000) * 1000;

    arm.it_value.tv_sec = seconds;
    arm.it_value.tv_nsec = nanoseconds;
    if (timer->isPersistent) {
        arm.it_interval.tv_sec = seconds;
        arm.it_interval.tv_nsec = nanoseconds;
    } else {
        arm.it_interval.tv_sec = 0;
        arm.it_interval.tv_nsec = 0;
    }
    timer->intervalMicros = micros;
    timer->armedInstantMicros = g_get_monotonic_time();
    gint result = timerfd_settime(timer->timerD, 0, &arm, NULL);
    if (result < 0) {
        tgen_critical("timerfd_settime(): returned %i error %i: %s", result,
                errno, g_strerror(errno));
        return;
    }
}

TGenIOResponse tgentimer_onEvent(TGenTimer* timer, gint descriptor, TGenEvent events) {
    TGEN_ASSERT(timer);

    TGenIOResponse response = {0};

    /* our timerD is readable, so our timer has expired */
    g_assert((events & TGEN_EVENT_READ) && descriptor == timer->timerD);

    /* clear the event from the descriptor */
    guint64 numExpirations = 0;
    gssize result = read(timer->timerD, &numExpirations, sizeof(guint64));

    if(result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* the timer actually wasn't ready to read yet */
            tgen_debug("We thought timer fd %i was ready, but it returned EAGAIN", timer->timerD);
            /* keep waiting for read */
            response.events = TGEN_EVENT_READ;
            return response;
        }
        tgen_error("reading from timer fd %i: %s", timer->timerD, g_strerror(errno));
        abort();
    }
    g_assert(numExpirations > 0);

    gint64 minimumExpectedTime = timer->armedInstantMicros + numExpirations * timer->intervalMicros;
    gint64 now = g_get_monotonic_time();
    gint64 earlyMicros = minimumExpectedTime - now;
    if (earlyMicros > 0) {
      tgen_error("Timer armed at %ld with interval %ld expired %lu times. Time "
                 "should be > %ld but is %ld. early-micros:%ld",
                 timer->armedInstantMicros, timer->intervalMicros,
                 numExpirations, minimumExpectedTime, now, earlyMicros);
      abort();
    }

    /* call the registered notification function */
    gboolean shouldCancel = TRUE;
    if(timer->notify) {
        shouldCancel = timer->notify(timer->data1, timer->data2);
    }

    if(shouldCancel) {
        /* it already expired once, so its only armed if persistent */
        if(timer->isPersistent) {
            _tgentimer_disarm(timer);
        }
        response.events = TGEN_EVENT_DONE;
    } else {
        /* we will only ever read timer expirations and never write */
        response.events = TGEN_EVENT_READ;
    }

    return response;
}

TGenTimer* tgentimer_new(guint64 microseconds, gboolean isPersistent,
        TGenTimer_notifyExpiredFunc notify, gpointer data1, gpointer data2,
        GDestroyNotify destructData1, GDestroyNotify destructData2) {
    /* if they dont want to be notified of timer expirations, there is no point */
    if(!notify) {
        return NULL;
    }

    /* create the timer descriptor */
    int timerD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    if (timerD < 0) {
        tgen_critical("timerfd_create(): returned %i error %i: %s", timerD, errno, g_strerror(errno));
        return NULL;
    }

    /* create the timer info */
    struct itimerspec arm;

    guint64 seconds = microseconds / 1000000;
    guint64 nanoseconds = (microseconds % 1000000) * 1000;

    /* a timer with 0 delay will cause timerfd to disarm, so we use a 1 nano
     * delay instead, in order to execute the event as close to now as possible */
    if(seconds == 0 && nanoseconds == 0) {
        nanoseconds = 1;
    }

    /* set the initial expiration */
    arm.it_value.tv_sec = seconds;
    arm.it_value.tv_nsec = nanoseconds;

    if(isPersistent) {
        /* timer continuously repeats */
        arm.it_interval.tv_sec = seconds;
        arm.it_interval.tv_nsec = nanoseconds;
    } else {
        /* timer never repeats */
        arm.it_interval.tv_sec = 0;
        arm.it_interval.tv_nsec = 0;
    }

    /* arm the timer, flags=0 -> relative time, NULL -> ignore previous setting */
    gint64 armedInstantMicros = g_get_monotonic_time();
    gint result = timerfd_settime(timerD, 0, &arm, NULL);

    if (result < 0) {
        tgen_critical("timerfd_settime(): returned %i error %i: %s", result, errno, g_strerror(errno));
        return NULL;
    }

    /* allocate the new server object and return it */
    TGenTimer* timer = g_new0(TGenTimer, 1);
    timer->magic = TGEN_MAGIC;
    timer->refcount = 1;

    timer->notify = notify;
    timer->data1 = data1;
    timer->data2 = data2;
    timer->destructData1 = destructData1;
    timer->destructData2 = destructData2;
    timer->armedInstantMicros = armedInstantMicros;
    timer->intervalMicros = microseconds;

    timer->timerD = timerD;
    timer->isPersistent = isPersistent;

    return timer;
}

static void _tgentimer_free(TGenTimer* timer) {
    TGEN_ASSERT(timer);
    g_assert(timer->refcount == 0);

    if(timer->timerD > 0) {
        close(timer->timerD);
    }

    if(timer->destructData1 && timer->data1) {
        timer->destructData1(timer->data1);
    }

    if(timer->destructData2 && timer->data2) {
        timer->destructData2(timer->data2);
    }

    timer->magic = 0;
    g_free(timer);
}

void tgentimer_ref(TGenTimer* timer) {
    TGEN_ASSERT(timer);
    timer->refcount++;
}

void tgentimer_unref(TGenTimer* timer) {
    TGEN_ASSERT(timer);
    if(--(timer->refcount) <= 0) {
        _tgentimer_free(timer);
    }
}

gint tgentimer_getDescriptor(TGenTimer* timer) {
    TGEN_ASSERT(timer);
    return timer->timerD;
}
