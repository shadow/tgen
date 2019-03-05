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

#define TGEN_VERSION "0.0.1"

#if 1 /* #ifdef DEBUG */
#define TGEN_MAGIC 0xABBABAAB
#define TGEN_ASSERT(obj) g_assert(obj && (obj->magic == TGEN_MAGIC))
#else
#define TGEN_MAGIC 0
#define TGEN_ASSERT(obj)
#endif

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
#include "tgen-transfer.h"
#include "tgen-generator.h"
#include "tgen-driver.h"

#endif /* SHD_TGEN_H_ */
