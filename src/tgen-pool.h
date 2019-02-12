/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_POOL_H_
#define TGEN_POOL_H_

#include <glib.h>

typedef struct _TGenPool TGenPool;

TGenPool* tgenpool_new(GDestroyNotify valueDestroyFunc);
void tgenpool_ref(TGenPool* pool);
void tgenpool_unref(TGenPool* pool);

void tgenpool_add(TGenPool* pool, gpointer item);
gpointer tgenpool_getRandom(TGenPool* pool);

#endif /* TGEN_POOL_H_ */
