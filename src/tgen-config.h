/*
 * See LICENSE for licensing information
 */

#ifndef TGEN_CONFIG_H_
#define TGEN_CONFIG_H_

#include <stddef.h>
#include <glib.h>

gint tgenconfig_gethostname(gchar* name, size_t len);
gchar* tgenconfig_getIP();
gchar* tgenconfig_getSOCKS();

const gchar* tgenconfig_getDefaultPacketMarkovModelName();
const gchar* tgenconfig_getDefaultPacketMarkovModelString();

const gchar* tgenconfig_getDefaultStreamMarkovModelName();
const gchar* tgenconfig_getDefaultStreamMarkovModelString();

#endif /* TGEN_CONFIG_H_ */
