/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_IGRAPH_COMPAT_H_
#define TGEN_IGRAPH_COMPAT_H_

/* Renamed in igraph 0.9.0 */
#if IGRAPH_VERSION_MAJOR==0 && IGRAPH_VERSION_MINOR<9
#define igraph_set_attribute_table igraph_i_set_attribute_table
#endif

/* Changed in igraph 0.10.0 */
#if IGRAPH_VERSION_MAJOR == 0 && IGRAPH_VERSION_MINOR < 10
// Renamed
#define igraph_connected_components igraph_clusters
#define IGRAPH_ATTRIBUTE_UNSPECIFIED IGRAPH_ATTRIBUTE_DEFAULT
// Change to int types
#define igraph_vector_int_t igraph_vector_t
#define igraph_vector_int_init igraph_vector_init
#define igraph_vector_int_size igraph_vector_size
#define igraph_vector_int_get igraph_vector_e
#define igraph_vector_int_destroy igraph_vector_destroy
// Removed the name arg and return it instead
inline const char *igraph_strvector_get_compat(igraph_strvector_t *sv,
                                               igraph_integer_t idx) {
  char *name_ = NULL;
  (igraph_strvector_get)(sv, idx, &name_);
  return (const char *)name_;
}
#define igraph_strvector_get igraph_strvector_get_compat
#endif

#endif