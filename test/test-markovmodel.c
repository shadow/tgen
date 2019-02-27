#include <stdint.h>

#include <glib.h>

#include "tgen-log.h"
#include "tgen-markovmodel.h"

#define NUM_OBS 100000

static void generate(TGenMarkovModel* mmodel) {
    gsize numObservations = 0;

    guint numServerPackets = 0;
    guint numOriginPackets = 0;
    guint numStreams = 0;


    while(numObservations < NUM_OBS) {
        gint32 nextServerPacketDelay = 0;
        gint32 nextOriginpacketDelay = 0;

        /* make sure the packet model is ready to generate more */
        tgenmarkovmodel_reset(mmodel);

        while(numObservations < NUM_OBS) {
            tgen_info("Generating next observation");
            guint64 delay = 0;
            Observation obs = tgenmarkovmodel_getNextObservation(mmodel, &delay);
            numObservations++;

            /* keep track of cumulative delay for each packet. we need this because
             * we are actually computing independent delays for the server and the origin.
             * we take care not to overflow the int32 type. */
            if(obs == OBSERVATION_PACKET_TO_ORIGIN || obs == OBSERVATION_PACKET_TO_SERVER) {
                if(delay > INT32_MAX) {
                    nextOriginpacketDelay = INT32_MAX;
                    nextServerPacketDelay = INT32_MAX;
                } else {
                    if(((guint64)nextOriginpacketDelay)+delay > INT32_MAX) {
                        nextOriginpacketDelay = INT32_MAX;
                    } else {
                        nextOriginpacketDelay += (gint32)delay;
                    }

                    if(((guint64)nextServerPacketDelay)+delay > INT32_MAX) {
                        nextServerPacketDelay = INT32_MAX;
                    } else {
                        nextServerPacketDelay += (gint32)delay;
                    }
                }
            }

            /* now build the schedule */
            if(obs == OBSERVATION_PACKET_TO_ORIGIN) {
                /* packet to origin means the server sent it. */
                numServerPackets++;

                tgen_info("Found packet to origin observation with packet delay "
                        "%"G_GUINT64_FORMAT", next origin-bound delay is %"G_GUINT64_FORMAT,
                        delay, nextServerPacketDelay);

                nextServerPacketDelay = 0;
            } else if(obs == OBSERVATION_PACKET_TO_SERVER) {
                /* packet to server means the origin sent it. */
                numOriginPackets++;

                tgen_info("Found packet to server observation with packet delay "
                        "%"G_GUINT64_FORMAT", next server-bound delay is %"G_GUINT64_FORMAT,
                        delay, nextOriginpacketDelay);

                nextOriginpacketDelay = 0;
            } else if(obs == OBSERVATION_STREAM) {
                numStreams++;
                tgen_info("Found stream observation with stream delay %"G_GUINT64_FORMAT, delay);
            } else {
                /* we observed an end state, so the packet stream is done. */
                tgen_info("Found end observation");
                break;
            }
        }
    }
}

gint main(gint argc, gchar *argv[]) {
    tgenlog_setLogFilterLevel(G_LOG_LEVEL_INFO);

    if(argc != 3) {
        tgen_info("USAGE: <seed> <path/to/markovmodel.graphml.xml>; e.g., 123456 traffic.packet.model.graphml.xml");
        return EXIT_FAILURE;
    }

    TGenMarkovModel* markovModel = tgenmarkovmodel_newWithSeed(argv[2], (guint32)atoi(argv[1]));
    if(!markovModel) {
        tgen_warning("failed to parse markov model");
        return EXIT_FAILURE;
    }

    generate(markovModel);

    tgenmarkovmodel_unref(markovModel);

    return EXIT_SUCCESS;
}