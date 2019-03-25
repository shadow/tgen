/*
 * See LICENSE for licensing information
 */

#include "tgen.h"

struct _TGenFlow {
    TGenMarkovModel* streamModel;
    TGenStreamOptions* streamOptions;
    TGenActionID actionID;
    const gchar* actionIDStr;

    guint numStreamsGenerated;
    guint numStreamsCompleted;
    gboolean reachedEndState;

    TGenIO* io;

    TGenTransport_notifyBytesFunc onBytes;
    TGenFlow_notifyCompleteFunc onComplete;
    gpointer arg;
    GDestroyNotify argRef;
    GDestroyNotify argUnref;

    gint refcount;
    guint magic;
};

/* forward declaration */
static void _tgenflow_generateNextStream(TGenFlow* flow);

static void _tgenflow_free(TGenFlow* flow) {
    TGEN_ASSERT(flow);
    g_assert(flow->refcount == 0);

    if(flow->streamModel) {
        tgenmarkovmodel_unref(flow->streamModel);
    }

    if(flow->io) {
        tgenio_unref(flow->io);
    }

    if(flow->arg && flow->argUnref) {
        flow->argUnref(flow->arg);
    }

    flow->magic = 0;
    g_free(flow);
}

static void _tgenflow_ref(TGenFlow* flow) {
    TGEN_ASSERT(flow);
    flow->refcount++;
}

static void _tgenflow_unref(TGenFlow* flow) {
    TGEN_ASSERT(flow);
    if(--(flow->refcount) == 0) {
        _tgenflow_free(flow);
    }
}

TGenFlow* tgenflow_new(TGenMarkovModel* streamModel, TGenStreamOptions* streamOptions,
        TGenActionID actionID, const gchar* actionIDStr, TGenIO* io,
        TGenTransport_notifyBytesFunc onBytes,
        TGenFlow_notifyCompleteFunc onComplete,
        gpointer arg, GDestroyNotify argRef, GDestroyNotify argUnref) {
    TGenFlow* flow = g_new0(TGenFlow, 1);
    flow->magic = TGEN_MAGIC;
    flow->refcount = 1;

    flow->streamModel = streamModel;
    flow->streamOptions = streamOptions;
    flow->actionID = actionID;
    flow->actionIDStr = actionIDStr;

    flow->io = io;
    tgenio_ref(io);

    flow->onBytes = onBytes;
    flow->onComplete = onComplete;
    flow->arg = arg;
    flow->argRef = argRef;
    flow->argUnref = argUnref;

    return flow;
}

static void _tgenflow_onStreamComplete(TGenFlow* flow, gpointer none, gboolean wasSuccess) {
    TGEN_ASSERT(flow);

    /* here we call the onComplete function in the driver so it can track some stats.
     * if the flow is all done, then we need to pass a non-negative actionID so the
     * driver knows how to continue in the action graph. if we still need to generate
     * more streams, then we return a negative actionID. */

    flow->numStreamsCompleted++;

    TGenFlowFlags flags = TGEN_FLOW_STREAM_COMPLETE;
    if(wasSuccess) {
        flags |= TGEN_FLOW_STREAM_SUCCESS;
    }

    if(flow->reachedEndState && flow->numStreamsCompleted >= flow->numStreamsGenerated) {
        /* the flow is all done! */
        tgen_message("Flow action '%s' status: completed %u of %u streams, flow is complete",
                flow->actionIDStr, flow->numStreamsCompleted, flow->numStreamsGenerated);

        flags |= TGEN_FLOW_COMPLETE;

        flow->onComplete(flow->arg, flow->actionID, flags);

        /* now we delete ourselves because we are done. the flow should never be used again. */
        _tgenflow_unref(flow);
    } else {
        tgen_info("Flow action '%s' status: completed %u of %u streams, flow is still active",
                flow->actionIDStr, flow->numStreamsCompleted, flow->numStreamsGenerated);

        flow->onComplete(flow->arg, flow->actionID, flags);
    }
}

static gboolean _tgenflow_createStream(TGenFlow* flow) {
    TGEN_ASSERT(flow);

    /* streams need packet Markov models */
    TGenMarkovModel* packetModel = NULL;

    /* get the seed if one was configured, otherwise generate a random seed */
    guint32 seed = flow->streamOptions->packetModelSeed.isSet ?
            flow->streamOptions->packetModelSeed.value : g_random_int();

    /* make sure we have a valid packet model for the stream */
    if(flow->streamOptions->packetModelPath.isSet) {
        gchar* path = flow->streamOptions->packetModelPath.value;
        gchar* name = g_path_get_basename(path);
        packetModel = tgenmarkovmodel_newFromPath(name, seed, path);
        g_free(name);

        /* we should have already validated this when we parsed the config graph */
        if(!packetModel) {
            tgen_error("A previously validated packet Markov model should be valid");
            return FALSE;
        }
    } else {
        const gchar* modelGraphml = tgenconfig_getDefaultPacketMarkovModelString();
        GString* graphmlBuffer = g_string_new(modelGraphml);
        packetModel = tgenmarkovmodel_newFromString("internal-packet-model", seed, graphmlBuffer);
        g_string_free(graphmlBuffer, TRUE);

        /* the internal model should be correct */
        if(!packetModel) {
            tgen_error("The internal packet Markov model format is incorrect, check the syntax");
            return FALSE;
        }
    }

    /* create the transport connection over which we can start a transfer */
    TGenTransport* transport = tgentransport_newActive(flow->streamOptions,
            flow->onBytes, flow->arg, flow->argUnref);

    if(transport) {
        /* ref++ the driver because the transport object is holding a ref to it
         * as a generic callback parameter for the notify function callback */
        flow->argRef(flow->arg);
    } else {
        tgen_warning("failed to initialize transport for stream '%s'", flow->actionIDStr);
        return FALSE;
    }

    /* a new transfer will be coming in on this transport. the transfer
     * takes control of the transport pointer reference. */
    TGenTransfer* transfer = tgentransfer_new(flow->actionIDStr, flow->streamOptions, packetModel,
            transport, (TGenTransfer_notifyCompleteFunc)_tgenflow_onStreamComplete,
            flow, (GDestroyNotify) _tgenflow_unref, NULL, NULL);

    if(transfer) {
        /* the transfer is holding a ref to the flow */
        _tgenflow_ref(flow);
    } else {
        /* the transport was created, but we failed to create the transfer.
         * so we should clean up the transport since we no longer need it.
         * The transport unref should call the arg unref function that we passed
         * as the destroy function, so we don't need to unref the arg again. */
        tgentransport_unref(transport);

        tgen_warning("failed to initialize stream");
        return FALSE;
    }

    /* now let the IO handler manage the transfer. our transfer pointer reference
     * will be held by the IO object */
    tgenio_register(flow->io, tgentransport_getDescriptor(transport),
            (TGenIO_notifyEventFunc)tgentransfer_onEvent,
            (TGenIO_notifyCheckTimeoutFunc) tgentransfer_onCheckTimeout,
            transfer, (GDestroyNotify)tgentransfer_unref);

    /* release our local transport pointer ref (from when we initialized the new transport)
     * because the transfer now owns it and holds the ref */
    tgentransport_unref(transport);

    /* increment our stream counter */
    flow->numStreamsGenerated++;

    return TRUE;
}

static gboolean _tgenflow_onTimerExpired(TGenFlow* flow, gpointer none) {
    TGEN_ASSERT(flow);

    tgen_info("Inter-stream delay timer expired on flow '%s'", flow->actionIDStr);
    _tgenflow_generateNextStream(flow);

    /* timer was a one time event, so it can be canceled and freed */
    return TRUE;
}

static gboolean _tgenflow_setTimer(TGenFlow* flow, guint64 delayTimeUSec) {
    TGEN_ASSERT(flow);

    /* create a timer to handle so we can delay before starting the next transfer */
    TGenTimer* flowTimer = tgentimer_new(delayTimeUSec, FALSE,
            (TGenTimer_notifyExpiredFunc)_tgenflow_onTimerExpired, flow, NULL,
            (GDestroyNotify)_tgenflow_unref, NULL);

    if(!flowTimer) {
        tgen_warning("Failed to initialize timer for flow '%s'", flow->actionIDStr);
        return FALSE;
    }

    tgen_info("Set timer of %"G_GUINT64_FORMAT" microseconds for flow '%s'",
            delayTimeUSec, flow->actionIDStr);

    /* ref++ the flow for the ref held in the timer */
    _tgenflow_ref(flow);

    /* let the IO module handle timer reads, transfer the timer pointer reference */
    tgenio_register(flow->io, tgentimer_getDescriptor(flowTimer),
            (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL, flowTimer,
            (GDestroyNotify)tgentimer_unref);

    return TRUE;
}

static void _tgenflow_generateNextStream(TGenFlow* flow) {
    TGEN_ASSERT(flow);
    g_assert(!flow->reachedEndState);
    g_assert(flow->streamModel);

    tgen_debug("Generating next stream observation on flow '%s'", flow->actionIDStr);

    guint64 streamDelay = 0;
    Observation obs = tgenmarkovmodel_getNextObservation(flow->streamModel, &streamDelay);

    if(obs == OBSERVATION_TO_ORIGIN || obs == OBSERVATION_TO_SERVER) {
        tgen_debug("Found stream observation on flow '%s' with a generated stream delay of "
                "%"G_GUINT64_FORMAT" microseconds", flow->actionIDStr, streamDelay);

        /* we should create a new stream now */
        if(_tgenflow_createStream(flow)) {
            tgen_info("Generated new stream successfully on flow '%s'", flow->actionIDStr);
        } else {
            tgen_warning("Failed to create a stream on flow '%s', "
                    "delaying %"G_GUINT64_FORMAT" microseconds before the next try",
                    flow->actionIDStr, streamDelay);
        }

        /* now wait streamDelay before creating the next one */
        if(_tgenflow_setTimer(flow, streamDelay)) {
            tgen_info("Flow '%s' will generate the next stream in %"G_GUINT64_FORMAT" microseconds",
                    flow->actionIDStr, streamDelay);
        } else {
            tgen_warning("Failed to set timer on flow '%s' for %"G_GUINT64_FORMAT" "
                    "microseconds. No more streams can be generated.",
                    flow->actionIDStr, streamDelay);

            if(flow->numStreamsCompleted >= flow->numStreamsGenerated) {
                /* tell the driver we are done so it continues the action graph */
                flow->onComplete(flow->arg, flow->actionID, TGEN_FLOW_COMPLETE);
                /* delete ourselves, dont use the flow anymore */
                _tgenflow_unref(flow);
            }
        }
    } else {
        tgen_info("Found stream end observation on flow '%s' after generating %u streams.",
                flow->actionIDStr, flow->numStreamsGenerated);

        /* we got to the end, we should not create any more streams */
        flow->reachedEndState = TRUE;

        /* if we have no outstanding streams, we are done */
        if(flow->numStreamsCompleted >= flow->numStreamsGenerated) {
            /* tell the driver we are done so it continues the action graph */
            flow->onComplete(flow->arg, flow->actionID, TGEN_FLOW_COMPLETE);
            /* delete ourselves, dont use the flow anymore */
            _tgenflow_unref(flow);
        }
    }
}

void tgenflow_start(TGenFlow* flow) {
    TGEN_ASSERT(flow);

    if(flow->streamModel) {
        /* we create streams according to the Markov model */
        _tgenflow_generateNextStream(flow);
    } else {
        /* we only create one stream */
        if(_tgenflow_createStream(flow)) {
            flow->reachedEndState = TRUE;
        } else {
            /* it failed, tell the driver to advance and free ourselves */
            flow->onComplete(flow->arg, flow->actionID, TGEN_FLOW_COMPLETE);
            _tgenflow_unref(flow);
        }
    }
}
