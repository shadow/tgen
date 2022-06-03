/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_H_
#define SHD_TGEN_H_

#include <glib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define TGEN_VERSION "1.1.0"

#if 1 /* #ifdef DEBUG */
#define TGEN_MAGIC 0xABBABAAB
#define TGENIO_MAGIC 0xDCBAABCD
#define TGENIOCHILD_MAGIC 0xABCDDCBA
#define TGEN_ASSERT(obj) g_assert(obj && (obj->magic == TGEN_MAGIC))
#define TGENIO_ASSERT(obj) g_assert(obj && (obj->magic == TGENIO_MAGIC))
#define TGENIOCHILD_ASSERT(obj) g_assert(obj && (obj->magic == TGENIOCHILD_MAGIC))
#else
#define TGEN_MAGIC 0
#define TGENIO_MAGIC 0
#define TGENIOCHILD_MAGIC 0
#define TGEN_ASSERT(obj)
#define TGENIO_ASSERT(obj)
#define TGENIOCHILD_ASSERT(obj)
#endif

typedef gint TGenActionID;

typedef enum _TGenNotifyFlags {
    TGEN_NOTIFY_NONE = 0,
    TGEN_NOTIFY_STREAM_CREATED = 1 << 0,
    TGEN_NOTIFY_STREAM_COMPLETE = 1 << 1,
    TGEN_NOTIFY_STREAM_SUCCESS = 1 << 2,
    TGEN_NOTIFY_FLOW_CREATED = 1 << 3,
    TGEN_NOTIFY_FLOW_COMPLETE = 1 << 4,
    TGEN_NOTIFY_FLOW_SUCCESS = 1 << 5,
    TGEN_NOTIFY_TRAFFIC_CREATED = 1 << 6,
    TGEN_NOTIFY_TRAFFIC_COMPLETE = 1 << 7,
    TGEN_NOTIFY_TRAFFIC_SUCCESS = 1 << 8,
} TGenNotifyFlags;

typedef void (*TGen_notifyFunc)(gpointer data, TGenActionID actionID, TGenNotifyFlags flags);

#include "tgen-config.h"
#include "tgen-log.h"
#include "tgen-io.h"
#include "tgen-timer.h"
#include "tgen-pool.h"
#include "tgen-peer.h"
#include "tgen-optionparser.h"
#include "tgen-server.h"
#include "tgen-markovmodel.h"
#include "tgen-graph.h"
#include "tgen-transport.h"
#include "tgen-stream.h"
#include "tgen-generator.h"
#include "tgen-driver.h"

#endif /* SHD_TGEN_H_ */
