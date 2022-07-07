/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_IGRAPH_COMPAT_H_
#define TGEN_IGRAPH_COMPAT_H_

/* Renamed in in igraph 0.9.0 */
#if IGRAPH_VERSION_MAJOR==0 && IGRAPH_VERSION_MINOR<9
#define igraph_set_attribute_table igraph_i_set_attribute_table
#endif

#endif