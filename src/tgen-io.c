/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "tgen.h"

struct _TGenIO {
    gint epollD;

    GHashTable* children;

    gint refcount;
    guint magic;
};

typedef struct _TGenIOChild {
    gint descriptor;
    uint32_t currentEvents;
    TGenTimer* deferWriteTimer;
    gint refcount;
    guint magic;

    TGenIO_notifyEventFunc notify;
    TGenIO_notifyCheckTimeoutFunc checkTimeout;
    gpointer data;
    GDestroyNotify destructData;

    /* We keep a pointer to the io, but we don't hold a ref in order to
     * avoid circular references that would prevent both io and children
     * from getting freed correctly. Instead we assume that the io is
     * ALWAYS in a valid state any time any child code is being run.
     * (The io always frees all children before destroying itself.) */
    TGenIO* io;
} TGenIOChild;

static TGenIOChild* _tgeniochild_new(TGenIO* io, gint descriptor, uint32_t events,
        TGenIO_notifyEventFunc notify, TGenIO_notifyCheckTimeoutFunc checkTimeout,
        gpointer data, GDestroyNotify destructData) {
    TGENIO_ASSERT(io);

    TGenIOChild* child = g_new0(TGenIOChild, 1);

    child->io = io;
    child->descriptor = descriptor;
    child->currentEvents = events;

    child->notify = notify;
    child->checkTimeout = checkTimeout;
    child->data = data;
    child->destructData = destructData;

    child->refcount = 1;
    child->magic = TGENIOCHILD_MAGIC;

    return child;
}

static void _tgeniochild_free(TGenIOChild* child) {
    TGENIOCHILD_ASSERT(child);

    if(child->destructData && child->data) {
        child->destructData(child->data);
    }

    if(child->deferWriteTimer) {
        tgentimer_unref(child->deferWriteTimer);
    }

    memset(child, 0, sizeof(TGenIOChild));
    g_free(child);
}

static void _tgeniochild_ref(TGenIOChild* child) {
    TGENIOCHILD_ASSERT(child);
    child->refcount++;
}

static void _tgeniochild_unref(TGenIOChild* child) {
    TGENIOCHILD_ASSERT(child);

    if(--(child->refcount) <= 0) {
        _tgeniochild_free(child);
    }
}

/* this function is needed because the child and timer hold refs to each other.
 * to ensure it is freed correctly, we need to remove one ref. */
static void _tgeniochild_hashTableDestroy(TGenIOChild* child) {
    TGENIOCHILD_ASSERT(child);

    if(child->deferWriteTimer) {
        /* tell the timer that we don't want it to fire anymore */
        tgentimer_cancel(child->deferWriteTimer);

        /* the child will never use the timer again. */
        tgentimer_unref(child->deferWriteTimer);
        child->deferWriteTimer = NULL;
    }

    _tgeniochild_unref(child);
}

TGenIO* tgenio_new() {
    /* create an epoll descriptor so we can manage events */
    gint epollD = epoll_create(1);
    if (epollD < 0) {
        tgen_critical("epoll_create(): returned %i error %i: %s", epollD, errno, g_strerror(errno));
        return NULL;
    }

    /* allocate the new server object and return it */
    TGenIO* io = g_new0(TGenIO, 1);
    io->magic = TGENIO_MAGIC;
    io->refcount = 1;

    io->children = g_hash_table_new_full(g_direct_hash, g_direct_equal,
            NULL, (GDestroyNotify)_tgeniochild_hashTableDestroy);

    io->epollD = epollD;

    return io;
}

static void _tgenio_free(TGenIO* io) {
    TGENIO_ASSERT(io);
    g_assert(io->refcount == 0);

    if(io->children) {
        g_hash_table_destroy(io->children);
    }

    io->magic = 0;
    g_free(io);
}

void tgenio_ref(TGenIO* io) {
    TGENIO_ASSERT(io);
    io->refcount++;
}

void tgenio_unref(TGenIO* io) {
    TGENIO_ASSERT(io);
    if(--(io->refcount) <= 0) {
        _tgenio_free(io);
    }
}

static void _tgenio_deregister(TGenIO* io, gint descriptor) {
    TGENIO_ASSERT(io);

    /* don't watch events for the descriptor anymore */
    gint result = epoll_ctl(io->epollD, EPOLL_CTL_DEL, descriptor, NULL);
    if(result != 0) {
        tgen_warning("epoll_ctl(): epoll %i descriptor %i returned %i error %i: %s",
                io->epollD, descriptor, result, errno, g_strerror(errno));
    }

    TGenIOChild* child = g_hash_table_lookup(io->children, GINT_TO_POINTER(descriptor));
    if(child && child->deferWriteTimer) {
        gint timerD = tgentimer_getDescriptor(child->deferWriteTimer);

        /* tell the timer that we don't want it to fire anymore */
        tgentimer_cancel(child->deferWriteTimer);

        /* the child will never use the timer again. */
        tgentimer_unref(child->deferWriteTimer);
        child->deferWriteTimer = NULL;

        /* tell the io module to stop paying attention to the timer. after this call
         * if the timer fires (becomes readable) we won't notice. this will call the
         * tgentimer_unref destructor that we passed in tgenio_register.*/
        _tgenio_deregister(io, timerD);
    }

    g_hash_table_remove(io->children, GINT_TO_POINTER(descriptor));

    tgen_debug("Deregistered listener on epoll fd %i for child fd %i", io->epollD, descriptor);
}

gboolean tgenio_register(TGenIO* io, gint descriptor, TGenIO_notifyEventFunc notify,
        TGenIO_notifyCheckTimeoutFunc checkTimeout, gpointer data, GDestroyNotify destructData) {
    TGENIO_ASSERT(io);

    TGenIOChild* oldchild = g_hash_table_lookup(io->children, GINT_TO_POINTER(descriptor));

    if(oldchild) {
        _tgenio_deregister(io, descriptor);
        tgen_warning("IO removed existing child descriptor %i to make room for a new one", descriptor);
    }

    /* start watching */
    uint32_t ev = EPOLLIN|EPOLLOUT;
    struct epoll_event ee = {0};
    ee.events = ev;
    ee.data.fd = descriptor;

    gint result = epoll_ctl(io->epollD, EPOLL_CTL_ADD, descriptor, &ee);

    if (result != 0) {
        tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                io->epollD, descriptor, result, errno, g_strerror(errno));
        return FALSE;
    }

    TGenIOChild* newchild = _tgeniochild_new(io, descriptor, ev, notify, checkTimeout, data, destructData);
    g_hash_table_replace(io->children, GINT_TO_POINTER(newchild->descriptor), newchild);

    tgen_debug("Registered listener on epoll fd %i for child fd %i", io->epollD, descriptor);

    return TRUE;
}

static void _tgenio_syncEpollEvents(TGenIO* io, TGenIOChild* child, uint32_t newEvents) {
    TGENIO_ASSERT(io);
    TGENIOCHILD_ASSERT(child);

    /* only modify the epoll if the events we are watching should change.
     * note that the ready events may only be a subset of the events we are watching.*/
    if(child->currentEvents != newEvents) {
        struct epoll_event ee = {0};
        ee.events = newEvents;
        ee.data.fd = child->descriptor;

        gint result = epoll_ctl(io->epollD, EPOLL_CTL_MOD, child->descriptor, &ee);
        if(result == 0) {
            child->currentEvents = newEvents;
        } else {
            tgen_warning("epoll_ctl(): epoll %i descriptor %i returned %i error %i: %s",
                    io->epollD, child->descriptor, result, errno, g_strerror(errno));
        }
    }
}

static gboolean _tgenio_onDeferTimerExpired(TGenIOChild* child, gpointer none) {
    TGENIOCHILD_ASSERT(child);
    TGenIO* io = child->io;
    TGENIO_ASSERT(io);

    tgen_debug("Defer timer expired on descriptor %i. Asking for write events again.",
            child->descriptor);

    /* If the child was previously deregistered, then the timer would have been cancelled.
     * Since the timer fired and called this function, the child must still exist. */
    TGenIOChild* oldchild = g_hash_table_lookup(io->children, GINT_TO_POINTER(child->descriptor));
    g_assert(oldchild);
    g_assert(child == oldchild);

    /* listen for writes again */
    _tgenio_syncEpollEvents(io, child, (child->currentEvents|EPOLLOUT));

    /* When we created the timer, we said it should not be persistent
     * and so should only expire once. But we still want the timer to be
     * tracked by the IO epoll so we don't have to register it again
     * if we have a new defer time in the future. Return FALSE so that
     * the timer will still fire again next time we set a new defer time. */
    return FALSE;
}

static void _tgenio_setDeferTimer(TGenIO* io, TGenIOChild* child, gint64 microsecondsPause) {
    TGENIO_ASSERT(io);
    TGENIOCHILD_ASSERT(child);
    g_assert(microsecondsPause > 0);

    tgen_debug("Deferring write events on descriptor %i by %"G_GINT64_FORMAT" "
            "microseconds using %s",
            child->descriptor, microsecondsPause,
            child->deferWriteTimer ? "an existing timer" : "a new timer");

    /* if we already have a timer, just update the trigger time */
    if(child->deferWriteTimer) {
        /* it should already be registered */
        gint timerD = tgentimer_getDescriptor(child->deferWriteTimer);
        g_assert(g_hash_table_lookup(io->children, GINT_TO_POINTER(timerD)));

        /* make it expire again after a pause */
        tgentimer_setExpireTimeMicros(child->deferWriteTimer, microsecondsPause);
    } else {
        /* the timer will hold a ref to child, which it will unref when the timer is destroyed */
        _tgeniochild_ref(child);
        /* the timer itself starts with one ref */
        child->deferWriteTimer = tgentimer_new(microsecondsPause, FALSE,
                (TGenTimer_notifyExpiredFunc)_tgenio_onDeferTimerExpired,
                child, NULL, (GDestroyNotify)_tgeniochild_unref, NULL);

        /* Tell the io module to watch the timer so we know when it expires.
         * The io module holds a second reference to the timer.
         * The order here is that the io module will watch the timer and then call
         * tgentimer_onEvent when the timer expires, then the timer will call
         * _tgenio_onDeferTimerExpired and will adjust the timer as appropriate. */
        tgentimer_ref(child->deferWriteTimer);
        /* the ref above will be unreffed when the timer is deregistered. */
        tgenio_register(io, tgentimer_getDescriptor(child->deferWriteTimer),
                (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL,
                child->deferWriteTimer, (GDestroyNotify)tgentimer_unref);
    }
}

static void _tgenio_helper(TGenIO* io, TGenIOChild* child, gboolean in, gboolean out, gboolean done) {
    TGENIO_ASSERT(io);
    TGENIOCHILD_ASSERT(child);

    TGenEvent readyEvents = TGEN_EVENT_NONE;

    /* check if we need read flag */
    if(in) {
        tgen_debug("descriptor %i is readable (EPOLLIN)", child->descriptor);
        readyEvents |= TGEN_EVENT_READ;
    }

    /* check if we need write flag */
    if(out) {
        tgen_debug("descriptor %i is writable (EPOLLOUT)", child->descriptor);
        readyEvents |= TGEN_EVENT_WRITE;
    }

    /* check if we need done flag */
    if(done) {
        tgen_debug("descriptor %i is done (EPOLLERR || EPOLLHUP)", child->descriptor);
        readyEvents |= TGEN_EVENT_DONE;
    }

    /* activate the transfer */
    TGenIOResponse response = child->notify(child->data, child->descriptor, readyEvents);

    /* if done, we can clean up the child now and exit */
    if(done || (response.events & TGEN_EVENT_DONE)) {
        _tgenio_deregister(io, child->descriptor);
        return;
    }

    /* check which events we need to watch */
    guint32 newEvents = 0;
    if(response.events & TGEN_EVENT_READ) {
        /* epoll for reads now */
        newEvents |= EPOLLIN;
    }
    if(response.events & TGEN_EVENT_WRITE_DEFERRED) {
        /* don't epoll for writes until after a pause */
        gint64 nowUSec = g_get_monotonic_time();
        if(response.deferUntilUSec > nowUSec) {
            /* we still need to pause */
            gint64 microsecondsPause = response.deferUntilUSec - nowUSec;
            _tgenio_setDeferTimer(io, child, microsecondsPause);
        } else {
            /* the requested pause already passed, so just turn on writes now */
            newEvents |= EPOLLOUT;
        }
    } else if(response.events & TGEN_EVENT_WRITE) {
        /* epoll for writes now */
        newEvents |= EPOLLOUT;
    }

    /* sync up the events we should watch with the epoll instance */
    _tgenio_syncEpollEvents(io, child, newEvents);
}

gint tgenio_loopOnce(TGenIO* io, gint maxEvents) {
    TGENIO_ASSERT(io);

    /* storage for collecting events from our epoll descriptor */
    struct epoll_event* epevs = g_new(struct epoll_event, maxEvents);

    /* collect all events that are ready */
    gint nfds = epoll_wait(io->epollD, epevs, maxEvents, 0);

    if(nfds < 0) {
        /* if the user paused with ctl-z, we get EINTR and can just try again */
        if(errno == EINTR) {
            return tgenio_loopOnce(io, maxEvents);
        }
        tgen_critical("epoll_wait(): epoll %i returned %i error %i: %s",
                io->epollD, nfds, errno, g_strerror(errno));
        g_free(epevs);

        /* we didn't process any events */
        return 0;
    }

    /* activate correct component for every descriptor that's ready. */
    for (gint i = 0; i < nfds; i++) {
        /* are we readable or writable? */
        gboolean in = (epevs[i].events & EPOLLIN) ? TRUE : FALSE;
        gboolean out = (epevs[i].events & EPOLLOUT) ? TRUE : FALSE;

        /* Error (EPOLLERR) or hangup (EPOLLHUP) occurred. Epoll always listens to these events
         * even though we don't set them, so we must handle them gracefully. */
        gboolean done = ((epevs[i].events & EPOLLERR) || (epevs[i].events & EPOLLHUP)) ? TRUE : FALSE;

        if (!in && !out && !done) {
            tgen_error("Unexpected event: %d", epevs[i].events);
        }
        
        gint eventDescriptor = epevs[i].data.fd;
        TGenIOChild* child = g_hash_table_lookup(io->children, GINT_TO_POINTER(eventDescriptor));

        if(child) {
            _tgenio_helper(io, child, in, out, done);
        } else {
            /* we don't currently have a child for the event descriptor.
             * it may have been deleted by a previous event in this event loop: e.g.,
             * at i=0 a transfer closed and that caused its timer to dereg, then at i=1
             * the timer would have expired but it was just deregged. just log the valid case.
             * make sure we stop paying attention to it. */
            tgen_warning("can't find child for descriptor %i, canceling event now", eventDescriptor);
            _tgenio_deregister(io, eventDescriptor);
        }
    }

    g_free(epevs);
    return nfds;
}

void tgenio_checkTimeouts(TGenIO* io) {
    TGENIO_ASSERT(io);

    /* TODO this was a quick polling approach to checking for timeouts, which
     * could be more efficient if replaced with an asynchronous notify design. */
    GList* items = g_hash_table_get_values(io->children);
    GList* item = g_list_first(items);

    /* deregistering children modifies the hash table and invalidates the values list.
     * we cannot modify the hash table while iterating. */
    GQueue* descriptorsToDeregister = NULL;

    while(item) {
        TGenIOChild* child = item->data;
        if(child && child->checkTimeout) {
            /* this calls tgentransfer_onCheckTimeout to check and handle if a timeout is present */
            gboolean hasTimeout = child->checkTimeout(child->data, child->descriptor);
            if(hasTimeout) {
                /* only create the queue on the fly if needed */
                if(descriptorsToDeregister == NULL) {
                    descriptorsToDeregister = g_queue_new();
                }
                g_queue_push_head(descriptorsToDeregister, GINT_TO_POINTER(child->descriptor));
            }
        }
        item = g_list_next(item);
    }

    if(items != NULL) {
        g_list_free(items);
    }

    /* now free any children who timed out */
    if(descriptorsToDeregister) {
        while(!g_queue_is_empty(descriptorsToDeregister)) {
            gint descriptor = GPOINTER_TO_INT(g_queue_pop_head(descriptorsToDeregister));
            _tgenio_deregister(io, descriptor);
        }

        g_queue_free(descriptorsToDeregister);
    }
}

gint tgenio_getEpollDescriptor(TGenIO* io) {
    TGENIO_ASSERT(io);
    return io->epollD;
}
