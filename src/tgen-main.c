/*
 * See LICENSE for licensing information
 */

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <glib.h>
#include <igraph.h>

#include "tgen.h"

static void _tgenmain_cleanup(gint status, gpointer arg) {
    if(arg) {
        TGenDriver* tgen = (TGenDriver*) arg;
        tgendriver_unref(tgen);
    }
}

static gint _tgenmain_returnError(gint flushLogCache) {
    if(flushLogCache) {
        /* make sure we flush any cached messages */
        tgenlog_setLogFilterLevel(0);
    }
    return EXIT_FAILURE;
}

static gint _tgenmain_run(gint argc, gchar *argv[]) {
    /* We generally use glib's random number generation, but in the past subtle
     * bugs have snuck in from using libc's random number generation without
     * seeding it. Defensively seed it here. (glib's is always seeded from an
     * appropriate source of entropy) */
    srand(g_random_int());

    /* get our hostname for logging */
    gchar hostname[128] = {0};
    tgenconfig_gethostname(hostname, 128);

    /* note: messages will not be flushed by the logger until after we read the config
     * and then set the log level in tgenlog_setLogFilterLevel. */
    tgen_message("Initializing TGen v%s running GLib v%u.%u.%u and IGraph v%s "
        "on host %s with process id %i",
        TGEN_VERSION,
        (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
#if defined(IGRAPH_VERSION)
        IGRAPH_VERSION,
#else
        "(n/a)",
#endif
        hostname, (gint)getpid());

    // TODO embedding a tgen graphml inside the shadow.config.xml file not yet supported
//    if(argv[1] && g_str_has_prefix(argv[1], "<?xml")) {
//        /* argv contains the xml contents of the xml file */
//        gchar* tempPath = _tgendriver_makeTempFile();
//        GError* error = NULL;
//        gboolean success = g_file_set_contents(tempPath, argv[1], -1, &error);
//        if(success) {
//            graph = tgengraph_new(tempPath);
//        } else {
//            tgen_warning("error (%i) while generating temporary xml file: %s", error->code, error->message);
//        }
//        g_unlink(tempPath);
//        g_free(tempPath);
//    } else {
//        /* argv contains the apth of a graphml config file */
//        graph = tgengraph_new(argv[1]);
//    }

    /* argv[0] is program name, argv[1] should be config file */
    if (argc != 2) {
        tgen_warning("USAGE: %s path/to/tgen.xml", argv[0]);
        tgen_critical("cannot continue: incorrect argument list format")
        return _tgenmain_returnError(1);
    }

    /* make sure broken pipes (if a tgen peer closes) do not crash our process */
    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        tgen_warning("Unable to set SIG_IGN for signal SIGPIPE");
    } else {
        tgen_message("Set SIG_IGN for signal SIGPIPE");
    }

    /* parse the config file */
    TGenGraph* graph = tgengraph_new(argv[1]);
    if (!graph) {
        tgen_critical("cannot continue: traffic generator config file '%s' failed validation", argv[1]);
        return _tgenmain_returnError(1);
    }

    /* set log level, which again defaults to message if no level was configured */
    TGenStartOptions* options = tgengraph_getStartOptions(graph);
    if(options && options->loglevel.isSet) {
        tgenlog_setLogFilterLevel(options->loglevel.value);
    } else {
        tgenlog_setLogFilterLevel(G_LOG_LEVEL_MESSAGE);
    }

    /* create the new state according to user inputs */
    TGenDriver* tgen = tgendriver_new(graph);

    /* driver should have reffed the graph if it needed it */
    tgengraph_unref(graph);

    if(!tgen) {
        tgen_critical("Error initializing new TrafficGen instance");
        return _tgenmain_returnError(0);
    } else {
        on_exit(_tgenmain_cleanup, tgen);
    }

    /* all of the tgen descriptors are watched internally */
    gint tgenepolld = tgendriver_getEpollDescriptor(tgen);
    if(tgenepolld < 0) {
        tgen_critical("Error retrieving tgen epolld");
        return _tgenmain_returnError(0);
    }

    /* now we need to watch all of the epoll descriptors in our main loop */
    gint mainepolld = epoll_create(1);
    if(mainepolld < 0) {
        tgen_critical("Error in main epoll_create");
        return _tgenmain_returnError(0);
    }

    /* register the tgen epoll descriptor so we can watch its events */
    struct epoll_event mainevent = {0};
    mainevent.events = EPOLLIN|EPOLLOUT;
    epoll_ctl(mainepolld, EPOLL_CTL_ADD, tgenepolld, &mainevent);

    /* main loop - wait for events on the trafficgen epoll descriptors */
    tgen_message("entering main loop to watch descriptors");
    while(TRUE) {
        /* clear the event space */
        memset(&mainevent, 0, sizeof(struct epoll_event));

        /* wait for an event on the tgen descriptor */
        tgen_debug("waiting for events");
        gint nReadyFDs = epoll_wait(mainepolld, &mainevent, 1, -1);

        if(nReadyFDs == -1) {
            /* if the user paused with ctl-z, we get EINTR and can just try again */
            if(errno == EINTR) {
                continue;
            }
            tgen_critical("error %i in client epoll_wait:, %s", errno, g_strerror(errno));
            return _tgenmain_returnError(0);
        }

        /* activate if something is ready */
        if(nReadyFDs > 0) {
            tgen_debug("processing event");
            tgendriver_activateIO(tgen);
        }

        /* break out if trafficgen is done */
        if(tgendriver_hasEnded(tgen)) {
            break;
        }
    }

    tgen_message("finished main loop, cleaning up");

    /* de-register the tgen epoll descriptor and close */
    epoll_ctl(mainepolld, EPOLL_CTL_DEL, tgenepolld, NULL);
    close(mainepolld);
    tgendriver_shutdownIO(tgen);

    tgen_message("returning 0 from main");

    /* _tgenmain_cleanup() should get called via on_exit */
    return EXIT_SUCCESS;
}

gint main(gint argc, gchar *argv[]) {
    return _tgenmain_run(argc, argv);
}
