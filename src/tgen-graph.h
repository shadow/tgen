/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_GRAPH_H_
#define TGEN_GRAPH_H_

#include <glib.h>

typedef struct _TGenGraph TGenGraph;

TGenGraph* tgengraph_new(gchar* path);
void tgengraph_ref(TGenGraph* g);
void tgengraph_unref(TGenGraph* g);

TGenAction* tgengraph_getStartAction(TGenGraph* g);
GQueue* tgengraph_getNextActions(TGenGraph* g, TGenAction* action);
gboolean tgengraph_hasEdges(TGenGraph* g);
const gchar* tgengraph_getActionIDStr(TGenGraph* g, TGenAction* action);
const gchar* tgengraph_getGraphPath(TGenGraph* g);

#endif /* TGEN_GRAPH_H_ */
