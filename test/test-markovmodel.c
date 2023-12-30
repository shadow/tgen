#include <stdlib.h>
#include <stdint.h>

#include <glib.h>
#include <igraph.h>

#include "tgen-igraph-compat.h"
#include "tgen-log.h"
#include "tgen-markovmodel.h"

#define NUM_OBS 100000

static void generate(TGenMarkovModel* mmodel) {
    gsize numObservations = 0;

    guint numServerPackets = 0;
    guint numOriginPackets = 0;

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
            if(obs == OBSERVATION_TO_ORIGIN || obs == OBSERVATION_TO_SERVER) {
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
            if(obs == OBSERVATION_TO_ORIGIN) {
                /* packet to origin means the server sent it. */
                numServerPackets++;

                tgen_info("Found packet to origin observation with packet delay "
                        "%"G_GUINT64_FORMAT", next origin-bound delay is %d",
                        delay, nextServerPacketDelay);

                nextServerPacketDelay = 0;
            } else if(obs == OBSERVATION_TO_SERVER) {
                /* packet to server means the origin sent it. */
                numOriginPackets++;

                tgen_info("Found packet to server observation with packet delay "
                        "%"G_GUINT64_FORMAT", next server-bound delay is %d",
                        delay, nextOriginpacketDelay);

                nextOriginpacketDelay = 0;
            } else {
                /* we observed an end state, so the packet stream is done. */
                tgen_info("Found end observation");
                break;
            }
        }
    }

    tgen_info("%d server packets and %d origin packets", numServerPackets, numOriginPackets);
}

gint main(gint argc, gchar *argv[]) {
    tgenlog_setLogFilterLevel(G_LOG_LEVEL_INFO);

    if(argc != 3) {
        tgen_info("USAGE: <seed> <path/to/markovmodel.graphml.xml>; e.g., 123456 traffic.packet.model.graphml.xml");
        return EXIT_FAILURE;
    }

    /* use the built-in C attribute handler. this is set once and then left alone. */
    igraph_set_attribute_table(&igraph_cattribute_table);

    guint32 seed = (guint32)atoi(argv[1]);
    gchar* path = g_strdup(argv[2]);
    gchar* name = g_path_get_basename(path);

    TGenMarkovModel* markovModel = tgenmarkovmodel_newFromPath(name, seed, path);
    if(!markovModel) {
        tgen_warning("failed to parse markov model name %s from file path %s", name, path);
        return EXIT_FAILURE;
    }

    GString* graphString = tgenmarkovmodel_toGraphmlString(markovModel);

    if(!graphString) {
        tgen_warning("Error writing graphml to memory buffer");
        return EXIT_FAILURE;
    }

    tgen_info("Successfully wrote graphml to memory buffer "
            "of length %"G_GSIZE_FORMAT, graphString->len);
    tgen_info("Here is the graphml contents:");
    g_print("%s", graphString->str);

    tgenmarkovmodel_unref(markovModel);

    markovModel = tgenmarkovmodel_newFromString(name, seed, graphString);

    if(!markovModel) {
        tgen_warning("failed to parse markov model name %s "
                "from string buffer of length %"G_GSIZE_FORMAT, name, graphString->len);
        return EXIT_FAILURE;
    }

    generate(markovModel);

    tgenmarkovmodel_unref(markovModel);
    g_string_free(graphString, TRUE);

    return EXIT_SUCCESS;
}
