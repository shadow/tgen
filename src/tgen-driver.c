/*
 * See LICENSE for licensing information
 */

#include <string.h>
#include <arpa/inet.h>
#include <glib/gstdio.h>

#include "tgen.h"

#define MAX_EVENTS_PER_IO_LOOP 100

struct _TGenDriver {
    /* our graphml dependency graph */
    TGenGraph* actionGraph;

    /* the starting action parsed from the action graph */
    TGenActionID startActionID;
    TGenStartOptions* startOptions;
    gint64 startTimeMicros;

    /* TRUE iff a condition in any endAction event has been reached */
    gboolean clientHasEnded;
    /* the server only ends if an end time is specified */
    gboolean serverHasEnded;

    /* our I/O event manager. this holds refs to all of the transfers
     * and notifies them of I/O events on the underlying transports */
    TGenIO* io;

    /* each transfer has a unique id */
    gsize globalTransferCounter;

    /* traffic statistics */
    guint64 heartbeatTransfersCompleted;
    guint64 heartbeatTransferErrors;
    gsize heartbeatBytesRead;
    gsize heartbeatBytesWritten;
    guint64 totalTransfersCompleted;
    guint64 totalTransferErrors;
    gsize totalBytesRead;
    gsize totalBytesWritten;

    gint refcount;
    guint magic;
};

/* forward declaration */
static gboolean _tgendriver_onStartClientTimerExpired(TGenDriver* driver, gpointer nullData);
static gboolean _tgendriver_onPauseTimerExpired(TGenDriver* driver, gpointer actionIDPtr);
static gboolean _tgendriver_onGeneratorTimerExpired(TGenDriver* driver, TGenGenerator* generator);
static void _tgendriver_continueNextActions(TGenDriver* driver, TGenActionID actionID);

static void _tgendriver_onTransferComplete(TGenDriver* driver, gpointer actionIDPtr, gboolean wasSuccess) {
    TGEN_ASSERT(driver);

    TGenActionID actionID = GPOINTER_TO_INT(actionIDPtr);

    /* our transfer finished, close the socket */
    if(wasSuccess) {
        driver->heartbeatTransfersCompleted++;
        driver->totalTransfersCompleted++;
    } else {
        driver->heartbeatTransferErrors++;
        driver->totalTransferErrors++;
    }

    /* We set the action ID to negative if the transfer was not started as part of
     * walking the action graph (we don't use 0 because that is a valid vertex id). */
    if(actionID >= 0) {
        _tgendriver_continueNextActions(driver, actionID);
    }
}

static void _tgendriver_onGeneratorTransferComplete(TGenDriver* driver, TGenGenerator* generator, gboolean wasSuccess) {
    TGEN_ASSERT(driver);

    _tgendriver_onTransferComplete(driver, GINT_TO_POINTER(-1), wasSuccess);
    tgengenerator_onTransferCompleted(generator);

    if(tgengenerator_isDoneGenerating(generator) &&
            tgengenerator_getNumOutstandingTransfers(generator) <= 0) {
        tgen_info("Model action complete, continue to next action");
        TGenActionID actionID = tgengenerator_getModelActionID(generator);
        _tgendriver_continueNextActions(driver, actionID);
    }
}

static void _tgendriver_onBytesTransferred(TGenDriver* driver, gsize bytesRead, gsize bytesWritten) {
    TGEN_ASSERT(driver);

    driver->totalBytesRead += bytesRead;
    driver->heartbeatBytesRead += bytesRead;
    driver->totalBytesWritten += bytesWritten;
    driver->heartbeatBytesWritten += bytesWritten;
}

static gboolean _tgendriver_onHeartbeat(TGenDriver* driver, gpointer nullData) {
    TGEN_ASSERT(driver);

    tgen_message("[driver-heartbeat] bytes-read=%"G_GSIZE_FORMAT" bytes-written=%"G_GSIZE_FORMAT
            " current-transfers-succeeded=%"G_GUINT64_FORMAT" current-transfers-failed=%"G_GUINT64_FORMAT
            " total-transfers-succeeded=%"G_GUINT64_FORMAT" total-transfers-failed=%"G_GUINT64_FORMAT,
            driver->heartbeatBytesRead, driver->heartbeatBytesWritten,
            driver->heartbeatTransfersCompleted, driver->heartbeatTransferErrors,
            driver->totalTransfersCompleted, driver->totalTransferErrors);

    driver->heartbeatTransfersCompleted = 0;
    driver->heartbeatTransferErrors = 0;
    driver->heartbeatBytesRead = 0;
    driver->heartbeatBytesWritten = 0;

    tgenio_checkTimeouts(driver->io);

    /* even if the client ended, we keep serving requests.
     * we are still running and the heartbeat timer still owns a driver ref.
     * do not cancel the timer */
    return FALSE;
}

static void _tgendriver_onNewPeer(TGenDriver* driver, gint socketD, gint64 started, gint64 created, TGenPeer* peer) {
    TGEN_ASSERT(driver);

    /* we have a new peer connecting to our listening socket */
    if(driver->serverHasEnded) {
        close(socketD);
        return;
    }

    /* this connect was initiated by the other end.
     * transfer information will be sent to us later. */
    TGenTransport* transport = tgentransport_newPassive(socketD, started, created, peer,
            (TGenTransport_notifyBytesFunc) _tgendriver_onBytesTransferred, driver,
            (GDestroyNotify)tgendriver_unref);

    if(!transport) {
        tgen_warning("failed to initialize transport for incoming peer, skipping");
        return;
    }

    /* ref++ the driver for the transport notify func */
    tgendriver_ref(driver);

    /* a new transfer will be coming in on this transport */
    gsize count = ++(driver->globalTransferCounter);

    TGenStreamOptions* options = &driver->startOptions->defaultStreamOpts;

    /* don't send a Markov model on passive streams */
    TGenTransfer* transfer = tgentransfer_new("passive-stream", count, options, NULL,
            driver->io, transport, (TGenTransfer_notifyCompleteFunc)_tgendriver_onTransferComplete,
            driver, (GDestroyNotify)tgendriver_unref, GINT_TO_POINTER(-1), NULL);

    if(!transfer) {
        tgentransport_unref(transport);
        tgendriver_unref(driver);
        tgen_warning("failed to initialize transfer for incoming peer, skipping");
        return;
    }

    /* ref++ the driver for the transfer notify func */
    tgendriver_ref(driver);

    /* now let the IO handler manage the transfer. our transfer pointer reference
     * will be held by the IO object */
    tgenio_register(driver->io, tgentransport_getDescriptor(transport),
            (TGenIO_notifyEventFunc)tgentransfer_onEvent,
            (TGenIO_notifyCheckTimeoutFunc) tgentransfer_onCheckTimeout,
            transfer, (GDestroyNotify)tgentransfer_unref);

    /* release our transport pointer reference, the transfer should hold one */
    tgentransport_unref(transport);
}

static gboolean _tgendriver_createNewActiveTransfer(TGenDriver* driver,
        TGenActionID actionID, TGenStreamOptions* options,
        TGenTransfer_notifyCompleteFunc onComplete,
        gpointer callbackArg1, GDestroyNotify arg1Destroy,
        gpointer callbackArg2, GDestroyNotify arg2Destroy) {
    TGEN_ASSERT(driver);

    /* active streams need markov models */
    TGenMarkovModel* mmodel = NULL;
    guint32 seed = options->packetModelSeed.isSet ? options->packetModelSeed.value : g_random_int();
    if(options->packetModelPath.isSet) {
        gchar* path = options->packetModelPath.value;
        gchar* name = g_path_get_basename(path);
        mmodel = tgenmarkovmodel_newFromPath(name, seed, path);
        g_free(name);
        /* we should have already validated this when we parsed the config graph */
        if(!mmodel) {
            tgen_error("A previously validated Markov model should be valid");
            return FALSE;
        }
    } else {
        const gchar* modelGraphml = tgenconfig_getDefaultPacketMarkovModelString();
        GString* graphmlBuffer = g_string_new(modelGraphml);
        mmodel = tgenmarkovmodel_newFromString("internal-nonstop-model", seed, graphmlBuffer);
        g_string_free(graphmlBuffer, TRUE);
        if(!mmodel) {
            tgen_error("The internal nonstop Markov model format is incorrect, check the syntax");
            return FALSE;
        }
    }

    /* create the transport connection over which we can start a transfer */
    TGenTransport* transport = tgentransport_newActive(options,
            (TGenTransport_notifyBytesFunc) _tgendriver_onBytesTransferred,
            driver, (GDestroyNotify)tgendriver_unref);

    if(transport) {
        /* ref++ the driver because the transport object is holding a ref to it
         * as a generic callback parameter for the notify function callback */
        tgendriver_ref(driver);
    } else {
        tgen_warning("failed to initialize transport for active transfer");
        return FALSE;
    }

    /* get transfer counter id */
    const gchar* actionIDStr = tgengraph_getActionName(driver->actionGraph, actionID);
    gsize count = ++(driver->globalTransferCounter);

    /* a new transfer will be coming in on this transport. the transfer
     * takes control of the transport pointer reference. */
    TGenTransfer* transfer = tgentransfer_new(actionIDStr, count, options, mmodel,
            driver->io, transport, onComplete,
            callbackArg1, arg1Destroy, callbackArg2, arg2Destroy);

    if(!transfer) {
        /* the transport was created, but we failed to create the transfer.
         * so we should clean up the transport since we no longer need it.
         * The transport unref should call the driver unref function that we passed
         * as the destroy function, so we don't need to unref driver again. */
        tgentransport_unref(transport);

        tgen_warning("failed to initialize active transfer");
        return FALSE;
    }

    /* now let the IO handler manage the transfer. our transfer pointer reference
     * will be held by the IO object */
    tgenio_register(driver->io, tgentransport_getDescriptor(transport),
            (TGenIO_notifyEventFunc)tgentransfer_onEvent,
            (TGenIO_notifyCheckTimeoutFunc) tgentransfer_onCheckTimeout,
            transfer, (GDestroyNotify)tgentransfer_unref);

    /* release our local transport pointer ref (from when we initialized the new transport)
     * because the transfer now owns it and holds the ref */
    tgentransport_unref(transport);

    return TRUE;
}

static void _tgendriver_initiateTransfer(TGenDriver* driver, TGenActionID actionID) {
    TGEN_ASSERT(driver);

    TGenStreamOptions* options = tgengraph_getStreamOptions(driver->actionGraph, actionID);

    gboolean isSuccess = _tgendriver_createNewActiveTransfer(driver, actionID, options,
            (TGenTransfer_notifyCompleteFunc)_tgendriver_onTransferComplete,
            driver, (GDestroyNotify)tgendriver_unref, GINT_TO_POINTER(actionID), NULL);

    if(isSuccess) {
        /* ref++ the driver because the transfer object is holding refs as
         * a generic callback parameter for the notify function callback.
         * it will get unref'd when the transfer finishes. */
        tgendriver_ref(driver);
    } else {
       tgen_warning("skipping failed transfer action and continuing to the next action");
       _tgendriver_continueNextActions(driver, actionID);
    }
}

static gboolean _tgendriver_setGeneratorDelayTimer(TGenDriver* driver, TGenGenerator* generator,
        guint64 delayTimeUSec) {
    TGEN_ASSERT(driver);

    /* create a timer to handle so we can delay before starting the next transfer */
    TGenTimer* generatorTimer = tgentimer_new(delayTimeUSec, FALSE,
            (TGenTimer_notifyExpiredFunc)_tgendriver_onGeneratorTimerExpired, driver, generator,
            (GDestroyNotify)tgendriver_unref, (GDestroyNotify)tgengenerator_unref);

    if(!generatorTimer) {
        tgen_warning("failed to initialize timer for model action");
        return FALSE;
    }

    tgen_info("set generator delay timer for %"G_GUINT64_FORMAT" microseconds", delayTimeUSec);

    /* ref++ the driver and generator for the refs held in the timer */
    tgendriver_ref(driver);
    tgengenerator_ref(generator);

    /* let the IO module handle timer reads, transfer the timer pointer reference */
    tgenio_register(driver->io, tgentimer_getDescriptor(generatorTimer),
            (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL, generatorTimer,
            (GDestroyNotify)tgentimer_unref);

    return TRUE;
}

static void _tgendriver_generateNextTransfer(TGenDriver* driver, TGenGenerator* generator) {
    TGEN_ASSERT(driver);

    TGenActionID actionID = tgengenerator_getModelActionID(generator);

    /* if these strings are non-null following this call, we own and must free them */
    gchar* localSchedule = NULL;
    gchar* remoteSchedule = NULL;
    guint64 delayTimeUSec = 0;
    gboolean shouldCreateStream = tgengenerator_generateStream(generator,
            &localSchedule, &remoteSchedule, &delayTimeUSec);

    if(!shouldCreateStream) {
       /* the generator reached the end of the streams for this flow,
        * so the action is now complete. */
        tgen_info("Generator reached end state after generating %u streams and %u packets",
                tgengenerator_getNumStreamsGenerated(generator),
                tgengenerator_getNumPacketsGenerated(generator));

        guint numOutstanding = tgengenerator_getNumOutstandingTransfers(generator);
        tgen_info("Generator has %u outstanding transfers", numOutstanding);

        if(tgengenerator_isDoneGenerating(generator) && numOutstanding <= 0) {
            tgen_info("Model action complete, continue to next action");
            _tgendriver_continueNextActions(driver, actionID);
        } else {
            tgen_info("Model action will be complete once all outstanding transfers finish");
        }

        tgengenerator_unref(generator);
        return;
    }

    /* we need to create a new transfer according to the schedules from the generator */
    TGenFlowOptions* flowOptions = tgengraph_getFlowOptions(driver->actionGraph, actionID);
    TGenStreamOptions* streamOptions = &flowOptions->streamOpts;

    /* Create the schedule type transfer. The sizes will be computed from the
     * schedules, and timeout and stallout will be taken from the default start vertex.
     * We pass a NULL action, because we don't want to continue in the action graph
     * when this transfer completes (we continue when the generator is done). */
    gboolean isSuccess = _tgendriver_createNewActiveTransfer(driver, actionID, streamOptions,
            (TGenTransfer_notifyCompleteFunc)_tgendriver_onGeneratorTransferComplete,
            driver, (GDestroyNotify)tgendriver_unref,
            generator, (GDestroyNotify)tgengenerator_unref);

    if(isSuccess) {
        /* ref++ the driver and generator because the transfer object is holding refs
         * as generic callback parameters for the notify function callback.
         * these will get unref'd when the transfer finishes. */
        tgendriver_ref(driver);
        tgengenerator_ref(generator);
        tgengenerator_onTransferCreated(generator);
    } else {
       tgen_warning("we failed to create a transfer in model action, "
               "delaying %"G_GUINT64_FORMAT" microseconds before the next try",
               delayTimeUSec);
    }

    isSuccess = _tgendriver_setGeneratorDelayTimer(driver, generator, delayTimeUSec);

    if(!isSuccess) {
        tgen_warning("Failed to set generator delay timer for %"G_GUINT64_FORMAT" "
                "microseconds. Stopping generator now and skipping to next action.",
                delayTimeUSec);
        if(tgengenerator_isDoneGenerating(generator) &&
                tgengenerator_getNumOutstandingTransfers(generator) <= 0) {
            tgen_info("Model action complete, continue to next action");
            _tgendriver_continueNextActions(driver, actionID);
        }
        tgengenerator_unref(generator);
    }

    tgen_info("successfully generated new transfer");

    if(localSchedule) {
        g_free(localSchedule);
    }
    if(remoteSchedule) {
        g_free(remoteSchedule);
    }
}

static void _tgendriver_initiateGenerator(TGenDriver* driver, TGenActionID actionID) {
    TGEN_ASSERT(driver);

//    TGenPeer* proxy = tgenaction_getSocksProxy(driver->startAction);

    /* these strings are owned by the action and we should not free them */
    gchar* streamModelPath = NULL;
    gchar* packetModelPath = NULL;
//    tgenaction_getModelPaths(action, &streamModelPath, &packetModelPath);

    TGenGenerator* generator = tgengenerator_new(streamModelPath, packetModelPath, actionID);

    if(!generator) {
        tgen_warning("failed to initialize generator for model action, skipping");
//        _tgendriver_continueNextActions(driver, action);
        return;
    }

    /* start generating transfers! */
    _tgendriver_generateNextTransfer(driver, generator);
}

static gboolean _tgendriver_initiatePause(TGenDriver* driver,
        TGenActionID actionID, TGenPauseOptions* options) {
    TGEN_ASSERT(driver);
    g_assert(options);

    guint64 pauseMicros = 0;
    if(options->times.isSet) {
        guint64* timeNanos = tgenpool_getRandom(options->times.value);
        g_assert(timeNanos);
        pauseMicros = (guint64)(*timeNanos / 1000);
    }

    /* if pause time is 0, just go to the next action */
    if(pauseMicros == 0) {
        tgen_info("Skipping pause action with 0 pause time");
        return FALSE;
    }

    gpointer actionIDPtr = GINT_TO_POINTER(actionID);

    /* create a timer to handle the pause action */
    TGenTimer* pauseTimer = tgentimer_new(pauseMicros, FALSE,
            (TGenTimer_notifyExpiredFunc)_tgendriver_onPauseTimerExpired, driver, actionIDPtr,
            (GDestroyNotify)tgendriver_unref, NULL);

    if(!pauseTimer) {
        tgen_warning("failed to initialize timer for pause action, skipping");
        return FALSE;
    }

    tgen_info("set pause timer for %"G_GUINT64_FORMAT" microseconds", pauseMicros);

    /* ref++ the driver and action for the pause timer */
    tgendriver_ref(driver);

    /* let the IO module handle timer reads, transfer the timer pointer reference */
    tgenio_register(driver->io, tgentimer_getDescriptor(pauseTimer),
            (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL, pauseTimer,
            (GDestroyNotify)tgentimer_unref);

    return TRUE;
}

static void _tgendriver_handlePause(TGenDriver* driver, TGenActionID actionID) {
    TGEN_ASSERT(driver);

    TGenPauseOptions* options = tgengraph_getPauseOptions(driver->actionGraph, actionID);

    if(options->times.isSet) {
        /* do a normal pause based on pause time */
        gboolean success = _tgendriver_initiatePause(driver, actionID, options);
        if(!success) {
            /* we have no timer set, lets just continue now so we don't stall forever */
            _tgendriver_continueNextActions(driver, actionID);
        }
    } else {
        /* do a 'synchronizing' pause where we wait until all incoming edges visit us */
        gboolean allVisited = tgengraph_incrementPauseVisited(driver->actionGraph, actionID);
        if(allVisited) {
            _tgendriver_continueNextActions(driver, actionID);
        }
    }
}

static void _tgendriver_checkEndConditions(TGenDriver* driver, TGenActionID actionID) {
    TGEN_ASSERT(driver);

    TGenEndOptions* options = tgengraph_getEndOptions(driver->actionGraph, actionID);

    /* only enforce limits if the limits are set by the user */

    if(options->sendSize.isSet) {
        if(driver->totalBytesWritten >= ((gsize)options->sendSize.value)) {
            tgen_message("TGen will end because we sent %"G_GSIZE_FORMAT" bytes "
                    "and we exceeded the configured send limit of %"G_GUINT64_FORMAT" bytes",
                    driver->totalBytesWritten, options->sendSize.value);
            driver->clientHasEnded = TRUE;
            driver->serverHasEnded = TRUE;
        }
    }

    if(options->recvSize.isSet) {
        if(driver->totalBytesRead >= ((gsize)options->recvSize.value)) {
            tgen_message("TGen will end because we received %"G_GSIZE_FORMAT" bytes "
                    "and we exceeded the configured receive limit of %"G_GUINT64_FORMAT" bytes",
                    driver->totalBytesRead, options->recvSize.value);
            driver->clientHasEnded = TRUE;
            driver->serverHasEnded = TRUE;
        }
    }

    if(options->count.isSet) {
        if(driver->totalTransfersCompleted >= options->count.value) {
            tgen_message("TGen will end because we completed %"G_GUINT64_FORMAT" transfers "
                    "and we exceeded the configured limit of %"G_GUINT64_FORMAT" transfers",
                    driver->totalTransfersCompleted, options->count.value);
            driver->clientHasEnded = TRUE;
            driver->serverHasEnded = TRUE;
        }
    }

    if(options->timeNanos.isSet) {
        gint64 nowMicros = g_get_monotonic_time();
        gint64 elapsedMicros = nowMicros - driver->startTimeMicros;
        elapsedMicros = MAX(0, elapsedMicros);
        guint64 elapsedNanos = (guint64) (((guint64) elapsedMicros) * 1000);

        if(elapsedNanos >= options->timeNanos.value) {
            tgen_message("TGen will end because %"G_GUINT64_FORMAT" nanoseconds have elapsed "
                    "and exceeded the configured limit of %"G_GUINT64_FORMAT" nanoseconds",
                    elapsedNanos, options->timeNanos.value);
            driver->clientHasEnded = TRUE;
            driver->serverHasEnded = TRUE;
        }
    }
}

static void _tgendriver_processAction(TGenDriver* driver, TGenActionID actionID) {
    TGEN_ASSERT(driver);

    switch(tgengraph_getActionType(driver->actionGraph, actionID)) {
        case TGEN_ACTION_START: {
            /* slide through to the next actions */
            _tgendriver_continueNextActions(driver, actionID);
            break;
        }
        case TGEN_ACTION_STREAM: {
            _tgendriver_initiateTransfer(driver, actionID);
            break;
        }
        case TGEN_ACTION_FLOW: {
            tgen_warning("TGen is degraded, flow actions are not supported at the moment");
//            _tgendriver_initiateGenerator(driver, actionID); TODO XXX
            _tgendriver_continueNextActions(driver, actionID);
            break;
        }
        case TGEN_ACTION_END: {
            _tgendriver_checkEndConditions(driver, actionID);
            _tgendriver_continueNextActions(driver, actionID);
            break;
        }
        case TGEN_ACTION_PAUSE: {
            _tgendriver_handlePause(driver, actionID);
            break;
        }
        case TGEN_ACTION_NONE:
        default: {
            tgen_warning("unrecognized action type");
            break;
        }
    }
}

static void _tgendriver_continueNextActions(TGenDriver* driver, TGenActionID actionID) {
    TGEN_ASSERT(driver);

    if(driver->clientHasEnded) {
        return;
    }

    GQueue* nextActions = tgengraph_getNextActionIDs(driver->actionGraph, actionID);
    g_assert(nextActions);

    while(g_queue_get_length(nextActions) > 0) {
        gpointer actionIDPtr = g_queue_pop_head(nextActions);
        TGenActionID nextActionID = (TGenActionID) GPOINTER_TO_INT(actionIDPtr);
        _tgendriver_processAction(driver, nextActionID);
    }

    g_queue_free(nextActions);
}

void tgendriver_activate(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    tgen_debug("activating tgenio loop");

    gint numEventsProcessed = MAX_EVENTS_PER_IO_LOOP;

    while(numEventsProcessed >= MAX_EVENTS_PER_IO_LOOP) {
        numEventsProcessed = tgenio_loopOnce(driver->io, MAX_EVENTS_PER_IO_LOOP);
        tgen_debug("processed %i events out of the max allowed of %i", numEventsProcessed, MAX_EVENTS_PER_IO_LOOP);
    }

    tgen_debug("tgenio loop complete");
}

static void _tgendriver_free(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    g_assert(driver->refcount <= 0);

    tgen_info("freeing driver state");

    if(driver->io) {
        tgenio_unref(driver->io);
    }
    if(driver->actionGraph) {
        tgengraph_unref(driver->actionGraph);
    }

    driver->magic = 0;
    g_free(driver);
}

void tgendriver_ref(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    driver->refcount++;
}

void tgendriver_unref(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    if(--driver->refcount <= 0) {
        _tgendriver_free(driver);
    }
}

//static gchar* _tgendriver_makeTempFile() {
//    gchar nameBuffer[256];
//    memset(nameBuffer, 0, 256);
//    tgenconfig_gethostname(nameBuffer, 255);
//
//    GString* templateBuffer = g_string_new("XXXXXX-shadow-tgen-");
//    g_string_append_printf(templateBuffer, "%s.xml", nameBuffer);
//
//    gchar* temporaryFilename = NULL;
//    gint openedFile = g_file_open_tmp(templateBuffer->str, &temporaryFilename, NULL);
//
//    g_string_free(templateBuffer, TRUE);
//
//    if(openedFile > 0) {
//        close(openedFile);
//        return g_strdup(temporaryFilename);
//    } else {
//        return NULL;
//    }
//}

static gboolean _tgendriver_startServerHelper(TGenDriver* driver, in_port_t serverPort) {
    TGEN_ASSERT(driver);

    /* create the server that will listen for incoming connections */
    TGenServer* server = tgenserver_new(serverPort,
            (TGenServer_notifyNewPeerFunc)_tgendriver_onNewPeer, driver,
            (GDestroyNotify)tgendriver_unref);

    if(server) {
        /* the server is holding a ref to driver */
        tgendriver_ref(driver);

        /* now let the IO handler manage the server. transfer our server pointer reference
         * because it will be stored as a param in the IO object */
        gint socketD = tgenserver_getDescriptor(server);
        tgenio_register(driver->io, socketD, (TGenIO_notifyEventFunc)tgenserver_onEvent, NULL,
                server, (GDestroyNotify) tgenserver_unref);

        tgen_message("Started server on port %u using descriptor %i", ntohs(serverPort), socketD);
        return TRUE;
    } else {
        tgen_critical("Unable to start server on port %u", ntohs(serverPort));
        return FALSE;
    }
}

static gboolean _tgendriver_setStartClientTimerHelper(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    /* get the delay in microseconds from now to start the client */
    guint64 delayMicros = 0;
    if(driver->startOptions->timeNanos.isSet) {
        delayMicros = (guint64)(driver->startOptions->timeNanos.value / 1000);
    }

    /* client will start in the future */
    TGenTimer* startTimer = tgentimer_new(delayMicros, FALSE,
            (TGenTimer_notifyExpiredFunc)_tgendriver_onStartClientTimerExpired, driver, NULL,
            (GDestroyNotify)tgendriver_unref, NULL);

    if(startTimer) {
        /* ref++ the driver since the timer is now holding a reference */
        tgendriver_ref(driver);

        /* let the IO module handle timer reads, transfer the timer pointer reference */
        gint timerD = tgentimer_getDescriptor(startTimer);
        tgenio_register(driver->io, timerD, (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL,
                startTimer, (GDestroyNotify)tgentimer_unref);

        tgen_info("set startClient timer using descriptor %i", timerD);
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _tgendriver_setHeartbeatTimerHelper(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    guint64 heartbeatPeriodMicros = 0;
    if(driver->startOptions->heartbeatPeriodNanos.isSet) {
        heartbeatPeriodMicros = (guint64)(driver->startOptions->heartbeatPeriodNanos.value / 1000);
    } else {
        heartbeatPeriodMicros = 1000*1000; /* 1 second */
    }

    /* start the heartbeat as a persistent timer event */
    TGenTimer* heartbeatTimer = tgentimer_new(heartbeatPeriodMicros, TRUE,
            (TGenTimer_notifyExpiredFunc)_tgendriver_onHeartbeat, driver, NULL,
            (GDestroyNotify)tgendriver_unref, NULL);

    if(heartbeatTimer) {
        /* ref++ the driver since the timer is now holding a reference */
        tgendriver_ref(driver);

        /* let the IO module handle timer reads, transfer the timer pointer reference */
        gint timerD = tgentimer_getDescriptor(heartbeatTimer);
        tgenio_register(driver->io, timerD, (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL,
                heartbeatTimer, (GDestroyNotify)tgentimer_unref);

        tgen_info("set heartbeat timer using descriptor %i", timerD);
        return TRUE;
    } else {
        return FALSE;
    }
}

TGenDriver* tgendriver_new(TGenGraph* graph) {
    /* create the main driver object */
    TGenDriver* driver = g_new0(TGenDriver, 1);
    driver->magic = TGEN_MAGIC;
    driver->refcount = 1;

    driver->io = tgenio_new();

    tgengraph_ref(graph);
    driver->actionGraph = graph;
    driver->startActionID = tgengraph_getStartActionID(graph);
    driver->startOptions = tgengraph_getStartOptions(graph);

    /* start a heartbeat status message every second */
    if(!_tgendriver_setHeartbeatTimerHelper(driver)) {
        tgendriver_unref(driver);
        return NULL;
    }

    /* only run a server if server port is set */
    if(driver->startOptions->serverport.isSet) {
        /* start a server to listen for incoming connections */
        in_port_t serverPort = htons((in_port_t)driver->startOptions->serverport.value);
        if(!_tgendriver_startServerHelper(driver, serverPort)) {
            tgendriver_unref(driver);
            return NULL;
        }
    } else {
        driver->serverHasEnded = TRUE;
    }

    /* only run the client if we have (non-start) actions we need to process */
    if(tgengraph_hasEdges(driver->actionGraph)) {
        /* the client-side transfers start as specified in the graph.
         * start our client after a timeout */
        if(!_tgendriver_setStartClientTimerHelper(driver)) {
            tgendriver_unref(driver);
            return NULL;
        }
    } else {
        driver->clientHasEnded = TRUE;
    }

    return driver;
}

gint tgendriver_getEpollDescriptor(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return tgenio_getEpollDescriptor(driver->io);
}

gboolean tgendriver_hasEnded(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return (driver->clientHasEnded && driver->serverHasEnded) ? TRUE : FALSE;
}

static gboolean _tgendriver_onStartClientTimerExpired(TGenDriver* driver, gpointer nullData) {
    TGEN_ASSERT(driver);

    driver->startTimeMicros = g_get_monotonic_time();

    tgen_message("starting client using action graph '%s'",
            tgengraph_getGraphPath(driver->actionGraph));
    _tgendriver_continueNextActions(driver, driver->startActionID);

    /* timer was a one time event, so it can be canceled and freed */
    return TRUE;
}

static gboolean _tgendriver_onPauseTimerExpired(TGenDriver* driver, gpointer actionIDPtr) {
    TGEN_ASSERT(driver);

    TGenActionID actionID = GPOINTER_TO_INT(actionIDPtr);

    tgen_info("pause timer expired");

    /* continue next actions if possible */
    _tgendriver_continueNextActions(driver, actionID);
    /* timer was a one time event, so it can be canceled and freed */
    return TRUE;
}

static gboolean _tgendriver_onGeneratorTimerExpired(TGenDriver* driver, TGenGenerator* generator) {
    TGEN_ASSERT(driver);

    tgen_info("generator timer expired");
    _tgendriver_generateNextTransfer(driver, generator);

    /* timer was a one time event, so it can be canceled and freed */
    return TRUE;
}

