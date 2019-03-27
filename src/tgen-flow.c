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

static TGenMarkovModel* _tgenflow_createMarkovModel(TGenOptionPool* seedGeneratorOpt,
        TGenOptionString* modelPathOpt, const gchar* internalGraphml, const gchar* internalName) {
    g_assert(seedGeneratorOpt);
    g_assert(modelPathOpt);
    g_assert(internalGraphml);
    g_assert(internalName);

    /* get the markov model to generate streams or packets */
    TGenMarkovModel* mmodel = NULL;

    /* calculate the seed for the model */
    guint32 seed = 0;

    if(seedGeneratorOpt->isSet) {
       /* this means either the flow action had a seed, or the start action did,
        * and we used it to seed a prng that we can use for the markov models. */
       g_assert(seedGeneratorOpt->value);
       GRand* prng = tgenpool_getRandom(seedGeneratorOpt->value);
       g_assert(prng);
       seed = g_rand_int(prng);
    } else {
        /* just use the GLib-global prng */
       seed = g_random_int();
    }

    if(modelPathOpt->isSet) {
       gchar* path = modelPathOpt->value;
       gchar* name = g_path_get_basename(path);
       mmodel = tgenmarkovmodel_newFromPath(name, seed, path);
       g_free(name);

       /* we should have already validated this when we parsed the config graph */
       if(!mmodel) {
           tgen_error("A previously validated Markov model '%s' should be valid", path);
       }
    } else {
       GString* graphmlBuffer = g_string_new(internalGraphml);
       mmodel = tgenmarkovmodel_newFromString(internalName, seed, graphmlBuffer);
       g_string_free(graphmlBuffer, TRUE);

       /* the internal model should be correct */
       if(!mmodel) {
           tgen_error("The internal stream Markov model '%s' format is incorrect, "
                   "check the syntax", internalName);
       }
    }

    return mmodel;
}

TGenFlow* tgenflow_new(TGenFlowOptions* flowOptions, TGenStreamOptions* streamOptions,
        TGenActionID actionID, const gchar* actionIDStr, TGenIO* io,
        TGenTransport_notifyBytesFunc onBytes,
        TGenFlow_notifyCompleteFunc onComplete,
        gpointer arg, GDestroyNotify argRef, GDestroyNotify argUnref) {
    /* see if we need a stream model */
    TGenMarkovModel* streamModel = NULL;

    /* if there are no flow options, then this flow contains a single stream and
     * we do not need a model for generating streams. */
    if(flowOptions) {
        const gchar* internalModelGraphml = tgenconfig_getDefaultStreamMarkovModelString();
        streamModel = _tgenflow_createMarkovModel(&flowOptions->streamOpts.seedGenerator,
                &flowOptions->streamModelPath, internalModelGraphml, "internal-stream-model");
    }

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

    if(flow->arg && flow->argRef) {
        flow->argRef(flow->arg);
    }

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

    const gchar* internalModelGraphml = tgenconfig_getDefaultPacketMarkovModelString();
    TGenMarkovModel* packetModel = _tgenflow_createMarkovModel(
            &flow->streamOptions->seedGenerator, &flow->streamOptions->packetModelPath,
            internalModelGraphml, "internal-packet-model");

    /* create the transport connection over which we can start a stream */
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

    /* a new stream will be coming in on this transport. the stream
     * takes control of the transport pointer reference. */
    TGenStream* stream = tgenstream_new(flow->actionIDStr, flow->streamOptions, packetModel,
            transport, (TGenStream_notifyCompleteFunc)_tgenflow_onStreamComplete,
            flow, (GDestroyNotify) _tgenflow_unref, NULL, NULL);

    /* release our ref to the model, the stream holds its own ref */
    tgenmarkovmodel_unref(packetModel);

    if(stream) {
        /* the stream is holding a ref to the flow */
        _tgenflow_ref(flow);
    } else {
        /* the transport was created, but we failed to create the stream.
         * so we should clean up the transport since we no longer need it.
         * The transport unref should call the arg unref function that we passed
         * as the destroy function, so we don't need to unref the arg again. */
        tgentransport_unref(transport);

        tgen_warning("failed to initialize stream");
        return FALSE;
    }

    /* now let the IO handler manage the stream. our stream pointer reference
     * will be held by the IO object */
    tgenio_register(flow->io, tgentransport_getDescriptor(transport),
            (TGenIO_notifyEventFunc)tgenstream_onEvent,
            (TGenIO_notifyCheckTimeoutFunc) tgenstream_onCheckTimeout,
            stream, (GDestroyNotify)tgenstream_unref);

    /* release our local transport pointer ref (from when we initialized the new transport)
     * because the stream now owns it and holds the ref */
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

    /* create a timer to handle so we can delay before starting the next stream */
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

    /* let the IO module handle timer reads, the io now holds the timer reference */
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
