/*
 * See LICENSE for licensing information
 */

#include "tgen.h"

struct _TGenGenerator {
    TGenActionID actionID;
    const gchar* actionIDStr;

    /* the model is:
     * - a flow model if we generate flows,
     *     in which case flowOptions is non-NULL and streamOptions is NULL;
     * - a stream model if we generate streams,
     *     in which case streamOptions is non-NULL and flowOptions is NULL;
     * - NULL if we generate a single stream,
     *     in which case streamOptions is non-NULL and flowOptions is NULL. */
    TGenMarkovModel* mmodel;
    TGenFlowOptions* flowOptions;
    TGenStreamOptions* streamOptions;

    guint numGenerated;
    guint numCompleted;
    gboolean reachedEndState;

    TGenIO* io;

    TGenTransport_notifyBytesFunc onBytes;
    TGen_notifyFunc onComplete;
    gpointer arg;
    GDestroyNotify argRef;
    GDestroyNotify argUnref;

    gint refcount;
    guint magic;
};

/* forward declaration */
static void _tgengenerator_generateNext(TGenGenerator* gen);

static void _tgengenerator_free(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    g_assert(gen->refcount == 0);

    if(gen->mmodel) {
        tgenmarkovmodel_unref(gen->mmodel);
    }

    if(gen->io) {
        tgenio_unref(gen->io);
    }

    if(gen->arg && gen->argUnref) {
        gen->argUnref(gen->arg);
    }

    gen->magic = 0;
    g_free(gen);
}

static void _tgengenerator_ref(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    gen->refcount++;
}

static void _tgengenerator_unref(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    if(--(gen->refcount) == 0) {
        _tgengenerator_free(gen);
    }
}

static TGenMarkovModel* _tgengenerator_createMarkovModel(
        TGenOptionPool* seedGeneratorOpt, TGenOptionString* modelPathOpt,
        const gchar* internalGraphml, const gchar* internalName) {
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

TGenGenerator* tgengenerator_new(TGenTrafficOptions* trafficOptions,
        TGenFlowOptions* flowOptions, TGenStreamOptions* streamOptions,
        TGenActionID actionID, const gchar* actionIDStr, TGenIO* io,
        TGenTransport_notifyBytesFunc onBytes,
        TGen_notifyFunc notifyFunc, gpointer notifyArg,
        GDestroyNotify notifyArgRef, GDestroyNotify notifyArgUnref) {
    TGenGenerator* gen = g_new0(TGenGenerator, 1);
    gen->magic = TGEN_MAGIC;
    gen->refcount = 1;

    gen->actionID = actionID;
    gen->actionIDStr = actionIDStr;

    /* Check which mode we are running in. */
    if(trafficOptions) {
        /* there are traffic options, so we need to generate flows */
        const gchar* internalModelName = tgenconfig_getDefaultFlowMarkovModelName();
        const gchar* internalModelGraphml = tgenconfig_getDefaultFlowMarkovModelString();

        gen->mmodel = _tgengenerator_createMarkovModel(
                &trafficOptions->flowOpts.streamOpts.seedGenerator,
                &trafficOptions->flowModelPath,
                internalModelGraphml, internalModelName);

        /* save the flow options so we can use them for new flows */
        gen->flowOptions = flowOptions;
        gen->streamOptions = NULL;

        tgen_info("Created new flow generator on action '%s'", actionIDStr);
    } else if(flowOptions) {
        /* there are flow options, so we need to generate streams */
        const gchar* internalModelName = tgenconfig_getDefaultStreamMarkovModelName();
        const gchar* internalModelGraphml = tgenconfig_getDefaultStreamMarkovModelString();

        gen->mmodel = _tgengenerator_createMarkovModel(
                &flowOptions->streamOpts.seedGenerator,
                &flowOptions->streamModelPath,
                internalModelGraphml, internalModelName);

        /* save the stream options so we can use them for new streams */
        gen->streamOptions = streamOptions;

        tgen_info("Created new stream generator on action '%s'", actionIDStr);
    } else if(streamOptions) {
        /* no traffic or flow options, we generate a single stream */
        gen->mmodel = NULL;
        /* save the stream options so we can use them when we create the stream */
        gen->streamOptions = streamOptions;

        tgen_info("Created new generator for a single stream on action '%s'", actionIDStr);
    } else {
        tgen_error("A generator must have at least one set of options.");
        g_assert_not_reached();
    }

    gen->io = io;
    tgenio_ref(io);

    gen->onBytes = onBytes;
    gen->onComplete = notifyFunc;

    gen->arg = notifyArg;
    gen->argRef = notifyArgRef;
    gen->argUnref = notifyArgUnref;

    if(gen->arg && gen->argRef) {
        gen->argRef(gen->arg);
    }

    return gen;
}

static void _tgengenerator_onComplete(TGenGenerator* gen, TGenActionID actionID, TGenNotifyFlags flags) {
    TGEN_ASSERT(gen);

    /* here we call the onComplete function in the driver so it can track some stats.
     * if the traffic/flow is all done, then we need to pass a non-negative actionID so the
     * driver knows how to continue in the action graph. if we still need to generate
     * more flows/streams, then we return a negative actionID. */

    const gchar* childType = gen->flowOptions ? "flow" : "stream";
    const gchar* parentType = gen->flowOptions ? "traffic" : "flow";

    if((gen->flowOptions && (flags & TGEN_NOTIFY_FLOW_COMPLETE)) ||
            (gen->streamOptions && (flags & TGEN_NOTIFY_STREAM_COMPLETE))){
        gen->numCompleted++;
    }

    if(gen->reachedEndState && gen->numCompleted >= gen->numGenerated) {
        /* the flow is all done! */
        tgen_message("Generator status for action '%s': completed %u of %u %ss, %s is complete",
                gen->actionIDStr, gen->numCompleted, gen->numGenerated, childType, parentType);

        if(gen->flowOptions) {
            flags |= TGEN_NOTIFY_TRAFFIC_COMPLETE;
        } else {
            flags |= TGEN_NOTIFY_FLOW_COMPLETE;
        }

        gen->onComplete(gen->arg, gen->actionID, flags);

        /* now we delete ourselves because we are done. the flow should never be used again. */
        _tgengenerator_unref(gen);
    } else {
        tgen_info("Generator status for action '%s': completed %u of %u %ss, %s is still active",
                gen->actionIDStr, gen->numCompleted, gen->numGenerated, childType, parentType);

        /* we send an action id of -1 so the driver knows that a stream completed, but
         * it will not try to move to the next action in the action graph. */
        gen->onComplete(gen->arg, (TGenActionID)-1, flags);
    }
}

static gboolean _tgengenerator_createFlow(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    g_assert(gen->flowOptions && !gen->streamOptions);

    TGenFlowOptions* flowOpts = gen->flowOptions;
    TGenStreamOptions* streamOpts = &flowOpts->streamOpts;

    /* NULL traffic options means generate streams for one flow */
    TGenGenerator* streamGen = tgengenerator_new(NULL, flowOpts, streamOpts,
            gen->actionID, gen->actionIDStr, gen->io,
            gen->onBytes, gen->onComplete, gen->arg, gen->argRef, gen->argUnref);

    /* the generator will unref itself when it finishes generating new streams and
     * all of its previously generated streams are complete. */
    if(streamGen) {
        tgengenerator_start(streamGen);
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _tgengenerator_createStream(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    g_assert(gen->streamOptions && !gen->flowOptions);

    const gchar* internalModelName = tgenconfig_getDefaultPacketMarkovModelName();
    const gchar* internalModelGraphml = tgenconfig_getDefaultPacketMarkovModelString();

    TGenMarkovModel* packetModel = _tgengenerator_createMarkovModel(
            &gen->streamOptions->seedGenerator, &gen->streamOptions->packetModelPath,
            internalModelGraphml, internalModelName);

    /* create the transport connection over which we can start a stream */
    TGenTransport* transport = tgentransport_newActive(gen->streamOptions,
            gen->onBytes, gen->arg, gen->argUnref);

    if(transport) {
        /* ref++ the driver because the transport object is holding a ref to it
         * as a generic callback parameter for the notify function callback */
        gen->argRef(gen->arg);
    } else {
        tgen_warning("failed to initialize transport for stream '%s'", gen->actionIDStr);
        return FALSE;
    }

    /* a new stream will be coming in on this transport. the stream
     * takes control of the transport pointer reference. */
    TGenStream* stream = tgenstream_new(gen->actionIDStr, gen->streamOptions, packetModel,
            transport, (TGenActionID) -1, (TGen_notifyFunc)_tgengenerator_onComplete,
            gen, (GDestroyNotify) _tgengenerator_unref);

    /* release our ref to the model, the stream holds its own ref */
    tgenmarkovmodel_unref(packetModel);

    if(stream) {
        /* the stream is holding a ref to the flow */
        _tgengenerator_ref(gen);
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
    tgenio_register(gen->io, tgentransport_getDescriptor(transport),
            (TGenIO_notifyEventFunc)tgenstream_onEvent,
            (TGenIO_notifyCheckTimeoutFunc) tgenstream_onCheckTimeout,
            stream, (GDestroyNotify)tgenstream_unref);

    /* release our local transport pointer ref (from when we initialized the new transport)
     * because the stream now owns it and holds the ref */
    tgentransport_unref(transport);

    /* increment our counter which is counting streams */
    gen->numGenerated++;

    return TRUE;
}

static gboolean _tgengenerator_onTimerExpired(TGenGenerator* gen, gpointer none) {
    TGEN_ASSERT(gen);

    const gchar* actionType = gen->flowOptions ? "traffic" : "flow";

    tgen_info("Inter-event delay timer expired on %s '%s'", actionType, gen->actionIDStr);
    _tgengenerator_generateNext(gen);

    /* timer was a one time event, so it can be canceled and freed */
    return TRUE;
}

static gboolean _tgengenerator_setTimer(TGenGenerator* gen, guint64 delayTimeUSec) {
    TGEN_ASSERT(gen);

    /* create a timer to handle so we can delay before starting the next stream */
    TGenTimer* genTimer = tgentimer_new(delayTimeUSec, FALSE,
            (TGenTimer_notifyExpiredFunc)_tgengenerator_onTimerExpired, gen, NULL,
            (GDestroyNotify)_tgengenerator_unref, NULL);

    const gchar* actionType = gen->flowOptions ? "traffic" : "flow";

    if(!genTimer) {
        tgen_warning("Failed to initialize timer for %s '%s'", actionType, gen->actionIDStr);
        return FALSE;
    }

    tgen_info("Set timer of %"G_GUINT64_FORMAT" microseconds for %s '%s'",
            delayTimeUSec, actionType, gen->actionIDStr);

    /* ref++ the flow for the ref held in the timer */
    _tgengenerator_ref(gen);

    /* let the IO module handle timer reads, the io now holds the timer reference */
    tgenio_register(gen->io, tgentimer_getDescriptor(genTimer),
            (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL, genTimer,
            (GDestroyNotify)tgentimer_unref);

    return TRUE;
}

static void _tgengenerator_generateNext(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    g_assert(!gen->reachedEndState);
    g_assert(gen->mmodel);

    const gchar* childType = gen->flowOptions ? "flow" : "stream";
    const gchar* parentType = gen->flowOptions ? "traffic" : "flow";

    tgen_debug("Generating next %s observation on %s '%s'",
            childType, parentType, gen->actionIDStr);

    guint64 delay = 0;
    Observation obs = tgenmarkovmodel_getNextObservation(gen->mmodel, &delay);

    if(obs == OBSERVATION_TO_ORIGIN || obs == OBSERVATION_TO_SERVER) {
        tgen_debug("Found %s observation on %s '%s' with a generated delay of "
                "%"G_GUINT64_FORMAT" microseconds",
                childType, parentType, gen->actionIDStr, delay);

        /* we should create a new stream or flow now */
        gboolean success = gen->flowOptions ?
                _tgengenerator_createFlow(gen) : _tgengenerator_createStream(gen);

        if(success) {
            tgen_info("Generated new %s successfully on %s action '%s'",
                    childType, parentType, gen->actionIDStr);
        } else {
            tgen_warning("Failed to create a %s on %s action '%s', "
                    "delaying %"G_GUINT64_FORMAT" microseconds before the next try",
                    childType, parentType, gen->actionIDStr, delay);
        }

        /* now wait the delay amount before creating the next one */
        if(_tgengenerator_setTimer(gen, delay)) {
            tgen_info("Generator for %s action '%s' will generate the next %s in %"G_GUINT64_FORMAT" microseconds",
                    parentType, gen->actionIDStr, childType, delay);
        } else {
            tgen_warning("Failed to set timer on %s action '%s' for %"G_GUINT64_FORMAT" "
                    "microseconds. No more %ss can be generated.",
                    parentType, gen->actionIDStr, delay, childType);

            if(gen->numCompleted >= gen->numGenerated) {
                /* tell the driver we are done so it continues the action graph */
                TGenNotifyFlags flags = gen->flowOptions ?
                        TGEN_NOTIFY_TRAFFIC_COMPLETE : TGEN_NOTIFY_FLOW_COMPLETE;
                gen->onComplete(gen->arg, gen->actionID, flags);
                /* delete ourselves, dont use the flow anymore */
                _tgengenerator_unref(gen);
            }
        }
    } else {
        tgen_info("Found %s end observation on %s '%s' after generating %u %ss.",
                childType, parentType, gen->actionIDStr, gen->numGenerated, childType);

        /* we got to the end, we should not create any more streams */
        gen->reachedEndState = TRUE;

        /* if we have no outstanding streams, we are done */
        if(gen->numCompleted >= gen->numGenerated) {
            /* tell the driver we are done so it continues the action graph */
            TGenNotifyFlags flags = gen->flowOptions ?
                    TGEN_NOTIFY_TRAFFIC_COMPLETE : TGEN_NOTIFY_FLOW_COMPLETE;
            gen->onComplete(gen->arg, gen->actionID, flags);
            /* delete ourselves, dont use the flow anymore */
            _tgengenerator_unref(gen);
        }
    }
}

void tgengenerator_start(TGenGenerator* gen) {
    TGEN_ASSERT(gen);

    if(gen->mmodel) {
        /* we create flows or streams according to the Markov model */
        _tgengenerator_generateNext(gen);
    } else {
        /* we only create one stream */
        if(_tgengenerator_createStream(gen)) {
            gen->reachedEndState = TRUE;
        } else {
            /* it failed, tell the driver to advance and free ourselves */
            gen->onComplete(gen->arg, gen->actionID, TGEN_NOTIFY_FLOW_COMPLETE);
            _tgengenerator_unref(gen);
        }
    }
}
