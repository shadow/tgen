/*
 * See LICENSE for licensing information
 */

#include "tgen.h"

struct _TGenPool {
    GTree* items;
    guint counter;
    gint refcount;
    guint magic;
};

static gint _tgenpool_intCompare(gconstpointer a, gconstpointer b,
        gpointer user_data) {
    const gint* ai = a;
    const gint* bi = b;
    g_assert(ai && bi);
    return *ai > *bi ? +1 : *ai == *bi ? 0 : -1;
}

TGenPool* tgenpool_new(GDestroyNotify valueDestroyFunc) {
    TGenPool* pool = g_new0(TGenPool, 1);
    pool->magic = TGEN_MAGIC;

    pool->items = g_tree_new_full(_tgenpool_intCompare, NULL, g_free, valueDestroyFunc);
    pool->refcount = 1;

    return pool;
}

static void _tgenpool_free(TGenPool* pool) {
    TGEN_ASSERT(pool);
    g_assert(pool->refcount == 0);

    g_tree_destroy(pool->items);

    pool->magic = 0;
    g_free(pool);
}

void tgenpool_ref(TGenPool* pool) {
    TGEN_ASSERT(pool);
    pool->refcount++;
}

void tgenpool_unref(TGenPool* pool) {
    TGEN_ASSERT(pool);
    if(--(pool->refcount) == 0) {
        _tgenpool_free(pool);
    }
}

void tgenpool_add(TGenPool* pool, gpointer item) {
    TGEN_ASSERT(pool);
    gint* key = g_new(gint, 1);
    *key = (pool->counter)++;
    g_tree_insert(pool->items, key, item);
}

gpointer tgenpool_getRandom(TGenPool* pool) {
    TGEN_ASSERT(pool);
    gint numNodes = g_tree_nnodes(pool->items);
    gint position = (numNodes > 1) ? ((gint)(rand() % numNodes)) : 0;
    return g_tree_lookup(pool->items, &position);
}
